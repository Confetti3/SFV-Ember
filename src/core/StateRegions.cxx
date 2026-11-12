#include "core/StateRegions.hxx"

#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <algorithm>
#include <bit>

#define XXH_STATIC_LINKING_ONLY // stack-allocated XXH3 state
#include <xxhash.h>

namespace sfve {

namespace {

constexpr size_t kChunk = 4096; // bytes masked and hashed at a time
// The most of an 8-byte word that starts in a chunk and ends past it: each
// chunk is read with that much of the next, so its words are tested whole.
constexpr size_t kLookahead = 7;

// Order-independent combination of one region's hash into the total.
uint64_t Mix(uint64_t regionHash, uint64_t size)
{
	const uint64_t parts[2] = {regionHash, size};
	return XXH3_64bits(parts, sizeof(parts));
}

// Zeroes the bytes of `chunk` (bytes at..at+n of its region) that lie in a
// marked unit: bit i of `marks` covers region bytes unit*i .. unit*(i+1)-1. A
// unit may be cut by the chunk or the region's end; only its bytes inside
// count. Only set bits are visited, so sparse masks cost little.
void ZeroMarked(uint8_t* chunk, size_t at, size_t n, const std::vector<uint64_t>& marks, size_t unit)
{
	const size_t first = at / unit;
	const size_t last = (at + n - 1) / unit;
	for (size_t block = first / 64; block <= last / 64 && block < marks.size(); block++) {
		for (uint64_t bits = marks[block]; bits; bits &= bits - 1) {
			const size_t i = block * 64 + (size_t)std::countr_zero(bits);
			if (i < first || i > last) continue;
			const size_t begin = i * unit > at ? i * unit - at : 0;
			const size_t end = std::min(i * unit + unit - at, n);
			memset(chunk + begin, 0, end - begin);
		}
	}
}

void Mark(std::vector<uint64_t>& marks, size_t unitIndex)
{
	marks[unitIndex / 64] |= 1ull << (unitIndex % 64);
}

int64_t Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

// Adds the time from its construction to its end to one of the costs.
class Timed {
public:
	Timed(uint64_t& calls, int64_t& ticks) : calls_(calls), ticks_(ticks), from_(Now()) {}
	~Timed()
	{
		calls_++;
		ticks_ += Now() - from_;
	}
	Timed(const Timed&) = delete;
	Timed& operator=(const Timed&) = delete;

private:
	uint64_t& calls_;
	int64_t& ticks_;
	int64_t from_;
};

} // namespace

// Zeroes the 8 bytes at every position that holds an address, and nothing
// else. Positions are the region's 4-byte boundaries by real address: a
// region may start anywhere in an object, and packed structures keep
// addresses on 4-byte boundaries. The region is tested in one pass from its
// start, whatever windows it is read in: `window` holds region bytes
// at..at+n, the positions from `scan.next` whose whole word lies in it are
// tested, and the scan carries on in the next window.
//
// The scan carries two offsets because they are different facts: the next
// position to test, and the end of the last address it zeroed. Only the
// second says what to zero at the start of a window (the rest of an address
// that ran past the last window's end). The first can lie past a window's
// start without anything before it being an address: the bytes before the
// region's first 4-byte boundary, or the end of a word that tested as no
// address across the window's start. Those bytes are hashed as they are.
//
// The position 4 bytes into an address is not tested: its low half is that
// address's high half, and with a small value after it the pair can look like
// an address in one process and not in another (a heap address's high half is
// 0x204 in one, 0x246 in the other). So the scan resumes after each address,
// which also means no word it tests holds a byte it has already zeroed. Words
// cut by the region's own ends are left alone.
void StateLayout::ZeroAddresses(uint8_t* window, size_t at, size_t n, AddressScan& scan) const
{
	if (scan.zeroedTo > at) memset(window, 0, scan.zeroedTo - at);
	while (scan.next + 8 <= at + n) {
		uint8_t* word = window + (scan.next - at);
		uint64_t value;
		memcpy(&value, word, 8);
		if (addressTest_(value)) {
			memset(word, 0, 8);
			scan.next += 8;
			scan.zeroedTo = scan.next;
		} else {
			scan.next += 4;
		}
	}
}

void StateLayout::Clear()
{
	regions_.clear();
	totalSize_ = 0;
}

size_t StateLayout::Add(std::string name, void* ptr, size_t size, bool checked)
{
	StateRegion r;
	r.name = std::move(name);
	r.ptr = static_cast<uint8_t*>(ptr);
	r.size = size;
	r.checked = checked;
	r.ignore.assign((size / 8 + 64) / 64, 0);
	r.peerIgnore.assign((size / 4 + 64) / 64, 0);
	regions_.push_back(std::move(r));
	totalSize_ += size;
	return regions_.size() - 1;
}

const StateRegion* StateLayout::Find(const std::string& name) const
{
	for (const StateRegion& r : regions_) {
		if (r.name == name) return &r;
	}
	return nullptr;
}

const StateRegion* StateLayout::FindByNameOrSize(const std::string& key) const
{
	if (key.empty() || key[0] < '0' || key[0] > '9') return Find(key);
	const size_t size = strtoull(key.c_str(), nullptr, 10);
	for (const StateRegion& r : regions_) {
		if (r.size == size) return &r;
	}
	return nullptr;
}

void StateLayout::IgnoreWord(size_t region, size_t offset)
{
	StateRegion& r = regions_.at(region);
	if (offset >= r.size) return;
	Mark(r.ignore, offset / 8);
}

void StateLayout::IgnoreForPeers(size_t region, size_t offset)
{
	StateRegion& r = regions_.at(region);
	if (offset >= r.size) return;
	Mark(r.peerIgnore, offset / 4);
}

bool StateLayout::IsIgnored(size_t region, size_t offset) const
{
	const StateRegion& r = regions_.at(region);
	const size_t word = offset / 8;
	return word / 64 < r.ignore.size() && ((r.ignore[word / 64] >> (word % 64)) & 1);
}

void StateLayout::SaveTo(uint8_t* dst) const
{
	const Timed timed(costs_.saves, costs_.saveTicks);
	for (const StateRegion& r : regions_) {
		memcpy(dst, r.ptr, r.size);
		dst += r.size;
	}
}

void StateLayout::LoadFrom(const uint8_t* src) const
{
	const Timed timed(costs_.loads, costs_.loadTicks);
	for (const StateRegion& r : regions_) {
		memcpy(r.ptr, src, r.size);
		src += r.size;
	}
}

uint64_t StateLayout::HashRegion(size_t index, const uint8_t* bytes, HashScope scope) const
{
	const StateRegion& r = regions_[index];
	XXH3_state_t state;
	XXH3_64bits_reset(&state);
	// Each chunk is read with the start of the next, so an address across
	// their edge is tested whole, and what is zeroed does not depend on where
	// the chunks fall: the hash is the whole region's, masked and with its
	// addresses zeroed.
	uint8_t window[kChunk + kLookahead];
	AddressScan scan{.next = (4 - (reinterpret_cast<uintptr_t>(r.ptr) & 3)) & 3}; // from the first 4-byte boundary
	for (size_t at = 0; at < r.size; at += kChunk) {
		const size_t n = std::min(kChunk, r.size - at);
		const size_t read = std::min(n + kLookahead, r.size - at);
		memcpy(window, bytes + at, read);
		// Masked words and dwords (region-relative) read as zero.
		ZeroMarked(window, at, read, r.ignore, 8);
		if (scope == HashScope::Peers) ZeroMarked(window, at, read, r.peerIgnore, 4);
		if (addressTest_) ZeroAddresses(window, at, read, scan);
		XXH3_64bits_update(&state, window, n);
	}
	return XXH3_64bits_digest(&state);
}

uint64_t StateLayout::HashLive(HashScope scope) const
{
	const Timed timed(costs_.hashes, costs_.hashTicks);
	uint64_t h = 0x5346564521ull; // "SFVE!"
	for (size_t i = 0; i < regions_.size(); i++) {
		if (Hashed(regions_[i], scope)) h += Mix(HashRegion(i, regions_[i].ptr, scope), regions_[i].size);
	}
	return h;
}

uint64_t StateLayout::HashLiveRegions(const std::vector<size_t>& regions, HashScope scope) const
{
	uint64_t h = 0x5346564521ull;
	for (const size_t i : regions) {
		if (i < regions_.size() && Hashed(regions_[i], scope)) h += Mix(HashRegion(i, regions_[i].ptr, scope), regions_[i].size);
	}
	return h;
}

uint64_t StateLayout::HashSaved(const uint8_t* src, HashScope scope) const
{
	const Timed timed(costs_.hashes, costs_.hashTicks);
	uint64_t h = 0x5346564521ull;
	for (size_t i = 0; i < regions_.size(); i++) {
		if (Hashed(regions_[i], scope)) h += Mix(HashRegion(i, src, scope), regions_[i].size);
		src += regions_[i].size;
	}
	return h;
}

void StateLayout::RegionHashesSaved(const uint8_t* src, std::vector<uint64_t>& out) const
{
	out.assign(regions_.size(), 0);
	for (size_t i = 0; i < regions_.size(); i++) {
		if (regions_[i].checked) out[i] = HashRegion(i, src, HashScope::Local);
		src += regions_[i].size;
	}
}

void StateLayout::PeerRegionHashesSaved(const uint8_t* src, std::vector<RegionHash>& out) const
{
	out.clear();
	for (size_t i = 0; i < regions_.size(); i++) {
		if (Hashed(regions_[i], HashScope::Peers)) out.push_back({regions_[i].size, HashRegion(i, src, HashScope::Peers), i});
		src += regions_[i].size;
	}
	std::sort(out.begin(), out.end(),
		[](const RegionHash& a, const RegionHash& b) { return a.size != b.size ? a.size < b.size : a.hash < b.hash; });
}

void StateLayout::AddressUnits(size_t region, const uint8_t* bytes, std::vector<uint64_t>& out) const
{
	const StateRegion& r = regions_.at(region);
	out.assign((r.size / 4 + 64) / 64, 0);
	if (!addressTest_ || r.size < 8) return;
	// What HashRegion's scan reads: masked words and the peers' dwords as zero.
	std::vector<uint8_t> masked(bytes, bytes + r.size);
	ZeroMarked(masked.data(), 0, r.size, r.ignore, 8);
	ZeroMarked(masked.data(), 0, r.size, r.peerIgnore, 4);
	size_t at = (4 - (reinterpret_cast<uintptr_t>(r.ptr) & 3)) & 3; // from the first 4-byte boundary
	while (at + 8 <= r.size) {
		uint64_t value;
		memcpy(&value, masked.data() + at, 8);
		if (!addressTest_(value)) {
			at += 4;
			continue;
		}
		for (size_t unit = at / 4; unit <= (at + 7) / 4; unit++) Mark(out, unit);
		at += 8; // as ZeroAddresses: the scan resumes after the address
	}
}

StateLayout::Costs StateLayout::TakeCosts() const
{
	const Costs taken = costs_;
	costs_ = {};
	return taken;
}

uint64_t StateLayout::LayoutId() const
{
	uint64_t id = 0;
	for (const StateRegion& r : regions_) {
		XXH3_state_t state;
		XXH3_64bits_reset(&state);
		const uint64_t sizeAndFlags[3] = {r.size, r.checked ? 1ull : 0ull, r.comparedWithPeers ? 1ull : 0ull};
		XXH3_64bits_update(&state, sizeAndFlags, sizeof(sizeAndFlags));
		XXH3_64bits_update(&state, r.ignore.data(), r.ignore.size() * sizeof(uint64_t));
		XXH3_64bits_update(&state, r.peerIgnore.data(), r.peerIgnore.size() * sizeof(uint64_t));
		id += XXH3_64bits_digest(&state);
	}
	return id;
}

} // namespace sfve
