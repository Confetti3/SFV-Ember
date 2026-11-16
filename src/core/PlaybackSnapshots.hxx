#pragma once

// Full states of a replay's playback, for the seeks that go back
// (core/ReplaySeeker.hxx): one at the first fight frame of each game, then one
// every kFirstSpacing frames. The states are the rollback layout's, taken with
// StateLayout::SaveTo and put back with LoadFrom as a rollback does (about a
// megabyte each), so at most kMost are held; when they are all taken every
// other one is dropped and the spacing doubles, which covers a long battle
// more coarsely instead of failing. The first of a game is never dropped: it
// is the earliest point a seek can reach.
//
// A seek restores the nearest state before its frame and plays on from it, so
// a snapshot is looked up by the frame it is strictly before: the update that
// plays after a restore is what puts the models where the state says they
// are (SfvAdapter::AfterLoad, SocketReads), so a landing is never on the
// restored state itself.
//
// Game thread only.

#include "core/StateRegions.hxx"

#include <stdint.h>

#include <cstddef>
#include <vector>

namespace sfve {

class PlaybackSnapshots {
public:
	static constexpr size_t kMost = 48;
	static constexpr int kFirstSpacing = 120;

	struct Snapshot {
		int frame = 0;
		std::vector<uint8_t> bytes;
	};

	explicit PlaybackSnapshots(size_t most = kMost, int spacing = kFirstSpacing) : most_(most < 2 ? 2 : most), firstSpacing_(spacing), spacing_(spacing) {}

	// A game starts (named by its first frame): the states of the last one
	// are no longer reachable, and the spacing starts over. The buffers stay.
	void Clear(int game);
	int Game() const { return game_; }

	// Whether a state is due at the start of `frame`: none is held for this
	// game, or the nearest at or before it is a spacing or more back.
	bool Due(int frame) const;
	// Takes the state at the start of `frame` from `layout` (thinning first
	// when all kMost are held).
	void Take(const StateLayout& layout, int frame);

	// The nearest state strictly before `frame`, or null.
	const Snapshot* NearestBefore(int frame) const;
	// The earliest, the game's first, or null.
	const Snapshot* First() const { return count_ ? &held_[0] : nullptr; }
	size_t Count() const { return count_; }
	int Spacing() const { return spacing_; }
	// The frames of those held, in order (tests, the status).
	std::vector<int> Frames() const;

private:
	// Drops every other one, the first of each pair kept, and doubles the spacing.
	void Thin();
	size_t most_;
	int firstSpacing_, spacing_;
	int game_ = -1;
	// held_[0, count_) in frame order; the rest are spare buffers.
	std::vector<Snapshot> held_;
	size_t count_ = 0;
};

} // namespace sfve
