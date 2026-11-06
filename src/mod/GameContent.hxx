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
//   package (`installed`), and the names, in the game's language.
//
// Stage names are the stage's data asset's FText (sfv::kStageName); fighter
// names the fighter's PersonalData FText; costume names the spec's (where it
// has none, the game's own "Costume<n>"). EMBER_LIBRARY.md section 15.

#include <string>
#include <vector>

namespace sfve::mod {

struct CostumeContent {
	int id = 0; // the game's costume number: its place in the fighter's CostumeDatas
	// The game's colour numbers for this costume, in the game's order: 0-99,
	// then 100 and up for the EX colours.
	std::vector<int> colours;
	std::string name;       // UTF-8, display only
	bool installed = false; // this copy can load it (sfv::kPlayableCostumes)
};

struct FighterContent {
	std::string code;
	std::vector<CostumeContent> costumes; // in the game's order
	std::string name;                     // UTF-8, display only; empty where the game has none
	bool installed = false;               // this copy has the fighter's package (sfv::kSelectableFighters)
};

struct ContentLists {
	std::vector<FighterContent> fighters; // in the order of the game's select screen
	std::vector<std::string> stages;      // playable ones (not random, not those without stage data)
	// UTF-8, one per stage in `stages`' order; empty where the game has none.
	std::vector<std::string> stageNames;
};

// Game thread. Empty lists until the game's content tables are loaded.
ContentLists ReadContentLists();

} // namespace sfve::mod
