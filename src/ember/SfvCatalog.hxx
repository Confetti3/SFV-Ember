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
// this copy's player may not pick (GameContent's `owned`, for a costume in a
// colour together, under its kContentOwnership switch: by default the
// game's answer, from the account's entitlement cache; EMBER_LIBRARY.md
// section 41) while Ember's ownership gate is on, and a
// fighter with no costume and colour left to ready is locked too: each
// player may ready only what their own copy lets them pick, and the
// opponent's pick plays as picked, whatever this copy owns (LaunchPicks
// checks the spec, and only that this copy has the content installed).
// EMBER_LIBRARY.md section 23.
//
// Stages are the spec's universe (GameContent's StageContent), the same on
// every install, and a stage id is its place there. Which of them this copy
// has installed is per copy (StageInstalled), never part of the build id:
// two copies that lack different stages still meet, the room carries what
// each lacks (MissingStages, the library's GameState::MissingStages), and
// Random rolls only over stages both seated players have. Which stages this
// copy's player may choose is per copy too (StageOffered: installed and,
// under kContentOwnership's default, granted by the account's entitlement
// cache): their stage row, their stored stage and the Random their copy
// rolls take only those, while a stage the room's P1 chose plays on any copy
// that has it installed. The stage row shows a stage only the opponent
// lacks, unable to be chosen (StageOptions). EMBER_LIBRARY.md sections 38,
// 41 and 43.
//
// A pick's V-Skill and V-Trigger are its loadout (SfvPickSpace's Loadout),
// which travels in the shared pick's Ultra slot as one of four numbers
// (ultraChoices): the library sees an Ultra numbered below that, and SFV's
// codec (picks::LoadoutIn, LoadoutOf, SetLoadout) alone gives it meaning. A fighter has
// one or two of each, as the game's name assets list them (GameContent), and
// a pick holds only parts its fighter has. Shared screens name the loadout as
// one value (SfvLoadoutText); SFV's pick screen has a row for each part.
// EMBER_LIBRARY.md section 40.
//
// What SFV has no use for: editions (a room's edition rule is ignored),
// personal actions and win quotes (SF4's; the wire carries SF4's values for
// them). A handicap is refused: SFV's launch has none.
// The table rules it offers are the room's that SFV plays: 1, 3, 5 or 7
// rounds of 30, 60, 99 or 300 seconds (VersusLaunch's PlayableRounds and
// RoundClockFrames), filled with the lists. A table's Instant rematch plays:
// Ember's post-match prompt over the WIN, answered by both fighters in the
// session's held frames (RoomBattle, EMBER_LIBRARY.md section 19).

#include "ember/SfvPickSpace.hxx"
#include "mod/GameContent.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <cstdint>
#include <functional>
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
// already holds another pick space, or holds it with the gate the other way
// (a filled catalog keeps its gate); true again for the same one with the
// same gate. Game thread,
// before Ember's runtime starts: once SealCatalog has run, it logs and
// refuses, because the runtime's threads read the catalog without a lock.
bool FillCatalog(const mod::ContentLists& lists, bool ownershipGate = true);

// Called once as Ember's runtime starts: the catalog is final from here on.
void SealCatalog();

// The build a room's members must share (GameState::BuildId): the game's exe
// build (kMatchBuild), "/", the ContentFingerprint of the filled lists, "/",
// and the rollback's layout id (SfvRoomBuild, mod::LayoutRulesId);
// kMatchBuild alone before FillCatalog.
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
// outside the lists (Random's kRandomStage among them).
std::string FighterCode(std::int64_t fighter);
std::string StageCode(std::int64_t stage);
// A stage as the shell names it: the game's own name for it, or its code
// where the game gave none (and empty for an id outside the list).
std::string StageName(std::int64_t stage);
// How many stages the universe has (0 before FillCatalog); ids run from 0.
int StageCount();
// This copy has the stage's package: what a stage needs to play here. A
// launch (LaunchPicks) and a replay's playback ask it, whatever the room
// says or this copy's account grants; false for an id outside the universe.
bool StageInstalled(std::int64_t stage);
// This copy's player may choose the stage: installed here and, while Ember's
// ownership gate is on, owned under GameContent's kContentOwnership (by
// default, granted by the account's entitlement cache, as SFV's own stage
// lists offer it). The one rule for this player's own stage offers: the
// stage row (StageOptions), the fitting of their stored stage (SfvPick) and
// the Random their copy rolls (the catalog's resolveStage,
// OfflineStageCode). False for an id outside the universe.
bool StageOffered(std::int64_t stage);
// The universe's stages this copy has not installed, by id, ascending: what
// the room hears this copy lacks (SfvGame's GameState::MissingStages). Fixed
// once the catalog is filled. Install-based only: a stage this player is not
// offered still plays here when the room's P1 chose it.
std::vector<std::int64_t> MissingStages();
// What the pick screen's stage row lists: Random (picks::kRandomStage)
// first, then the universe's stages in its order that this player is offered
// (StageOffered: no other can ever be theirs to pick, so it is not listed). A
// stage in `unavailable` that they are offered is one the opponent lacks:
// listed, as OpponentLacks, so the player sees it and why (the runtime's
// runtime.ready.stage_not_shared), but never chosen. `unavailable` is the
// runtime's unavailableStages: this copy's missing stages, and at a table the
// opponent's as well; the two are told apart here alone. EMBER_LIBRARY.md
// sections 41 and 43.
enum class StageOffer { Playable, OpponentLacks };
struct StageOption {
	int id = 0;
	StageOffer offer = StageOffer::Playable;
	bool operator==(const StageOption&) const = default;
};
std::vector<StageOption> StageOptions(const std::vector<std::int64_t>& unavailable);
// The options that can be chosen, in order: what Left and Right step through
// and what a choice must be. Random and the Playable stages, the stages the
// catalog's resolveStage rolls Random over.
std::vector<int> PlayableIds(const std::vector<StageOption>& options);
// The stage code a launch that is not a room's (Training) plays the stage
// choice on: the stage, or for Random one this player is offered, rolled by
// the catalog's own resolveStage with this copy's missing stages as the
// unavailable ones, as a room's Random is. A launch never sees Random
// itself: with no stage offered the code is empty, and the launch leaves the
// stage as it is.
std::string OfflineStageCode(int stage);

// The fighter's costume numbers in the game's order, and a costume's colour
// numbers. Availability is of a whole look, a costume in one colour: with
// pickableOnly, only the looks this copy's player can ready (the costume
// installed here and, with the gate on, the game letting them pick both the
// costume and the colour), so a costume is listed only when it has a colour
// they can ready, and a colour only when it makes such a look. The pick
// screen offers these, and their own pick is fitted to them (FitOwnPick).
// Empty for a fighter or costume the catalog does not have.
std::vector<int> CostumeIds(std::int64_t fighter, bool pickableOnly);
std::vector<int> ColourIds(std::int64_t fighter, int costume, bool pickableOnly = false);
// With the gate on, the game does not let this copy's player pick the
// fighter (or any look of it), any look of the costume (a costume theirs in
// no colour of theirs is not theirs to wear), or that colour of the costume
// (the select screen's NOT OWNED); false for anything the catalog does not
// have, and always false with the gate off.
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
// The pick fitted to the spec alone (picks::FitPick, the loadout among it),
// as every copy fits any pick: what the catalog's normalizePick does with no
// availability. The pick screen previews a costume this player cannot ready
// so.
void FitToSpec(sf4e::selection::Pick& pick);
// The same fitting kept to the pick's own fighter: the pick fitted to the
// spec (FitToSpec), then, for a fighter this player may play, to a look they
// can ready as FitOwnPick would. For a fighter they may not play, the spec's
// fitting alone. The pick screen's preview of a fighter or costume
// (SfvSelectionScreen's Previewed) is so what choosing it stores.
void FitLookWithin(sf4e::selection::Pick& pick);
// A costume as the game names it, in its language (its number where the
// game gave no name), and a colour as the game's costume menu labels it
// ("01", "EX01").
std::string CostumeName(std::int64_t fighter, int costume);
std::string ColourName(int colour);

// How many choices the fighter has of a loadout part (1 or 2; 0 for a
// fighter the catalog does not have), the game's name for choice `number`
// (empty where the game gave none), and the fighter's loadouts by
// picks::LoadoutIndex, in that order (what the table page steps through).
int PartChoices(std::int64_t fighter, picks::LoadoutPart part);
std::string MoveName(std::int64_t fighter, picks::LoadoutPart part, int number);
std::vector<int> LoadoutIndices(std::int64_t fighter);

// The pick as a launch names it (VersusLaunch's FighterPick): the fighter's
// code, the costume and colour numbers, the loadout, and the voice the pick
// carries (picks::VoiceIn), or, where it carries none, this copy's options
// at the launch (mod::kVoiceOfOptions: Training's own picks carry none). The
// one place a pick becomes a launch's, for a room's match, Training and its
// dummy.
mod::FighterPick FighterPickOf(const sf4e::selection::Pick& pick);

// This copy's player's voice language for each fighter, as the game's
// options give it (GameContent's CharaVoiceLanguage, which `voiceOf` reads
// by the fighter's code): RefreshOwnVoices reads them all, on the game
// thread (EmberHost: once the catalog is filled, then now and then at the
// main menu, so a change in the game's options reaches the next Ready).
// OwnVoice is any thread's: the last read's voice for the fighter, or the
// game's default, English, before any read or where the read gave none.
// Ready carries it for the pick's fighter (SfvPick's AddPickTo).
void RefreshOwnVoices(const std::function<std::optional<int>(const std::string& code)>& voiceOf);
int OwnVoice(std::int64_t fighter);

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
	// The room allows tiny fighters (room::Snapshot::tinyFighters, a private
	// room's rule): only then is a pick's Tiny drawn, so a public room's
	// battle is always normal size whatever a pick carries.
	bool tinyAllowed = false;
	// The room's Skip winner scenes rule (room::Snapshot::skipScenes, on by
	// default, any room's): the fighters' copies ask for the decided match's
	// scenes to be skipped on their own (mod::intro_skip::Request).
	bool skipScenes = true;
};

// Why this copy cannot launch a room's match: the reason the runtime words
// for the player (SfvGame's Battle::Enter returns it), and what the log says.
struct MatchRefusal {
	ember::game::EntryRefusal reason = ember::game::EntryRefusal::UnsupportedMatch;
	std::string log;
};

// A room's match as this copy launches it: the battle, or why not.
struct MatchLaunch {
	std::optional<mod::MatchPicks> picks; // none when refused
	MatchRefusal refusal;                 // when refused
};

// The battle to launch for a room's match: both picks and the stage by their
// codes and numbers, and the room's rounds and time. Refused when the catalog
// refuses a pick (UnsupportedMatch) or the stage (UnsupportedStage), this
// copy lacks a fighter or costume (UnsupportedMatch) or the stage
// (StageNotInstalled), since a battle would load nothing sensible, or SFV
// cannot play the rounds or the time (VersusLaunch's PlayableRounds and
// RoundClockFrames; UnsupportedMatch): this is the backstop that keeps a copy
// from launching content it does not have. The match's end is not part of it
// (MatchEnd's choice). Each player's voice is their own (picks::VoiceOf:
// what their copy sent, or English for a copy that sent none), so every copy
// launches the same.
MatchLaunch LaunchPicks(const RoomMatch& match);

} // namespace sfve::adapter
