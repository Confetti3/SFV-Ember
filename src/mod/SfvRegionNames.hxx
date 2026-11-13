#pragma once

// Names for the regions of the game's rollback snapshot, by owner.
//
// The rollback manager's table lists only {address, size}: each entry is a
// run of fields some unit or part registered, coalesced with its neighbours.
// RegionNames finds the battle's known units (by type id, FindUnit) and their
// parts (unit+192..+200, typed by calling their type getter), computes where
// each registered run the map lists should start (docs/design/
// SFV_REGION_MAP.md, section 2), and names a table entry by the run that
// starts at its address: "CharaUnit[1].CharaStatusPart+0x238". An entry no
// known run starts at is "unowned", and the summary says what did not match
// (an unknown owner, a run of another length, an expected run the table does
// not have), which is how the map's single-source rows get checked.
//
// Game thread, at bind, after the table is built.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace sfve::mod {

struct OwnerSpec; // an owner type's registered runs (SfvRegionNames.cxx)

// A table entry's owner: the registered run that starts at its address.
struct NamedRun {
	std::string name;           // "CharaUnit[1].CharaStatusPart+0x238"; empty when unowned
	const char* owner = nullptr; // the owner's type, e.g. "CharaStatusPart"
	uint32_t offset = 0;        // the run's start in its owner
};

// A known object of the battle with its extent, for classing a pointer as
// "object + offset" (the desync report's state file census).
struct NamedObject {
	uintptr_t address = 0;
	std::string name;     // "CharaUnit[1].CharaStatusPart", "CharaUnit[0]->0xC58" for a sub-object
	// The bytes from `address` the mod can vouch for: one past the end of the
	// last field the object's tables register (a run, a sub-object's pointer,
	// a unit's parts vector). That is a LOWER BOUND, a guess at the object's
	// real size: the game's objects are larger (the tables cover only what
	// the rollback saves), so a pointer into an unregistered tail is not
	// classed as this object's. 0 for a part no table knows.
	size_t extent = 0;
};

class RegionNames {
public:
	// Finds the known objects from any unit of the battle and the rollback
	// manager (the root unit is the manager's owner).
	void Collect(uint8_t* anyUnit, uint8_t* manager);

	// The run a table entry starts with, or an empty name when no known run
	// starts at `ptr`. Marks the run as seen.
	NamedRun Name(const uint8_t* ptr, size_t size);

	// Logs what matched, every entry without an owner, runs whose length
	// differs from the map's, and expected runs the table did not list.
	void LogSummary();

	// Every known object, by address (read-only; valid until the next Collect).
	// RegionNames belongs to the game thread: a reader on another thread takes
	// a copy here, on the game thread, and reads the copy.
	const std::vector<NamedObject>& Objects() const { return objects_; }

private:
	struct Run {
		NamedRun named;
		size_t expectedSize = 0;
		bool seen = false;
	};
	void AddObject(const std::string& name, uint8_t* object, const OwnerSpec& spec, size_t minExtent = 0);
	void AddParts(const std::string& unitName, uint8_t* unit);
	// The known object whose start is nearest below `ptr` (for unowned entries).
	std::string Nearest(uintptr_t ptr) const;

	std::unordered_map<uintptr_t, Run> runs_; // by start address
	std::vector<NamedObject> objects_; // sorted by address once Collect is done
	std::vector<std::string> unowned_;
	std::vector<std::string> otherSize_;
	int named_ = 0;
	std::vector<std::string> unknownParts_;
};

} // namespace sfve::mod
