#pragma once

// What SfvAdapter's bind reads of the game's rollback manager: every region
// its table lists, named by its owner, and the callbacks the game's own
// restore runs after copying them back.

#include "core/StateRegions.hxx"
#include "mod/SfvRegionNames.hxx"

#include <stdint.h>

#include <vector>

namespace sfve::mod {

// A post-restore callback as the manager's list holds it.
struct GamePostRestore {
	void (*fn)(void* context, const int32_t* frameId);
	void* context;
};

// Every region of the game's rollback manager added to `layout`, named by its
// owner (`names` collects the owners from `rollbackUnit`); the runs
// sfv::manager::kRunRules lists are restored but not hashed, or masked.
// False (logged) when the table looks wrong.
bool AddGameRegions(StateLayout& layout, RegionNames& names, uint8_t* rollbackUnit, uint8_t* manager);
// One entry of the table, `size` bytes at `ptr`, added to `layout` as `run`
// names it ("game.<entry>" when it has no owner), with what every
// sfv::manager::kRunRules row for its owner and run offset says: a kind (not
// hashed; the name's suffix), masked words, or dwords left out of the peers'
// hash. `applied` (one count per row, or null) counts the rows that matched.
// Returns the region's index.
size_t AddGameRun(StateLayout& layout, const NamedRun& run, size_t entry, uint8_t* ptr, size_t size, int* applied);
// The manager's post-restore callbacks, in its list's order, into `out`.
void CollectPostRestore(uint8_t* manager, std::vector<GamePostRestore>& out);

} // namespace sfve::mod
