#pragma once

// The rollback state of one game, as a list of memory regions.
//
// Saving copies every region into one flat buffer; loading copies it back.
// Hashing reads the same bytes but leaves out two kinds of data that differ
// between machines without the simulation differing:
//
// * masked words: 8-byte words marked ignored (heap and vtable pointers,
//   engine clocks). They are still saved and restored.
// * unchecked regions: restored on rollback but never hashed (SFV's pawn and
//   camera animation clocks follow how ticks fall into rendered frames, which
//   differs per machine).
//
// Hashes do not depend on region order: a game may list the same regions in a
// different order in each process (SFV builds its table in allocation order),
// so region hashes are combined commutatively. An address test (optional)
// hashes every 8-byte word, aligned to its real address, that holds an
// address of this process as zero: heap and code addresses differ between
// processes even when the state they describe is the same.
//
// The layout id combines sizes, flags and masks the same way, so two peers can
// check they compare the same thing before comparing hashes.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace sfve {

struct StateRegion {
	std::string name;
	uint8_t* ptr = nullptr;
	size_t size = 0;
	bool checked = true;            // part of the hash
	std::vector<uint64_t> ignore;   // bit i set: word i (bytes 8i..8i+7) not hashed
};

// True for a value that is an address of this process.
using AddressTest = bool (*)(uint64_t value);

class StateLayout {
public:
	void Clear();
	void SetAddressTest(AddressTest test) { addressTest_ = test; }
	size_t Add(std::string name, void* ptr, size_t size, bool checked = true);
	// Marks the 8-byte word holding `offset` in region `region` as not hashed.
	void IgnoreWord(size_t region, size_t offset);
	bool IsIgnored(size_t region, size_t offset) const;

	const std::vector<StateRegion>& Regions() const { return regions_; }
	const StateRegion* Find(const std::string& name) const;
	size_t TotalSize() const { return totalSize_; }
	bool Empty() const { return regions_.empty(); }

	void SaveTo(uint8_t* dst) const;
	void LoadFrom(const uint8_t* src) const;

	// Hash of the live memory, and of a buffer laid out by SaveTo. Both give
	// the same value for the same contents.
	uint64_t HashLive() const;
	uint64_t HashSaved(const uint8_t* src) const;
	// One hash per region (unchecked regions hash as 0), for locating a desync.
	void RegionHashesSaved(const uint8_t* src, std::vector<uint64_t>& out) const;

	uint64_t LayoutId() const;

private:
	uint64_t HashRegion(size_t index, const uint8_t* bytes) const;
	void ZeroAddresses(uint8_t* bytes, size_t n, uintptr_t at) const;

	std::vector<StateRegion> regions_;
	size_t totalSize_ = 0;
	AddressTest addressTest_ = nullptr;
};

} // namespace sfve
