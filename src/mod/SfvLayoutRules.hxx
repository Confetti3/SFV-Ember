#pragma once

// The rules SFV Ember's rollback lays over the game's own snapshot, and their
// id (EMBER_LIBRARY.md section 42).
//
// A battle's state is every region of the game's rollback manager, which the
// verified exe (BuildCheck) gives every copy alike, plus what this mod
// decides itself: which runs of those regions are hashed or masked
// (sfv::manager::kRunRules), the state the game's snapshot leaves out and
// which of its words the peers do not compare (AddStateOutsideTheGameSnapshot
// below), and the flow record (SfvFlow's FlowRecord). Two builds of the mod
// that decide differently save, restore or compare different state and
// desync at their first checkpoint, so a room's build carries the id of
// these rules (ember/SfvRoomBuild) and such builds never meet.

#include "core/StateRegions.hxx"

#include <stdint.h>

#include <string>

namespace sfve::mod {

// Where one battle keeps the state its snapshot leaves out: the rule unit,
// the match data, the battle frame, the input unit, the exe's base and each
// fighter's parts that sfv::kPartRunsOutsideSnapshot names, by its row (0:
// the fighter has no such part).
constexpr size_t kMaxPartRuns = 4;
struct OutsideState {
	uintptr_t ruleUnit = 0;
	uintptr_t battleData = 0;
	uintptr_t battleFrame = 0;
	uintptr_t inputUnit = 0;
	uintptr_t base = 0;
	uintptr_t fighterParts[2][kMaxPartRuns] = {};
};

// Adds what a battle tick changes that the game's snapshot leaves out: its
// netcode manages the counters itself, its own rollback never crossed a
// round's end, which is where most of the rest changes, and some parts'
// registrations stop short of state a move sets
// (sfv::kPartRunsOutsideSnapshot). The counters' per-process
// values (the updates each process ran before binding, a clock reading) are
// left out of the hash peers compare.
void AddStateOutsideTheGameSnapshot(StateLayout& layout, const OutsideState& where);

// The rules' id, 16 hex digits: a hash of kRunRules; of the regions and peer
// masks AddStateOutsideTheGameSnapshot adds, laid out at fixed made-up
// addresses (their offsets, sizes, flags and masks); and of the flow
// record's rules (its size, sfv::flowstate's base and every class's size,
// own fields and leftover words); and of the two rules no table shows, the
// address rule's version (kAddressRuleVersion) and the CRT math of the modes
// that roll back (kRollbackCrtMath). The same for every copy of one build;
// any change to a table or rule that decides what is saved, restored,
// computed or compared changes it, and a new one must feed it too.
const std::string& LayoutRulesId();

} // namespace sfve::mod
