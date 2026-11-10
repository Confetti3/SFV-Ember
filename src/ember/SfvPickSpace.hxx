#pragma once

// SFV's pick space: which fighters, costumes, colours and stages exist, as
// the game's spec data lists them, and how many V-Skills and V-Triggers each
// fighter has, as the game's name assets list them (mod/GameContent's pick
// space, without names, installs or ownership), and SFV's rules over it. A
// pick's V-Skill and V-Trigger are its Loadout, which travels in the shared
// pick's `ultra` slot; LoadoutIn, LoadoutOf and SetLoadout are the only code
// that reads or writes that slot (EMBER_LIBRARY.md section 40). Every copy of the
// same game data has the same pick space, and the build a room checks carries
// its fingerprint, so two programs that hold it judge a pick alike: the game
// (SfvCatalog, which adds this copy's installs and ownership) and SFV's room
// host (roomhost/SfvHostCatalog, which holds it as data exported from the
// game). EMBER_LIBRARY.md sections 15 and 24.
//
// No game code: the room host builds it on Linux with g++ alone (C++17).

#include "common/SelectionPick.hxx"
#include "common/StageCatalog.hxx"

#include <array>
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
// 40); and the pick space is every fighter, costume and colour the spec's
// rows hold, whatever the account's entitlements or the install (section
// 41). No earlier copy may meet one of these.
constexpr const char* kMatchBuild = "sfv-ember/13173604/0.5";

// The stage choice that stands for Random (GameCatalog::randomStage): the
// library's own selection::RandomStageId, never a stage on the wire. P1's
// copy turns it into a stage both players have before it is sent.
constexpr int kRandomStage = sf4e::selection::RandomStageId;

// The two parts of a loadout, in SFV's own order: V-Skill, then V-Trigger.
// Each indexes a Loadout's choices, so code that treats the parts alike (a
// pick screen row each, a fitting each) walks kLoadoutParts.
enum LoadoutPart : std::uint8_t { VSkill, VTrigger };
constexpr LoadoutPart kLoadoutParts[] = {LoadoutPart::VSkill, LoadoutPart::VTrigger};

// A part has at most two choices, numbered as the game numbers them in its
// per-fighter record (VersusLaunch's CharPkg, +56 and +48): I is 0 and II
// is 1. The game holds no third of either (its select screen keeps V-Trigger
// below 2, rva 0x59300D).
constexpr int kPartChoices = 2;

// What a player takes into the battle besides their look: the choice number
// of each part, by LoadoutPart.
struct Loadout {
	std::array<std::uint8_t, 2> choice{};
};
// Element by element: std::array's own comparison is not constexpr before
// C++20, and the room host builds this header as C++17.
constexpr bool operator==(Loadout a, Loadout b)
{
	return a.choice[VSkill] == b.choice[VSkill] && a.choice[VTrigger] == b.choice[VTrigger];
}
constexpr bool operator!=(Loadout a, Loadout b) { return !(a == b); }

// The loadout of two choice numbers (the launch's, the pipe's and a replay's
// form); none when either is no choice.
std::optional<Loadout> LoadoutOfNumbers(int vSkill, int vTrigger);
// "I" or "II", for choice number 0 or 1; empty for any other.
const char* Numeral(int number);

// The loadout's place in the shared pick's `ultra` slot, 0 to 3 (V-Skill
// times two, plus V-Trigger): the catalog's ultraChoices is kLoadoutChoices.
// LoadoutAt is the other way, and none outside 0 to 3.
constexpr int kLoadoutChoices = kPartChoices * kPartChoices;
int LoadoutIndex(Loadout loadout);
std::optional<Loadout> LoadoutAt(int index);
// The two readers and the one writer of the pick's `ultra` slot. LoadoutIn
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

// The player's voice language for the fighter they pick, as the game's own
// numbers have it (VersusLaunch's CharPkg +61, EKWVoiceLanguage): what their
// copy's options give for that fighter, so a room's match plays each
// fighter in its own player's language on both copies. It travels in the
// shared pick's `winQuote` slot (SF4's win quote, which SFV has no use for),
// as 1 + the voice: 0, the slot's default, is no voice, so a pick from a
// copy that sends none (0.1.0-beta.3 and earlier) reads as none.
// Ready's pick carries it (SfvPick's AddPickTo); nothing judges or fits it.
constexpr int kVoiceJapanese = 0;
constexpr int kVoiceEnglish = 1;
// The voice the pick carries; none for a slot that holds none.
std::optional<int> VoiceIn(const sf4e::selection::Pick& pick);
// VoiceIn, or the game's own default, English, for a pick that carries
// none: a room's match launches that on both copies.
int VoiceOf(const sf4e::selection::Pick& pick);
// Carries `voice` (kVoiceJapanese or kVoiceEnglish); any other number
// carries none.
void SetVoice(sf4e::selection::Pick& pick, int voice);

// The game's own costume codes (EMBER_LIBRARY.md section 55): the buttons a
// player holds as the battle loads, which the input unit keeps for its
// fighters (Urien's LP+MK+HP, his Street Fighter III look). A room's match
// carries each player's choice in the shared pick's `personalAction` slot
// (SF4's personal action, which SFV has no use for) as kLoadCodeMarker | the
// choice's index: the marker says the copy knows codes, so 0 (what 0.1.0-beta.4
// and earlier send) reads as none known, and both copies launch the codes only
// when both picks carry the marker. Ready's pick carries it (SfvPick's
// AddPickTo); nothing judges or fits it.
constexpr int kLoadCodeMarker = 0x80;
struct LoadCode {
	std::uint32_t buttons; // the game's input bits (mod/GameInput.hxx)
	const char* label;     // its buttons as players write them; none for no code
};
// Index 0 is no code. The bits are GameInput's: Up 0x001, LP 0x010, MP 0x020,
// LK 0x040, MK 0x080, HP 0x200 (SfvLoadCodeTests checks them).
inline constexpr LoadCode kLoadCodes[] = {
	{0, nullptr},
	{0x010 | 0x080 | 0x200, "LP+MK+HP"},
	{0x001 | 0x010 | 0x020 | 0x200 | 0x040, "Up+LP+MP+HP+LK"},
};
constexpr int kLoadCodeCount = (int)(sizeof(kLoadCodes) / sizeof(kLoadCodes[0]));
// The code's index the pick carries; none for a pick without the marker or
// with an index this copy does not know.
std::optional<int> LoadCodeIn(const sf4e::selection::Pick& pick);
// Carries code `index` with the marker; an index out of range carries code 0.
void SetLoadCode(sf4e::selection::Pick& pick, int index);
// The buttons of code `index`; 0 for one out of range.
std::uint32_t LoadCodeButtons(int index);

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
