#pragma once

// The game's table of rollback regions (its rollback manager's
// {void* ptr; uint64 size} entries, sfv::manager::kRegionsBegin..End) as the
// adapter's bind read it. The adapter's layout copies the regions the table
// listed then, so every save and load goes through those addresses. Should
// the game build the table again with other entries (SFV_REGION_MAP.md:
// message 15 clears it, message 1 builds it), those addresses may describe
// nothing any more: copying through them is memory corruption, or a desync
// from regions left out.
//
// So the table's entries are compared with the bind's each time the session
// asks the adapter for a failure (GameAdapter::StateFailure: before every save
// and every load, and after every frame a watcher plays). The same entries
// are the same table wherever the game keeps them, and a table cleared to be
// built again is no change yet; any other difference is latched, for the
// session to fail on.

#include <stddef.h>
#include <stdint.h>

namespace sfve::mod {

class RegionTable {
public:
	// The table of `manager` as it is now, read after the adapter has checked
	// it (AddGameRegions); false, and nothing bound, when it cannot be read or
	// is empty.
	bool Bind(const uint8_t* manager);
	void Forget() { *this = {}; }
	// Why the table no longer lists the bound entries, latched; null while it
	// does, and when nothing is bound. Reads the game's memory (game thread).
	const char* Changed() const;

private:
	const uint8_t* manager_ = nullptr;
	size_t entries_ = 0;
	uint64_t hash_ = 0; // XXH3 of the entries
	mutable char changed_[160] = {};
};

} // namespace sfve::mod
