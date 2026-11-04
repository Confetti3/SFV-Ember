#include "core/StateRegions.hxx"

#include <string.h>

#include <algorithm>
#include <bit>

#define XXH_STATIC_LINKING_ONLY // stack-allocated XXH3 state
#include <xxhash.h>

namespace sfve {

namespace {

constexpr size_t kChunk = 4096; // bytes masked and hashed at a time

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

} // namespace

// Zeroes the 8 bytes at every 4-byte-aligned position of `bytes` (copied
// from address `at`) that hold an address: a region may start anywhere in an
// object, and packed structures keep addresses on 4-byte boundaries. The
// original bytes are tested, so zeroing one word cannot hide or create
// another. The position 4 bytes into an address is not tested: its low half
// is that address's high half, and with a small value after it the pair can
// look like an address in one process and not in another (a heap address's
// high half is 0x204 in one, 0x246 in the other). Words cut by the chunk's
// edges are left alone.
void StateLayout::ZeroAddresses(uint8_t* bytes, size_t n, uintptr_t at) const
{
	uint8_t original[kChunk];
	memcpy(original, bytes, n);
	for (size_t off = (4 - (at & 3)) & 3; off + 8 <= n; off += 4) {
		uint64_t value;
		memcpy(&value, original + off, 8);
		if (!addressTest_(value)) continue;
		memset(bytes + off, 0, 8);
		off += 4; // past the address's high half
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
	for (const StateRegion& r : regions_) {
		memcpy(dst, r.ptr, r.size);
		dst += r.size;
	}
}

void StateLayout::LoadFrom(const uint8_t* src) const
{
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
	alignas(8) uint8_t chunk[kChunk];
	// After the first chunk every chunk starts at a real 8-byte boundary, so
	// no address word is cut by a chunk edge (only by the region's own ends).
	const size_t misalignment = reinterpret_cast<uintptr_t>(r.ptr) & 7;
	size_t n = 0;
	for (size_t at = 0; at < r.size; at += n) {
		n = (at == 0 ? kChunk - misalignment : kChunk);
		if (n > r.size - at) n = r.size - at;
		memcpy(chunk, bytes + at, n);
		// Masked words and dwords (region-relative) read as zero.
		ZeroMarked(chunk, at, n, r.ignore, 8);
		if (scope == HashScope::Peers) ZeroMarked(chunk, at, n, r.peerIgnore, 4);
		if (addressTest_) ZeroAddresses(chunk, n, reinterpret_cast<uintptr_t>(r.ptr) + at);
		XXH3_64bits_update(&state, chunk, n);
	}
	return XXH3_64bits_digest(&state);
}

uint64_t StateLayout::HashLive(HashScope scope) const
{
	uint64_t h = 0x5346564521ull; // "SFVE!"
	for (size_t i = 0; i < regions_.size(); i++) {
		if (Hashed(regions_[i], scope)) h += Mix(HashRegion(i, regions_[i].ptr, scope), regions_[i].size);
	}
	return h;
}

uint64_t StateLayout::HashSaved(const uint8_t* src, HashScope scope) const
{
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
		if (Hashed(regions_[i], HashScope::Peers)) out.push_back({regions_[i].size, HashRegion(i, src, HashScope::Peers)});
		src += regions_[i].size;
	}
	std::sort(out.begin(), out.end(),
		[](const RegionHash& a, const RegionHash& b) { return a.size != b.size ? a.size < b.size : a.hash < b.hash; });
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
