#pragma once

// The fighters, their costumes and colours, and the stages the game offers,
// read from its own tables for the overlay's pickers and Ember's catalog.
// Codes are the game's ("RYU", "S01"); costume and colour numbers are the
// game's own (what a battle's setup takes, and what the native costume
// settings menu lists).
//
// Two kinds of fact, kept apart because two copies of the game must agree on
// the first and need not on the second:
// - The pick space: which fighters, costumes and colours exist. It comes from
//   the game's spec data (DA_Spec) and fighter table (DA_CharaEntry), never
//   from what this copy has installed or owns (sfv::kSpecFighters,
//   kSpecCostumes, kSpecColours).
// - This copy's own: whether its install has a fighter's or a costume's
//   package (`installed`), what the game lets this copy's player pick
//   (`owned`, sfv::kOwnedFighters and the rest), and the names, in the game's language.
//
// Stage names are the stage's data asset's FText (sfv::kStageName); fighter
// names the fighter's PersonalData FText; costume names the spec's (where it
// has none, the game's own "Costume<n>"). EMBER_LIBRARY.md section 15.

#include "mod/ModConfig.hxx"

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod {

struct CostumeContent {
	int id = 0; // the game's costume number: its place in the fighter's CostumeDatas
	// The game's colour numbers for this costume, in the game's order: 0-99,
	// then 100 and up for the EX colours.
	std::vector<int> colours;
	std::string name;       // UTF-8, display only
	bool installed = false; // this copy can load it (sfv::kPlayableCostumes)
	bool owned = false;     // the game lets this copy's player pick it (sfv::kOwnedCostumes)
	// The colours of `colours` the game lets this copy's player pick
	// (sfv::kOwnedColours), in the same order.
	std::vector<int> ownedColours;
};

struct FighterContent {
	std::string code;
	std::vector<CostumeContent> costumes; // in the game's order
	std::string name;                     // UTF-8, display only; empty where the game has none
	bool installed = false;               // this copy has the fighter's package (sfv::kSelectableFighters)
	bool owned = false;                   // the game lets this copy's player pick the fighter (sfv::kOwnedFighters)
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
};

struct ContentLists {
	std::vector<FighterContent> fighters; // in the order of the game's select screen
	std::vector<std::string> stages;      // playable ones (not random, not those without stage data)
	// UTF-8, one per stage in `stages`' order; empty where the game has none.
	std::vector<std::string> stageNames;
	// The stages of `stages` the game lets this copy's player choose
	// (sfv::kOwnedStages), in the same order; logged, not gated (a room's
	// stage is P1's choice and the battle jump runs no check in a room).
	std::vector<std::string> ownedStages;
	OwnershipSource ownership;
};

// Game thread. Empty lists until the game's content tables and its battle
// setup asset are loaded. The setup asset is found, or loaded, with
// StaticLoadObject, so call it at the main menu (never in a battle).
ContentLists ReadContentLists();

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
// of the game's answer, so the gate's screens and refusals can be tried with
// an account that owns everything. Never the other way: nothing the game
// does not allow becomes owned. Counted in `ownership.markedUnowned`.
void MarkUnowned(ContentLists& lists, const std::vector<UnownedContent>& marks);

// Game thread, at the main menu. A text the game's own localization data
// holds, by namespace and key (sfv::kCreateText, as the game builds its
// literals), as the game shows it in its current language, UTF-8; empty
// where the data has no such key (the game would show the key itself). Each
// call keeps its FText for the process's life, as the game keeps its own, so
// ask once per key.
std::string LocalizedText(const wchar_t* textNamespace, const wchar_t* key);

} // namespace sfve::mod
