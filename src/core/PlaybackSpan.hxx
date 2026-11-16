#pragma once

// Where a replay's playback may seek (docs/design/beta10/replay-seek.md): the
// frames of the game it is in, and the frames a seek may land on.
//
// A seek restores a state a snapshot took (core/PlaybackSnapshots.hxx) and
// plays on, so it only lands where a rollback would be legal: at the start of
// a frame that reads the players' inputs (RecordedFrame::Fight). Outside the
// fight the battle's flow waits on presentation a state does not hold, so no
// state is taken or restored there. A seek never leaves its game: a rematch
// in place starts another, whose own frames the room's WIN answers (held
// frames, RecordedFrame::Held) set apart.
//
// A position is a frame number: the frames played so far, so "frame F" is the
// state at the start of frame F, the one the next update plays.

#include "core/ReplayFrames.hxx"

#include <algorithm>

namespace sfve::reach {

// The game a frame is in: its frames [from, to), between the held frames
// before and after it, and the first and last of them that read inputs.
struct Span {
	int from = 0, to = 0;
	int first = -1, last = -1;
	bool Has() const { return first >= 0; }
	bool Contains(int frame) const { return frame >= from && frame < to; }
};

// The game `frame` is in; empty past the frames and on a held frame (the
// WIN's answers, where nothing is sought).
inline Span SpanAt(const ReplayFrames& frames, int frame)
{
	Span span;
	const int size = static_cast<int>(frames.size());
	if (frame < 0 || frame >= size || frames[(size_t)frame].Held()) return span;
	int from = frame, to = frame;
	while (from > 0 && !frames[(size_t)from - 1].Held()) from--;
	while (to < size && !frames[(size_t)to].Held()) to++;
	span.from = from;
	span.to = to;
	for (int i = from; i < to; i++) {
		if (!frames[(size_t)i].Fight()) continue;
		if (span.first < 0) span.first = i;
		span.last = i;
	}
	return span;
}

// The nearest legal landing at or before `target` that is not before `floor`
// (a fight frame of the span), or -1.
inline int LandingBefore(const ReplayFrames& frames, int target, int floor)
{
	for (int at = std::min(target, static_cast<int>(frames.size()) - 1); at >= floor && at >= 0; at--) {
		if (frames[(size_t)at].Fight()) return at;
	}
	return -1;
}

// The nearest legal landing at or after `target` that is not past `last`, or -1.
inline int LandingAfter(const ReplayFrames& frames, int target, int last)
{
	for (int at = std::max(target, 0); at <= last && at < static_cast<int>(frames.size()); at++) {
		if (frames[(size_t)at].Fight()) return at;
	}
	return -1;
}

} // namespace sfve::reach
