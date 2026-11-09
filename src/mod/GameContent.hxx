#pragma once

// The fighters, their costumes and colours, and the stages the game offers,
// read from its own tables for the overlay's pickers and Ember's catalog.
// Codes are the game's ("RYU", "S01"); costume and colour numbers are the
// game's own (what a battle's setup takes, and what the native costume
// settings menu lists).
//
// Two kinds of fact, kept apart because two copies of the game must agree on
// the first and need not on the second:
// - The pick space: which fighters, costumes, colours and stages exist. It
//   comes from the game's spec data (DA_Spec) and fighter table
//   (DA_CharaEntry), never from what this copy has installed or what its
//   account's entitlement cache holds: every fighter, costume and colour with
//   a CharaSpec row (sfv::spec, found with sfv::kFindSpecFighter and the
//   others), and the spec's stages (sfv::kSpecStageIds). EMBER_LIBRARY.md
//   section 41.
// - This copy's own: whether its install has a fighter's, a costume's or a
//   stage's package (`installed`), what its account's entitlement cache
//   grants (`entitled`), what this copy's player may pick (`owned`: the
//   game's answer, sfv::kOwnedFighters and the rest, under kCostumeOwnership
//   for looks), and the names, in the game's language.
//
// Stage names are the stage's data asset's FText (sfv::kStageName); fighter
// names the fighter's PersonalData FText; costume names the spec's (where it
// has none, the game's own "Costume<n>"). EMBER_LIBRARY.md section 15.
//
// How many V-Skills and V-Triggers a fighter has, and their names, come from
// the game's name assets (sfv::choices), as its select and VS screens count
// and name them; the count is part of the pick space (section 40).

#include "mod/ModConfig.hxx"

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod {

// Which looks (a costume in one of its colours) this copy's player may pick.
// The opponent's look always plays as picked, whatever this copy's answer
// (EMBER_LIBRARY.md sections 23 and 41). Fighters keep the game's answer
// (sfv::kOwnedFighters) under either.
enum class CostumeOwnership {
	// The game's own answer (sfv::kOwnedCostumes, kOwnedColours), as its
	// menus offer looks: those the account's entitlement cache grants and
	// this copy has installed. On a copy whose cache lacks a costume, that
	// costume shows as not owned.
	Entitlements,
	// Every look of a costume this copy has installed, whatever the cache
	// holds. While the battle setup asset restricts the picks to its own list
	// (OwnershipSource::restricted), the game's answer still decides.
	Installed,
};

// The one switch for that rule, applied by ApplyCostumeOwnership as the
// content lists are read.
constexpr CostumeOwnership kCostumeOwnership = CostumeOwnership::Entitlements;

struct CostumeContent {
	int id = 0; // the game's costume number: its place in the fighter's CostumeDatas
	// The game's colour numbers for this costume, in the game's order: 0-99,
	// then 100 and up for the EX colours. Every colour with a CharaSpec row.
	std::vector<int> colours;
	std::string name; // UTF-8, display only
	// This copy has the costume's package (sfv::kCostumePackage, the test the
	// battle's own load makes).
	bool installed = false;
	// This copy's player may pick it, under kCostumeOwnership.
	bool owned = false;
	// The colours of `colours` this copy's player may pick, in the same order.
	std::vector<int> ownedColours;
	// The account's entitlement cache grants the costume's code
	// (sfv::spec::kEntitled), and the colours of `colours` whose codes it
	// grants, in the same order. Reported, never gated on directly.
	bool entitled = false;
	std::vector<int> entitledColours;
};

struct FighterContent {
	std::string code;
	std::vector<CostumeContent> costumes; // in the game's order
	std::string name;                     // UTF-8, display only; empty where the game has none
	bool installed = false;               // this copy has the fighter's package (sfv::kFighterInstalled)
	bool owned = false;                   // the game lets this copy's player pick the fighter (sfv::kOwnedFighters)
	// The fighter's V-Skills and V-Triggers, I first, by the game's names for
	// them (UTF-8, display only; empty where the game has none). How many
	// there are is part of the pick space; the names are not. Always one or
	// two (FitChoices); one unnamed until read.
	std::vector<std::string> vSkills{std::string()};
	std::vector<std::string> vTriggers{std::string()};
	// The account's entitlement cache grants the fighter's RefAvailableCode
	// (sfv::spec::kEntitled). Reported only.
	bool entitled = false;
};

// Where `owned` came from, for the log and the status.
struct OwnershipSource {
	// The battle setup asset was there and its lists were read. Lists are
	// only ever given with it read: without the asset ReadContentLists gives
	// none, so the catalog is not filled and Ember's runtime waits.
	bool read = false;
	bool restricted = false;    // the asset's +96: picks limited to its own list
	int restrictionEntries = 0; // that list's length
	bool filter = false;        // a filter object installed at the asset's +280
	bool loggedIn = false;      // the online identity's status (offline: false)
	// Content the command line marks unowned for this copy
	// (-sfve_unowned, ModConfig), applied on top of the game's answer.
	int markedUnowned = 0;
	// The rule that made the costumes' `owned` (ApplyCostumeOwnership).
	CostumeOwnership looks = CostumeOwnership::Entitlements;
};

// One stage of the spec's universe (sfv::kSpecStageIds): every copy of the
// build lists the same stages in the same order, whatever it has installed.
struct StageContent {
	std::string code;       // "S01"
	std::string name;       // UTF-8, display only; empty where the game has none
	bool installed = false; // in sfv::kSpecStages: this copy has the stage's package
	// The game lets this copy's player choose it (sfv::kOwnedStages); logged,
	// not gated (a room's stage is P1's choice and the battle jump runs no
	// check in a room).
	bool owned = false;
};

struct ContentLists {
	std::vector<FighterContent> fighters; // in the order of the game's select screen
	// The universe, in the spec's order: playable ones (not random, not those
	// without stage data), installed here or not.
	std::vector<StageContent> stages;
	OwnershipSource ownership;
};

// Game thread. Empty lists until the game's content tables, its battle
// setup asset and its V-Trigger and V-Skill name assets are loaded, until
// the spec's refresh has set the entitlement flags (sfv::kSpecFighters lists
// a fighter), and while the spec's stage universe is empty or this copy has
// none of its stages installed. Those assets are found, or loaded, with
// StaticLoadObject, so call it at the main menu (never in a battle). A name
// asset whose counts are past any the game could hold (sfv::choices'
// limits) reads as not there, logged once. This copy's installs and the
// names are read last, once everything else is there: each asks the game's
// package files. The costumes' `owned` is kCostumeOwnership's
// (ApplyCostumeOwnership).
ContentLists ReadContentLists();

// Makes each costume's `owned` and `ownedColours` the `policy`'s, from the
// game's answer they hold when called (ReadContentLists' ReadOwnership).
// Entitlements keeps the game's answer. Installed makes a costume owned
// exactly when it is installed, in every colour, unless the battle setup
// asset restricts the picks (`ownership.restricted`), when the game's answer
// stays. Fighters and stages are untouched. Records the policy in
// `ownership.looks`. No game code: the tests call it with either policy.
void ApplyCostumeOwnership(ContentLists& lists, CostumeOwnership policy);
const char* CostumeOwnershipName(CostumeOwnership policy);

// Whether a battle on this copy loads the costume `costume` of the fighter
// `code` names (compared as the game compares codes) as picked: the spec has
// a row for it, and this copy has its package (sfv::kCostumePackage), the
// one test the battle's load makes before it would silently put the costume
// back to 0. Whose entitlement it is does not enter: the opponent's pick
// plays as picked. Game thread, at the main menu.
enum class CostumeLoad { Loadable, NotInSpec, NoPackage, NoTables };
CostumeLoad CostumeLoadState(std::wstring_view code, int costume);
// The spec has a row for the colour `colour` of that costume. Game thread.
bool ColourInSpec(std::wstring_view code, int costume, int colour);

// A fighter's V-Skill or V-Trigger names as the game's name asset gave them,
// made one or two choices (picks::kPartChoices): none becomes one unnamed
// choice, more than two the first two. A change is logged with the fighter's
// code and `part` ("V-Skill") and the count the asset gave, so a count
// Ember did not expect shows in the log rather than refusing the roster.
std::vector<std::string> FitChoices(std::vector<std::string> names, const char* part, const std::string& fighter);

// For the log: `V-Skills 2 ("A", "B"), V-Triggers 1 ("C")`.
std::string ChoicesText(const FighterContent& fighter);

// How many V-Skills and V-Triggers a fighter has, as FitChoices makes them.
struct ChoiceCounts {
	int vSkills = 0;
	int vTriggers = 0;
};
// The fighter `code` names, compared as the game compares codes (ignoring
// case): its counts from ReadContentLists' last read of the name assets, so
// the pipe's launch cannot load a choice the game would show as "noname".
// Before any such read (a launch with Ember's catalog never filled, as the
// synctest mode's) the counts are read for this fighter alone, the same way.
// None while either name asset is not there, and for a fighter the last read
// did not list. Game thread, at the main menu.
std::optional<ChoiceCounts> FighterChoiceCounts(std::wstring_view code);

// Whether a battle can load the fighter `code` names: the game's fighter
// table (`charaAsset`, the game data's CharaAsset, sfv::content::kCharaAsset)
// has an entry with that code which names a battle setup of its own
// (sfv::content::kCharaSetupData). Random has none, and neither has ELEVEN:
// the select screen turns both into another fighter before a battle loads,
// which Ember's launch does not, and a battle launched with ELV itself hangs
// at a black screen. This is the one rule for which fighters exist for
// Ember: ReadContentLists lists only these, and a launch (VersusLaunch) takes
// only these, so a launch cannot reach a fighter the catalog leaves out.
// Whether this copy has the fighter's package is a separate, per-copy fact
// (FighterContent::installed). Reads only the table, no game code; false for
// a null table.
bool BattleFighter(uint8_t* charaAsset, std::wstring_view code);

// Development (-sfve_unowned, ModConfig): each mark takes a fighter, a
// costume of it or a colour of that costume from this copy's player, on top
// of `owned` (the game's answer, or kCostumeOwnership's for looks), so the
// gate's screens and refusals can be tried with an account that owns
// everything. Never the other way: nothing not owned becomes owned. Counted
// in `ownership.markedUnowned`.
void MarkUnowned(ContentLists& lists, const std::vector<UnownedContent>& marks);

// Development (-sfve_uninstalled, ModConfig): each code takes a stage of the
// universe from this copy's install, so one full install can play a copy
// that lacks stages. It only ever clears `installed`: the universe, and so
// the build id, stay as they are. A code outside the universe changes
// nothing, and so does one that would leave no stage installed. Returns how
// many stages it cleared.
int MarkUninstalled(ContentLists& lists, const std::vector<std::string>& codes);

// Game thread, at the main menu. A text the game's own localization data
// holds, by namespace and key (sfv::kCreateText, as the game builds its
// literals), as the game shows it in its current language, UTF-8; empty
// where the data has no such key (the game would show the key itself). Each
// call keeps its FText for the process's life, as the game keeps its own, so
// ask once per key.
std::string LocalizedText(const wchar_t* textNamespace, const wchar_t* key);

} // namespace sfve::mod
