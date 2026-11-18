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
// the match data, the battle frame, the input unit, the exe's base, the
// round's end wait unit and each
// fighter unit's parts that sfv::kPartRunsOutsideSnapshot names, by the
// unit's place among the battle's CharaUnits (partners after the two
// fighters, sfv::unit::kMostCharaUnits) and the row (0: the unit has no such
// part).
constexpr size_t kMaxPartRuns = 12;
constexpr size_t kMaxFighterUnits = 8; // sfv::unit::kMostCharaUnits (checked in SfvLayoutRules.cxx)
struct OutsideState {
	uintptr_t ruleUnit = 0;
	uintptr_t battleData = 0;
	uintptr_t battleFrame = 0;
	uintptr_t inputUnit = 0;
	uintptr_t base = 0;
	uintptr_t koWait = 0; // the round's end wait unit (sfv::kowait); 0 for a battle without one
	uintptr_t fighterParts[kMaxFighterUnits][kMaxPartRuns] = {};
};

// Adds what a battle tick changes that the game's snapshot leaves out: its
// netcode manages the counters itself, its own rollback never crossed a
// round's end, which is where most of the rest changes, and some parts'
// registrations stop short of state a move sets
// (sfv::kPartRunsOutsideSnapshot). The counters' per-process
// values (the updates each process ran before binding, a clock reading) are
// left out of the hash peers compare.
void AddStateOutsideTheGameSnapshot(StateLayout& layout, const OutsideState& where);

// The version of the rollback code rules no table shows: code every copy of
// a match must run alike, whose change makes two builds hash or compute
// different state (some of it after frame 0, where the start agreement does
// not look) yet changes no table the id hashes. Move it with any change to
// that code. The publish pipeline's plan (scripts/publish/SfvPublish.ps1,
// Assert-PubCodeRulesMoved) refuses a release that changed one of these
// files since the last release while the version stayed; it reads them from
// the lines "//   <file>: <the code>" below.
//   src/mod/GameHooks.cxx: h_GameAlloc zeroes what the battle code allocates
//   src/mod/ProjectileRelease.cxx: h_Releasable frees a projectile once its visual part died or fades
//   src/mod/IntroSkip.hxx: ScenesStepFor, the scenes' cuts and the winner's and draw's demo guard (kDemoGuardTicks)
//   src/mod/ScenesSkip.cxx: the KO stop and the phase's ticks the cuts are decided by
//   src/mod/SfvAdapter.cxx: ReseedRandom's place formula and fighter draw reset; OnInputRecord's input while loading
//   src/mod/GameInput.hxx: RecordedInput, what a pad records while the battle loads
//   src/mod/SocketReads.cxx: Policy and AnchorPosition, the rules the anchor text names the sites and offsets of
// 1 from 0.1.1 (the demo guard).
constexpr int kRollbackCodeRules = 1;

// The rules in words, which the id hashes: kRunRules; the owners those rules
// match by name (RegionOwnersText: RegionNames' tables); the regions and peer
// masks AddStateOutsideTheGameSnapshot adds, laid out at fixed made-up
// addresses (their offsets, sizes, flags and masks); the flow record's rules
// (its size, sfv::flowstate's base and every class's size, own fields and
// leftover words); and the rules no table shows: the address rule's version
// (kAddressRuleVersion), the start agreement's (start_state::kVersion), the
// rollback code rules' (kRollbackCodeRules), the CRT math of the modes that
// roll back (kRollbackCrtMath), where their battles run the tick's tasks
// (kRollbackTickTasks, or the placement -sfve_tasks gave a development build)
// and whether the fight socket reads that may feed fight state are anchored
// to the fight's position, with the anchor rules' call sites and offsets
// (ChooseSocketAnchor, socket_reads::AnchorRulesText). First made
// once the config is loaded.
const std::string& LayoutRulesText();
// The rules' id, 16 hex digits: a hash of LayoutRulesText. The same for every
// copy of one build; any change to a table or rule that decides what is
// saved, restored, computed or compared changes it, and a new one must feed
// it too (its owning module's text, as RegionOwnersText and AnchorRulesText
// do).
const std::string& LayoutRulesId();

} // namespace sfve::mod
