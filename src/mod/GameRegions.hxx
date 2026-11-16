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
// The manager's post-restore callbacks, in its list's order, into `out`.
void CollectPostRestore(uint8_t* manager, std::vector<GamePostRestore>& out);

} // namespace sfve::mod
