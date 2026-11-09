#include "ember/SfvSwatchStrip.hxx"

#include <algorithm>
#include <cmath>

namespace sfve::adapter {

namespace {

// How many chips a row `width` wide holds: at least one.
int ChipsPerRow(float width, const StripMetrics& metrics)
{
	const float step = metrics.chip + metrics.gap;
	if (!(step > 0)) return 1;
	return (std::max)(1, (int)std::floor((width + metrics.gap) / step));
}

} // namespace

StripLayout LayOutSwatchStrip(const std::vector<StripColour>& colours, int chosen, const StripMetrics& metrics)
{
	StripLayout layout;
	if (colours.empty()) return layout;
	int row = 0, column = 0;
	bool inEx = false;
	for (const StripColour& colour : colours) {
		const bool ex = colour.colour >= kFirstExColour;
		int perRow = ChipsPerRow(inEx ? metrics.width - metrics.exIndent : metrics.width, metrics);
		if (ex && !inEx) {
			// The EX colours' first row: a row of its own, after any normal colours.
			if (!layout.chips.empty()) row++;
			column = 0;
			inEx = true;
			layout.exRow = row * metrics.pitch;
			perRow = ChipsPerRow(metrics.width - metrics.exIndent, metrics);
		} else if (column >= perRow) {
			row++;
			column = 0;
		}
		Chip chip;
		chip.colour = colour.colour;
		chip.swatch = colour.swatch;
		chip.x = (inEx ? metrics.exIndent : 0) + column * (metrics.chip + metrics.gap);
		chip.y = row * metrics.pitch;
		chip.dimmed = !colour.ready;
		chip.chosen = colour.colour == chosen && !chip.dimmed;
		layout.chips.push_back(chip);
		column++;
	}
	layout.height = (row + 1) * metrics.pitch;
	return layout;
}

} // namespace sfve::adapter
