#pragma once

// SFV's pick space: which fighters, costumes, colours and stages exist, as
// the game's spec data lists them, and how many V-Skills and V-Triggers each
// fighter has, as the game's name assets list them (mod/GameContent's pick
// space, without names, installs or ownership), and SFV's rules over it. A
// pick's V-Skill and V-Trigger are its Loadout, which travels in the shared
// pick's `ultra` slot; LoadoutOf and SetLoadout are the only code that reads
// or writes that slot (EMBER_LIBRARY.md section 40). Every copy of the
// same game data has the same pick space, and the build a room checks carries
// its fingerprint, so two programs that hold it judge a pick alike: the game
// (SfvCatalog, which adds this copy's installs and ownership) and SFV's room
// host (roomhost/SfvHostCatalog, which holds it as data exported from the
// game). EMBER_LIBRARY.md sections 15 and 24.
//
// No game code: the room host builds it on Linux with g++ alone (C++17).

#include "common/SelectionPick.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sfve::picks {

// The game build a room's matches are played on: the exe's build (BuildCheck's
// verified one) and the mod's match version. A room's build id adds the pick
// space's Fingerprint. 0.4: stage ids are positions in the spec's stage
// universe rather than in this copy's installed stages, and a room's members
// carry the stages their copies lack (EMBER_LIBRARY.md section 38). 0.5:
// every launch writes both players' V-Skill and V-Trigger, where an earlier
// copy launched V-Skill I and V-Trigger I whatever the room said (section
// 40). No earlier copy may meet one of these.
constexpr const char* kMatchBuild = "sfv-ember/13173604/0.5";

// The stage choice that stands for Random (GameCatalog::randomStage): the
// library's selection::RandomStageId, never a stage on the wire. P1's copy
// turns it into a stage both players have before it is sent.
constexpr int kRandomStage = 255;

// A fighter's V-Skill and V-Trigger choices, numbered as the game numbers
// them in its per-fighter record (VersusLaunch's CharPkg, +56 and +48): I is
// 0 and II is 1. The game holds no third of either (its select screen keeps
// V-Trigger below 2, rva 0x59300D).
enum class VSkill : std::uint8_t { I, II };
enum class VTrigger : std::uint8_t { I, II };
constexpr int kPartChoices = 2;

// What a player takes into the battle besides their look.
struct Loadout {
	VSkill vSkill = VSkill::I;
	VTrigger vTrigger = VTrigger::I;
};
constexpr bool operator==(Loadout a, Loadout b) { return a.vSkill == b.vSkill && a.vTrigger == b.vTrigger; }
constexpr bool operator!=(Loadout a, Loadout b) { return !(a == b); }

// The two parts, for code that treats them alike (a pick screen row each, a
// fitting each), in SFV's own order: V-Skill, then V-Trigger.
enum class LoadoutPart { VSkill, VTrigger };
constexpr LoadoutPart kLoadoutParts[] = {LoadoutPart::VSkill, LoadoutPart::VTrigger};

// A part's number in the loadout (0 or 1), and the loadout with that part
// set to `number`; none for a number that is no choice.
int PartNumber(Loadout loadout, LoadoutPart part);
std::optional<Loadout> WithPart(Loadout loadout, LoadoutPart part, int number);
// The loadout of two part numbers (the launch's and a replay's form); none
// when either is no choice.
std::optional<Loadout> LoadoutOfNumbers(int vSkill, int vTrigger);

// The loadout's place in the shared pick's `ultra` slot, 0 to 3 (V-Skill
// times two, plus V-Trigger): the catalog's ultraChoices is kLoadoutChoices.
// LoadoutAt is the other way, and none outside 0 to 3.
constexpr int kLoadoutChoices = kPartChoices * kPartChoices;
int LoadoutIndex(Loadout loadout);
std::optional<Loadout> LoadoutAt(int index);
// The one reader and the one writer of the pick's `ultra` slot. LoadoutIn
// is none for a slot that holds no loadout (ValidPick's question);
// LoadoutOf reads such a slot as V-Skill I and V-Trigger I, which is what
// FitPick makes of it.
std::optional<Loadout> LoadoutIn(const sf4e::selection::Pick& pick);
Loadout LoadoutOf(const sf4e::selection::Pick& pick);
void SetLoadout(sf4e::selection::Pick& pick, Loadout loadout);
// The two picks hold the same fighter, look, Ultra slot and handicap: every
// part of a pick SFV's fitting can change.
bool SameFitting(const sf4e::selection::Pick& a, const sf4e::selection::Pick& b);
// "vskill II vtrigger I", for the logs and the pipe.
std::string LoadoutText(Loadout loadout);

struct Costume {
	int id = 0;               // the game's costume number
	std::vector<int> colours; // the game's colour numbers for it, in its order
};

struct Fighter {
	std::string code;               // "RYU"
	std::vector<Costume> costumes;  // in the game's order
	// How many V-Skills and V-Triggers the fighter has, 1 or kPartChoices:
	// the game's name assets' count, which GameContent brings into that range.
	int vSkills = 1;
	int vTriggers = 1;
};

// The fighter's number of choices for the part.
int PartChoices(const Fighter& fighter, LoadoutPart part);
// Every part of the loadout is one the fighter has.
bool HasLoadout(const Fighter& fighter, Loadout loadout);
// The loadout with each part the fighter lacks made I, part by part.
Loadout FitLoadout(const Fighter& fighter, Loadout loadout);
// The fighter's loadouts by LoadoutIndex, in that order.
std::vector<int> LoadoutIndices(const Fighter& fighter);

struct PickSpace {
	std::vector<Fighter> fighters;   // ids are positions here
	// The spec's stage universe, as codes, installed on a copy or not; ids
	// are positions here.
	std::vector<std::string> stages;
};

// The table rules SFV plays: the room's round counts and times that
// VersusLaunch's PlayableRounds and RoomClockFrames accept, in the room's
// order (9999 s is the game's no time limit, and what makes a Practice room
// playable: ember::PlayableRoundTime(room::PracticeRoundTime)).
// sfve_ember_tests checks them against those two.
constexpr std::uint8_t kRoundCounts[] = {1, 3, 5, 7};
constexpr std::uint16_t kRoundTimes[] = {30, 60, 99, 300, 9999};

// Why the lists make no pick space, or empty when they do: no list is empty,
// the roster fits a room (ember::MaxFighters), every fighter has a costume and
// every costume a colour (the fitting's first of each), every number fits
// the wire's byte (SelectionPick's ToNative), and every fighter has 1 or
// kPartChoices V-Skills and V-Triggers.
std::string Problem(const PickSpace& space);

// 16 hex digits of a hash of the pick space: the fighters' codes, each
// fighter's costume numbers and each costume's colour numbers, each
// fighter's V-Skill and V-Trigger counts, and the stages' codes, all in
// order. Equal pick spaces give equal fingerprints.
std::string Fingerprint(const PickSpace& space);

// kMatchBuild, "/", and the Fingerprint: the build id a room's members share.
std::string BuildId(const PickSpace& space);

// For the log only, never the build id: "picks <hex>, stages <hex>", a hash
// of the fighters with their costumes and colours, and one of the stages,
// made as Fingerprint makes the whole. Two copies' logs then show which part
// of their pick spaces differs.
std::string PartFingerprints(const PickSpace& space);

const Fighter* FighterAt(const PickSpace& space, std::int64_t fighter);
const Costume* CostumeAt(const PickSpace& space, std::int64_t fighter, int costume);
bool HasColour(const Costume& costume, int colour);

// A costume of the fighter, in one of that costume's colours, with a loadout
// the fighter has (HasLoadout) and no handicap (SFV's launch has none).
bool ValidPick(const PickSpace& space, const sf4e::selection::Pick& pick);
bool ValidStage(const PickSpace& space, std::int64_t stage);

// Each part of the pick SFV refuses becomes the game's first of its kind, in
// this order so each fits the one before: fighter 0, the fighter's first
// costume, that costume's first colour, then each part of the loadout the
// fighter lacks becomes I (FitLoadout); no handicap. Only the pick space
// decides, so every copy fits a pick alike. True when it changed.
bool FitPick(const PickSpace& space, sf4e::selection::Pick& pick);

// One past the highest costume and colour number of any fighter (the numbers
// a room shows: GameCatalog's costumeLimit and colorLimit).
int CostumeLimit(const PickSpace& space);
int ColourLimit(const PickSpace& space);
// One past the fighter's highest costume number (GameCatalog's costumeCount):
// SFV's numbers have gaps, and every costume the spec has reads as known.
int CostumeCount(const PickSpace& space, int fighter);

// The pick space as the room host's data: {"build", "fighters": [{"code",
// "costumes": [{"id", "colours"}], "v_skills", "v_triggers"}], "stages"},
// with the build it was exported from.
std::string ToJson(const PickSpace& space, const std::string& build);
// The other way. False, with the reason in `error`, for text that is not
// such a document.
bool FromJson(const std::string& text, PickSpace& space, std::string& build, std::string& error);

} // namespace sfve::picks
