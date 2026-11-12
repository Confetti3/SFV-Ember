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
//   - unchecked regions: restored on rollback but never hashed (SFV's
//     camera follows how ticks fall into rendered frames, which differs per
//     machine).
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
// test (optional) hashes every 8-byte word on a 4-byte boundary of its real
// address that holds an address of this process as zero: heap and code
// addresses differ between processes even when the state they describe is the
// same.
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
	// `mapped` (optional): the value is an address of this process's memory
	// (committed), asked only where two overlapping 8-byte windows both pass
	// `test` (AddressAt).
	void SetAddressTest(AddressTest test, AddressTest mapped = nullptr)
	{
		addressTest_ = test;
		addressMapped_ = mapped;
	}
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
	// The hash of some regions' live memory, each as HashLive takes it (masked,
	// addresses zeroed; a region not hashed in `scope` left out): what that part
	// of the state is now. Indices past the layout are left out.
	uint64_t HashLiveRegions(const std::vector<size_t>& regions, HashScope scope = HashScope::Local) const;
	// One hash per region (unchecked regions hash as 0), in layout order, for
	// locating a desync within one process.
	void RegionHashesSaved(const uint8_t* src, std::vector<uint64_t>& out) const;
	// The regions peers compare, each with its peers' hash, in canonical order
	// (size, then hash): equal lists on two processes whatever order each
	// lists its regions in.
	void PeerRegionHashesSaved(const uint8_t* src, std::vector<RegionHash>& out) const;

	uint64_t LayoutId() const;

	// The 4-byte units of region `region` (unit i: region bytes 4i..4i+3) that
	// the peers' hash zeroes as part of an address, with the region's bytes as
	// `bytes` holds them (its size): the same scan HashRegion runs, over the
	// region with its masked words and peers' dwords zeroed first. Bit i of
	// `out` is unit i. None without an address test.
	void AddressUnits(size_t region, const uint8_t* bytes, std::vector<uint64_t>& out) const;

	// What saving, loading and hashing cost since the last TakeCosts: the
	// calls and their QueryPerformanceCounter ticks (the cost log, SFV's
	// adapter). Hashing counts HashSaved and HashLive, the state hashes; the
	// hash of some regions (HashLiveRegions) is its caller's to count.
	struct Costs {
		uint64_t saves = 0, loads = 0, hashes = 0;
		int64_t saveTicks = 0, loadTicks = 0, hashTicks = 0;
		// Counted by the rollback session, which knows why
		// (docs/design/beta8/perf.md): saves that kept no copy, as no load can
		// reach them; frames hashed once final, after their save; and saves a
		// rollback replaced before they were final, never hashed.
		uint64_t skippedSaves = 0, confirmedHashes = 0, droppedHashes = 0;
	};
	Costs TakeCosts() const;
	void CountSkippedSave() const { costs_.skippedSaves++; }
	void CountConfirmedHash() const { costs_.confirmedHashes++; }
	void CountDroppedHash() const { costs_.droppedHashes++; }

private:
	bool Hashed(const StateRegion& r, HashScope scope) const
	{
		return r.checked && (scope == HashScope::Local || r.comparedWithPeers);
	}
	uint64_t HashRegion(size_t index, const uint8_t* bytes, HashScope scope) const;
	// Where one pass over a region's addresses stands between the windows it
	// is read in (region offsets).
	struct AddressScan {
		size_t next = 0;     // the next 4-byte boundary to test
		size_t zeroedTo = 0; // the end of the last address found, which may lie past its window
		uintptr_t base = 0;  // the region's real address
	};
	// Tests the 8-byte windows that start in [at, at + chunk) of a window of
	// `read` region bytes from `at`, zeroing each address.
	void ZeroAddresses(uint8_t* window, size_t at, size_t chunk, size_t read, AddressScan& scan) const;
	// The 8-byte window at `word` (real address `address`, `after` readable
	// bytes past its end) holds an address (see the .cxx).
	bool AddressAt(const uint8_t* word, uintptr_t address, size_t after) const;

	std::vector<StateRegion> regions_;
	size_t totalSize_ = 0;
	AddressTest addressTest_ = nullptr;
	AddressTest addressMapped_ = nullptr;
	mutable Costs costs_; // TakeCosts; the game thread's
};

} // namespace sfve
