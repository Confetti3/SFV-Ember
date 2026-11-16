#pragma once

// How many battle ticks each update of a bound battle ran (SfvAdapter), so
// two copies' logs show directly whether one ran more or fewer ticks for the
// same frame (docs/design/beta10/desync.md, "Battle frame counts"). One tick
// an update is the rule: the session drives one update a frame with the
// game's extra ticks at 0, and an update runs none while the battle frame is
// stopped (a KO's). A line is logged where the count changes, with the
// input frame it changed at, the first kMostLines a battle; the battle's
// totals as it ends. Pure: the caller logs.

#include <stdint.h>

#include <string>

namespace sfve::mod {

class TickCounts {
public:
	static constexpr int kMostLines = 64;

	// One update at input frame `frame` ran `ticks` ticks (`replayed`: after a
	// restore). The line to log when the count is not the last update's
	// (within kMostLines a battle); empty otherwise.
	std::string Note(int32_t frame, uint32_t ticks, bool replayed);
	// "N updates: Z with no tick, M with more than one; C changes" for the
	// battle's end; empty for a battle with no update noted.
	std::string Summary() const;
	void Forget() { *this = {}; }

private:
	uint64_t updates_ = 0, none_ = 0, more_ = 0, changes_ = 0;
	int64_t last_ = -1; // the last update's count; -1 before the first
	int lines_ = 0;
};

} // namespace sfve::mod
