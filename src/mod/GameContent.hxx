#pragma once

// The fighters and stages the game offers, as its own select screens list
// them (from its spec data and installed packages; no ownership involved),
// for the overlay's pickers. Codes are the game's: "RYU", "S01".

#include <string>
#include <vector>

namespace sfve::mod {

struct ContentLists {
	std::vector<std::string> fighters;
	std::vector<std::string> stages; // playable ones (not random, not those without stage data)
};

// Game thread. Empty lists until the game's content tables are loaded.
ContentLists ReadContentLists();

} // namespace sfve::mod
