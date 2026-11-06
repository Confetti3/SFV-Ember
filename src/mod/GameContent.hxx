#pragma once

// The fighters and stages the game offers, as its own select screens list
// them (from its spec data and installed packages; no ownership involved),
// for the overlay's pickers. Codes are the game's: "RYU", "S01".
//
// Stages also carry the names the game's own menus show (sfv::kStageName:
// the stage's data asset's FText, in the game's language). Fighters carry
// none: the game's data has no fighter-name reader to call (EMBER_LIBRARY.md
// section 14), so the shell names them by code.

#include <string>
#include <vector>

namespace sfve::mod {

struct ContentLists {
	std::vector<std::string> fighters;
	std::vector<std::string> stages; // playable ones (not random, not those without stage data)
	// UTF-8, one per stage in `stages`' order; empty where the game has none.
	// For display only: names follow the game's language, so nothing that two
	// copies must agree on (the build's content fingerprint) reads them.
	std::vector<std::string> stageNames;
};

// Game thread. Empty lists until the game's content tables are loaded.
ContentLists ReadContentLists();

} // namespace sfve::mod
