// The pick screen pane's colour chips (SfvSwatchStrip), laid out without
// ImGui: rows wrap at the pane's width, EX colours start a row of their own,
// the chosen colour is marked, what cannot be readied is dimmed, and each
// chip carries its colour's swatch. Then the pane's arithmetic
// (SfvPickPreview) at the smallest window.

#include <doctest/doctest.h>

#include "ember/SfvPickPreview.hxx"
#include "ember/SfvSwatchStrip.hxx"

#include <algorithm>
#include <optional>
#include <vector>

using namespace sfve::adapter;

namespace {

// Chips of 10 with gaps of 2: eight a row in 94 (8 x 10 + 7 x 2), rows 16 apart.
StripMetrics Metrics(float width = 94, float exIndent = 0)
{
	return {width, 10, 2, 16, exIndent};
}

const Chip& ChipOf(const StripLayout& layout, int colour)
{
	const auto at = std::find_if(layout.chips.begin(), layout.chips.end(), [colour](const Chip& chip) { return chip.colour == colour; });
	REQUIRE(at != layout.chips.end());
	return *at;
}

// RYU's default costume: colours 01-15, then EX01 and EX09.
std::vector<int> RyuColours()
{
	std::vector<int> colours;
	for (int colour = 0; colour < 15; colour++) colours.push_back(colour);
	colours.push_back(100);
	colours.push_back(108);
	return colours;
}

// The colours as the strip takes them, those in `ready` this player's, none
// with a swatch yet.
std::vector<StripColour> Strip(const std::vector<int>& colours, const std::vector<int>& ready)
{
	std::vector<StripColour> strip;
	for (int colour : colours) strip.push_back({colour, std::find(ready.begin(), ready.end(), colour) != ready.end(), std::nullopt});
	return strip;
}

} // namespace

TEST_CASE("a costume's colours wrap at the pane's width, and its EX colours start a row of their own")
{
	const std::vector<int> colours = RyuColours();
	const StripLayout layout = LayOutSwatchStrip(Strip(colours, colours), 3, Metrics());
	REQUIRE(layout.chips.size() == 17);
	// Eight a row: 0-7, then 8-14, then the EX colours.
	CHECK(ChipOf(layout, 0).x == 0);
	CHECK(ChipOf(layout, 0).y == 0);
	CHECK(ChipOf(layout, 7).x == 84);
	CHECK(ChipOf(layout, 7).y == 0);
	CHECK(ChipOf(layout, 8).x == 0);
	CHECK(ChipOf(layout, 8).y == 16);
	CHECK(ChipOf(layout, 14).y == 16);
	CHECK(ChipOf(layout, 100).x == 0);
	CHECK(ChipOf(layout, 100).y == 32);
	CHECK(ChipOf(layout, 108).x == 12);
	CHECK(ChipOf(layout, 108).y == 32);
	CHECK(layout.exRow == 32);
	CHECK(layout.height == 48);
	// The chips keep the game's order.
	for (size_t i = 0; i < colours.size(); i++) CHECK(layout.chips[i].colour == colours[i]);
}

TEST_CASE("an EX row starts after its caption, and a full normal row leaves no empty row before it")
{
	std::vector<int> colours{0, 1, 2, 3, 4, 5, 6, 7, 100, 101};
	const StripLayout layout = LayOutSwatchStrip(Strip(colours, colours), -1, Metrics(94, 20));
	CHECK(ChipOf(layout, 7).y == 0);
	CHECK(ChipOf(layout, 100).y == 16);
	CHECK(ChipOf(layout, 100).x == 20);
	CHECK(ChipOf(layout, 101).x == 32);
	CHECK(layout.exRow == 16);
	CHECK(layout.height == 32);
	// No EX colours: no caption's row.
	const StripLayout plain = LayOutSwatchStrip(Strip({0, 1, 2}, {0, 1, 2}), 0, Metrics(94, 20));
	CHECK(plain.exRow == -1);
	CHECK(plain.height == 16);
	// EX colours alone start on the first row.
	const StripLayout exOnly = LayOutSwatchStrip(Strip({100}, {100}), 100, Metrics(94, 20));
	CHECK(exOnly.exRow == 0);
	CHECK(ChipOf(exOnly, 100).x == 20);
	CHECK(ChipOf(exOnly, 100).y == 0);
}

TEST_CASE("the chosen colour is marked, and a colour this player cannot ready is dimmed and never marked")
{
	const std::vector<int> colours = RyuColours();
	std::vector<int> ready = colours;
	ready.erase(std::remove(ready.begin(), ready.end(), 5), ready.end());
	ready.erase(std::remove(ready.begin(), ready.end(), 108), ready.end());
	const StripLayout layout = LayOutSwatchStrip(Strip(colours, ready), 2, Metrics());
	CHECK(ChipOf(layout, 2).chosen);
	CHECK_FALSE(ChipOf(layout, 2).dimmed);
	CHECK(std::count_if(layout.chips.begin(), layout.chips.end(), [](const Chip& chip) { return chip.chosen; }) == 1);
	CHECK(ChipOf(layout, 5).dimmed);
	CHECK(ChipOf(layout, 108).dimmed);
	CHECK_FALSE(ChipOf(layout, 0).dimmed);
	// A chosen colour that cannot be readied carries no mark.
	CHECK_FALSE(ChipOf(LayOutSwatchStrip(Strip(colours, ready), 5, Metrics()), 5).chosen);
	// A chosen colour the costume does not have marks nothing.
	const StripLayout none = LayOutSwatchStrip(Strip(colours, ready), 42, Metrics());
	CHECK(std::none_of(none.chips.begin(), none.chips.end(), [](const Chip& chip) { return chip.chosen; }));
	// A costume not this player's: every chip dimmed.
	const StripLayout locked = LayOutSwatchStrip(Strip(colours, {}), 2, Metrics());
	CHECK(std::all_of(locked.chips.begin(), locked.chips.end(), [](const Chip& chip) { return chip.dimmed && !chip.chosen; }));
}

TEST_CASE("an empty strip takes no room, and a pane narrower than a chip still holds one a row")
{
	const StripLayout empty = LayOutSwatchStrip({}, 0, Metrics());
	CHECK(empty.chips.empty());
	CHECK(empty.height == 0);
	CHECK(empty.exRow == -1);
	const StripLayout narrow = LayOutSwatchStrip(Strip({0, 1, 2}, {0, 1, 2}), 0, Metrics(4));
	REQUIRE(narrow.chips.size() == 3);
	for (int i = 0; i < 3; i++) {
		CHECK(narrow.chips[(size_t)i].x == 0);
		CHECK(narrow.chips[(size_t)i].y == 16.f * i);
	}
	CHECK(narrow.height == 48);
}

TEST_CASE("each chip carries its own colour's swatch, and none while it loads")
{
	std::vector<StripColour> colours = Strip({0, 1, 100}, {0, 1, 100});
	colours[0].swatch = SwatchColours{0x112233, 0x445566};
	colours[2].swatch = SwatchColours{0xAABBCC, 0xDDEEFF};
	const StripLayout layout = LayOutSwatchStrip(colours, 1, Metrics());
	CHECK(ChipOf(layout, 0).swatch == SwatchColours{0x112233, 0x445566});
	CHECK_FALSE(ChipOf(layout, 1).swatch.has_value());
	CHECK(ChipOf(layout, 100).swatch == SwatchColours{0xAABBCC, 0xDDEEFF});
}

TEST_CASE("at the smallest window the pane's picture gives way, so every line of the look and the loadout shows")
{
	// The pane at 960x540 and the default interface size, as the laptop's
	// pick screen shot measures it: 346 tall inside its padding, lines 26
	// apart, the banner 236 tall at the pane's 348, a gap of 10, and RYU's
	// 17 colours in two rows of 27.
	const float room = 346, line = 26, natural = 236, gap = 10, strip = 2 * 27;
	const float words = LookWordsHeight(line, strip);
	CHECK(words == 3 * line + strip + 2 * line); // names, colour line, V-Skill and V-Trigger
	const float picture = (std::min)(natural, PictureRoom(room, words, gap));
	CHECK(picture == 152);
	CHECK(picture + gap + words <= room);
	// Three rows of chips: the picture gives way again.
	CHECK(PictureRoom(room, LookWordsHeight(line, 3 * 27), gap) == 125);
	// At 1280x720 the pane is taller than it needs: the banner at its size.
	CHECK(PictureRoom(532, words, gap) >= natural);
	// Words that fill the pane leave no room for a picture, never less.
	CHECK(PictureRoom(100, 200, gap) == 0);
}
