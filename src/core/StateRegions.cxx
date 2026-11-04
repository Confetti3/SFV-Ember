#include "core/StateRegions.hxx"

#include <string.h>

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

} // namespace

// Zeroes the 8 bytes at every 4-byte-aligned position of `bytes` (copied
// from address `at`) that hold an address: a region may start anywhere in an
// object, and packed structures keep addresses on 4-byte boundaries. The
// original bytes are tested, so zeroing one word cannot hide or create
// another. Words cut by the chunk's edges are left alone.
void StateLayout::ZeroAddresses(uint8_t* bytes, size_t n, uintptr_t at) const
{
	uint8_t original[kChunk];
	memcpy(original, bytes, n);
	for (size_t off = (4 - (at & 3)) & 3; off + 8 <= n; off += 4) {
		uint64_t value;
		memcpy(&value, original + off, 8);
		if (addressTest_(value)) memset(bytes + off, 0, 8);
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
	const size_t word = offset / 8;
	r.ignore[word / 64] |= 1ull << (word % 64);
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

uint64_t StateLayout::HashRegion(size_t index, const uint8_t* bytes) const
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
		// Masked words (region-relative) read as zero; a word may be cut by
		// the chunk or the region's end, and only its bytes inside count.
		for (size_t word = at / 8; word * 8 < at + n; word++) {
			if (!((r.ignore[word / 64] >> (word % 64)) & 1)) continue;
			const size_t begin = word * 8 > at ? word * 8 - at : 0;
			const size_t end = word * 8 + 8 < at + n ? word * 8 + 8 - at : n;
			memset(chunk + begin, 0, end - begin);
		}
		if (addressTest_) ZeroAddresses(chunk, n, reinterpret_cast<uintptr_t>(r.ptr) + at);
		XXH3_64bits_update(&state, chunk, n);
	}
	return XXH3_64bits_digest(&state);
}

uint64_t StateLayout::HashLive() const
{
	uint64_t h = 0x5346564521ull; // "SFVE!"
	for (size_t i = 0; i < regions_.size(); i++) {
		if (regions_[i].checked) h += Mix(HashRegion(i, regions_[i].ptr), regions_[i].size);
	}
	return h;
}

uint64_t StateLayout::HashSaved(const uint8_t* src) const
{
	uint64_t h = 0x5346564521ull;
	for (size_t i = 0; i < regions_.size(); i++) {
		if (regions_[i].checked) h += Mix(HashRegion(i, src), regions_[i].size);
		src += regions_[i].size;
	}
	return h;
}

void StateLayout::RegionHashesSaved(const uint8_t* src, std::vector<uint64_t>& out) const
{
	out.assign(regions_.size(), 0);
	for (size_t i = 0; i < regions_.size(); i++) {
		if (regions_[i].checked) out[i] = HashRegion(i, src);
		src += regions_[i].size;
	}
}

uint64_t StateLayout::LayoutId() const
{
	uint64_t id = 0;
	for (const StateRegion& r : regions_) {
		XXH3_state_t state;
		XXH3_64bits_reset(&state);
		const uint64_t sizeAndFlag[2] = {r.size, r.checked ? 1ull : 0ull};
		XXH3_64bits_update(&state, sizeAndFlag, sizeof(sizeAndFlag));
		XXH3_64bits_update(&state, r.ignore.data(), r.ignore.size() * sizeof(uint64_t));
		id += XXH3_64bits_digest(&state);
	}
	return id;
}

} // namespace sfve
