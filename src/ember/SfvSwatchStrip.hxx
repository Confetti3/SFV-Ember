#pragma once

// Where the pick screen's pane puts a costume's colour chips (SfvPickPreview):
// every colour of the costume, in the game's order, as square chips in rows
// across the pane. EX colours (100 and up, the game's "EX01" on) start a row
// of their own, after room for an "EX" caption, as the game's costume menu
// sets them apart. A pure function of the colours and the sizes, so the tests
// lay out a strip without ImGui. EMBER_LIBRARY.md section 43.

#include <vector>

namespace sfve::adapter {

// The sizes a strip is laid out with, in the pane's pixels.
struct StripMetrics {
	float width = 0;    // the pane's width
	float chip = 0;     // a chip's side
	float gap = 0;      // between chips across a row
	float pitch = 0;    // from one row's top to the next's (a chip, its chosen mark and a gap)
	float exIndent = 0; // where an EX row's first chip starts (the caption's width and a gap)
};

struct Chip {
	int colour = 0;
	float x = 0, y = 0; // its top left, from the strip's
	bool chosen = false; // the stored or previewed colour, which the player can ready
	bool dimmed = false; // a colour this player cannot ready
	bool operator==(const Chip&) const = default;
};

struct StripLayout {
	std::vector<Chip> chips;
	float height = 0;  // the rows' pitches: 0 for no chips
	float exRow = -1;  // the first EX row's top, for its caption; -1 for none
};

// The colours as chips, rows wrapping at the metrics' width (at least one chip
// a row, however narrow). `ready` are the colours this player can ready; any
// other is dimmed, and a dimmed colour is never marked chosen. A `chosen`
// not among `colours` marks nothing.
StripLayout LayOutSwatchStrip(const std::vector<int>& colours, const std::vector<int>& ready, int chosen,
	const StripMetrics& metrics);

// The first EX colour number: the game numbers EX colours from 100.
inline constexpr int kFirstExColour = 100;

} // namespace sfve::adapter
