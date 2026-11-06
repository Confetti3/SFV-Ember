#pragma once

// The rollback state of one game, as a list of memory regions.
//
// Saving copies every region into one flat buffer; loading copies it back.
// Hashing reads the same bytes but leaves out data that differs without the
// simulation differing. Two scopes:
//
// * Local (one process: the synctest, load checks) leaves out
//   - masked words: 8-byte words marked ignored (heap and vtable pointers,
//     engine clocks);
//   - unchecked regions: restored on rollback but never hashed (SFV's pawn
//     and camera animation clocks follow how ticks fall into rendered frames,
//     which differs per machine).
// * Peers (the hash two processes compare) leaves out all of that, and also
//   - peer-masked dwords: values each process legitimately has its own of (a
//     clock reading, a heap leftover in padding) that a replay in one process
//     still reproduces, so the synctest keeps checking them;
//   - regions not compared with peers: presentation each process keeps its
//     own way (still saved, restored and synctest-checked).
//
// Hashes do not depend on region order: a game may list the same regions in a
// different order in each process (SFV builds its table in allocation order),
// so region hashes are combined commutatively, and the per-region hashes peers
// compare are listed in a canonical order (by size, then hash). An address
// test (optional) hashes every 8-byte word, aligned to its real address, that
// holds an address of this process as zero: heap and code addresses differ
// between processes even when the state they describe is the same.
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
	bool comparedWithPeers = true;  // part of the hash peers compare (when checked)
	std::vector<uint64_t> ignore;     // bit i set: word i (bytes 8i..8i+7) not hashed
	std::vector<uint64_t> peerIgnore; // bit i set: dword i (bytes 4i..4i+3) not in the peers' hash
};

enum class HashScope {
	Local, // everything one process must reproduce on replay
	Peers, // what two processes playing the same match must agree on
};

// A region's hash as peers compare it.
struct RegionHash {
	uint64_t size = 0;
	uint64_t hash = 0;
	// The region's place in this process's layout, which names it there;
	// not compared (two processes list their regions in different orders).
	size_t region = 0;
	bool operator==(const RegionHash& o) const { return size == o.size && hash == o.hash; }
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
	// Marks the 4-byte dword holding `offset` in region `region` as left out
	// of the peers' hash only.
	void IgnoreForPeers(size_t region, size_t offset);
	// Leaves region `region` out of the peers' hash only.
	void ExcludeFromPeers(size_t region) { regions_.at(region).comparedWithPeers = false; }

	const std::vector<StateRegion>& Regions() const { return regions_; }
	const StateRegion* Find(const std::string& name) const;
	// The region named `key`, or, when `key` is a number, the first region of
	// that size (how the pipe's diagnostic commands name regions).
	const StateRegion* FindByNameOrSize(const std::string& key) const;
	size_t TotalSize() const { return totalSize_; }
	bool Empty() const { return regions_.empty(); }

	void SaveTo(uint8_t* dst) const;
	void LoadFrom(const uint8_t* src) const;

	// Hash of the live memory, and of a buffer laid out by SaveTo. Both give
	// the same value for the same contents.
	uint64_t HashLive(HashScope scope = HashScope::Local) const;
	uint64_t HashSaved(const uint8_t* src, HashScope scope = HashScope::Local) const;
	// One hash per region (unchecked regions hash as 0), in layout order, for
	// locating a desync within one process.
	void RegionHashesSaved(const uint8_t* src, std::vector<uint64_t>& out) const;
	// The regions peers compare, each with its peers' hash, in canonical order
	// (size, then hash): equal lists on two processes whatever order each
	// lists its regions in.
	void PeerRegionHashesSaved(const uint8_t* src, std::vector<RegionHash>& out) const;

	uint64_t LayoutId() const;

private:
	bool Hashed(const StateRegion& r, HashScope scope) const
	{
		return r.checked && (scope == HashScope::Local || r.comparedWithPeers);
	}
	uint64_t HashRegion(size_t index, const uint8_t* bytes, HashScope scope) const;
	void ZeroAddresses(uint8_t* bytes, size_t n, uintptr_t at) const;

	std::vector<StateRegion> regions_;
	size_t totalSize_ = 0;
	AddressTest addressTest_ = nullptr;
};

} // namespace sfve
