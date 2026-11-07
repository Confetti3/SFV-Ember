#pragma once

// SFV's game catalog for Ember (the library's common/GameCatalog.hxx): the
// fighters, their costumes and colours, and the stages the game's own spec
// data lists (GameContent's ReadContentLists), SFV's rules for a pick and a
// stage, and the battle a room's match launches (VersusLaunch).
//
// The catalog is filled at run time, once, on the game thread, before Ember's
// runtime starts: the host calls FillCatalog(mod::ReadContentLists()) once the
// game's content tables are loaded. Until then it has no fighters
// (ember::Usable is false) and every pick and stage is refused.
//
// A pick carries the game's own numbers: fighter and stage ids are positions
// in the lists, a costume is the game's costume number (its place in the
// fighter's CostumeDatas; the spec may leave numbers out) and a colour the
// game's colour number for that costume (0-99, then 100 and up for EX
// colours). Which of them exist is the spec's to say, so every copy of the
// same game data has the same pick space, and the build a room checks
// (MatchBuildId) carries a fingerprint of it: copies whose spec differs fail
// that check instead of disagreeing on fighter N or on what a costume is.
// What this copy's install lacks is per copy: such a fighter is locked and
// such a costume cannot be readied (FighterLocked, AvailablePick). So is what
// the game does not let this copy's player pick (GameContent's `owned`)
// while Ember's ownership gate is on: each player may ready only what their
// own copy lets them pick, and the opponent's pick plays as picked, whatever
// this copy owns (LaunchPicks checks the spec alone). EMBER_LIBRARY.md
// section 23.
//
// What SFV has no use for: Ultras (one choice, 0), editions (a room's
// edition rule is ignored), personal actions and win quotes (SF4's; the wire
// carries SF4's values for them). A handicap is refused: SFV's launch has none.
// The table rules it offers are the room's that SFV plays: 1, 3, 5 or 7
// rounds of 30, 60, 99 or 300 seconds (VersusLaunch's PlayableRounds and
// RoundClockFrames), filled with the lists. A table's Instant rematch plays:
// Ember's post-match prompt over the WIN, answered by both fighters in the
// session's held frames (RoomBattle, EMBER_LIBRARY.md section 19).

#include "ember/SfvPickSpace.hxx"
#include "mod/GameContent.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/SelectionPick.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

// The game build a room's matches are played on (SfvPickSpace). MatchBuildId
// adds the content.
using picks::kMatchBuild;

// Fills the catalog from the game's lists, and the build id from them, with
// Ember's ownership gate on or (development only) off. False,
// and nothing changed, when the lists make no pick space (a list empty, a
// roster longer than ember::MaxFighters, a fighter with no costume or a
// costume with no colour, a number past the wire's byte) or the catalog
// already holds another pick space; true again for the same one. Game thread,
// before Ember's runtime starts: once SealCatalog has run, it logs and
// refuses, because the runtime's threads read the catalog without a lock.
bool FillCatalog(const mod::ContentLists& lists, bool ownershipGate = true);

// Called once as Ember's runtime starts: the catalog is final from here on.
void SealCatalog();

// The build a room's members must share (GameState::BuildId): the game's exe
// build (kMatchBuild), "/", and the ContentFingerprint of the filled
// lists; kMatchBuild alone before FillCatalog.
const std::string& MatchBuildId();

// 16 hex digits of a hash of the pick space: the fighters' codes, each
// fighter's costume numbers and each costume's colour numbers, and the
// stages' codes, all in order. Equal pick spaces give equal fingerprints;
// names and what this copy has installed never enter it.
std::string ContentFingerprint(const mod::ContentLists& lists);

// The lists' pick space: what every copy of the same game data shares, and
// what SFV's room host judges picks by (SfvPickSpace).
picks::PickSpace PickSpaceOf(const mod::ContentLists& lists);
// The filled catalog's pick space and build as the room host's data
// (picks::ToJson); empty before FillCatalog. The pipe's `catalog-export`
// writes it (EMBER_LIBRARY.md section 24).
std::string PickSpaceJson();

// The game's code of a fighter or a stage ("RYU", "S01"); empty for an id
// outside the lists.
std::string FighterCode(std::int64_t fighter);
std::string StageCode(std::int64_t stage);
// A stage as the shell names it: the game's own name for it, or its code
// where the game gave none (and empty for an id outside the list).
std::string StageName(std::int64_t stage);
// How many stages the catalog lists (0 before FillCatalog); ids run from 0.
int StageCount();

// The fighter's costume numbers in the game's order, and a costume's colour
// numbers; with pickableOnly, only those this copy's player can ready (a
// costume installed here and, with the gate on, a costume or colour the game
// lets them pick). Empty for a fighter or costume the catalog does not have.
std::vector<int> CostumeIds(std::int64_t fighter, bool pickableOnly);
std::vector<int> ColourIds(std::int64_t fighter, int costume, bool pickableOnly = false);
// With the gate on, the game does not let this copy's player pick the
// fighter, the costume, or that colour of the costume (the select screen's
// NOT OWNED); false for anything the catalog does not have, and always false
// with the gate off.
bool FighterNotOwned(std::int64_t fighter);
bool CostumeNotOwned(std::int64_t fighter, int costume);
bool ColourNotOwned(std::int64_t fighter, int costume, int colour);
// What SfvGame reports as the fighter's Availability (the library's
// GameState::FighterAvailability): `ready` when this copy's player may play
// the fighter, and every costume and colour bit set then. SFV's costume and
// colour numbers do not follow that layout (SF4's), so its catalog rules
// read their own table for costumes and colours, and nothing reads those
// bits.
sf4e::selection::Availability OwnedAvailability(std::int64_t fighter);
// This copy's player's own pick fitted to the catalog and to what they can
// ready (NormalizePick with their availability): the pick store's rule, so
// a stored pick they cannot ready falls back to one they can.
void FitOwnPick(sf4e::selection::Pick& pick);
// A costume as the game names it, in its language (its number where the
// game gave no name), and a colour as the game's costume menu labels it
// ("01", "EX01").
std::string CostumeName(std::int64_t fighter, int costume);
std::string ColourName(int colour);

// The other way, from a code as a launch names it (L"RYU", VersusLaunch's
// FighterPick): its id, empty for one not listed.
std::optional<int> FighterId(const std::wstring& code);
std::optional<int> StageId(const std::wstring& code);

// A room's match, as SFV can launch it.
struct RoomMatch {
	sf4e::selection::Pick picks[2]; // P1, P2
	std::int64_t stage = -1;
	int rounds = 0;       // the room's round count
	int roundSeconds = 0; // the room's round time
	bool practice = false; // a Practice room's (the lobby's trainingMode)
};

// The battle to launch for a room's match: both picks and the stage by their
// codes and numbers, and the room's rounds and time. Empty when the catalog
// refuses a pick or the stage, or SFV cannot play the rounds or the time
// (VersusLaunch's PlayableRounds and RoundClockFrames). The match's end is
// not part of it (MatchEnd's choice).
std::optional<mod::MatchPicks> LaunchPicks(const RoomMatch& match);

} // namespace sfve::adapter
