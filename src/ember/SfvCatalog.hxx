#pragma once

// SFV's game catalog for Ember (the library's common/GameCatalog.hxx): the
// fighters and stages the game's own select screens list (GameContent's
// ReadContentLists), numbered in that order, SFV's rules for a pick and a
// stage, and the battle a room's match launches (VersusLaunch).
//
// The catalog is filled at run time, once, on the game thread, before Ember's
// runtime starts: the host calls FillCatalog(mod::ReadContentLists()) once the
// game's content tables are loaded. Until then it has no fighters
// (ember::Usable is false) and every pick and stage is refused. Fighter and
// stage ids are positions in this PC's lists, so the build a room checks
// (MatchBuildId) carries a fingerprint of them: copies whose installed
// content differs fail that check instead of disagreeing on fighter N.
//
// What SFV has no use for: Ultras (one choice, 0), editions (a room's
// edition rule is ignored), personal actions and win quotes (SF4's; the wire
// carries SF4's values for them). A handicap is refused: SFV's launch has none.

#include "mod/GameContent.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/FighterCatalog.hxx"

#include <cstdint>
#include <optional>
#include <string>

namespace sfve::adapter {

// Costume and colour numbers a room carries: the two digits a player's setup
// takes (VersusLaunch's ParseSetup). Whether the game has a given one is its
// own check as the battle launches (sfv::kCostumeValid, sfv::kColourValid).
constexpr int kVariantLimit = 100;

// Fills the catalog from the game's lists, and the build id from them. False,
// and nothing changed, when either list is empty, the roster is longer than
// ember::MaxFighters, or the catalog already holds other lists; true again
// for the same lists.
bool FillCatalog(const mod::ContentLists& lists);

// The build a room's members must share (GameState::BuildId): the game's exe
// build (mod::kMatchBuild), "/", and the ContentFingerprint of the filled
// lists; kMatchBuild alone before FillCatalog.
const std::string& MatchBuildId();

// 16 hex digits of a hash of both lists, their codes in order: equal lists
// give equal fingerprints, and any change of a code, its place or its list
// another.
std::string ContentFingerprint(const mod::ContentLists& lists);

// The game's code of a fighter or a stage ("RYU", "S01"); empty for an id
// outside the lists.
std::string FighterCode(std::int64_t fighter);
std::string StageCode(std::int64_t stage);

// A room's match, as SFV can launch it.
struct RoomMatch {
	sf4e::selection::Pick picks[2]; // P1, P2
	std::int64_t stage = -1;
	int rounds = 0;       // the room's round count
	int roundSeconds = 0; // the room's round time
};

// The battle to launch for a room's match: both picks and the stage by their
// codes, and the room's rounds and time. Empty when the catalog refuses a
// pick or the stage, or SFV cannot play the rounds or the time
// (VersusLaunch's PlayableRounds and RoundClockFrames). The match's end is
// not part of it (MatchEnd's choice).
std::optional<mod::MatchPicks> LaunchPicks(const RoomMatch& match);

} // namespace sfve::adapter
