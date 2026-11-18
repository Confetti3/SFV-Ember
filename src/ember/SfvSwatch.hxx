#pragma once

// A colour's swatch as the pick screen draws it (SfvPickPreview's colour tag
// and the colour cards' fallback chip): the two colours the game's costume
// menu shows for it, and the costume's colours as SfvArt's CostumePalette
// keeps them. EMBER_LIBRARY.md section 43.

#include <cstdint>
#include <optional>
#include <utility>

namespace sfve::adapter {

// A colour's swatch: the two colours (0xRRGGBB each) the game's costume menu
// shows for it, the first on top.
using SwatchColours = std::pair<std::uint32_t, std::uint32_t>;

// One colour of a costume (SfvArt's CostumePalette).
struct PaletteColour {
	int colour = 0;
	bool ready = false;                  // this player can ready the costume in it
	std::optional<SwatchColours> swatch; // none while it loads, or where the game has none
	bool operator==(const PaletteColour&) const = default;
};

} // namespace sfve::adapter
