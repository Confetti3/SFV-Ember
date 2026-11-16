#pragma once

// What a replay's playback needs of the session to seek (docs/design/beta10/
// replay-seek.md): the states it takes as it plays (core/PlaybackSnapshots.hxx),
// where the game it is in begins and ends (core/PlaybackSpan.hxx), and the
// restore that puts one back.
//
// The playback's driver (the netplay mode) calls Before for each frame it is
// about to play, reads Ground for what the playback may ask this engine
// frame, and Restore when the playback's turn says to go back. What to seek
// to, and the transport around it, is the playback's (ember/SfvPlayback.hxx);
// nothing here knows the game.
//
// A forward seek needs none of this: it is the playback played on, a few
// frames to the engine frame.

#include "core/PlaybackSnapshots.hxx"
#include "core/PlaybackSpan.hxx"
#include "core/ReplayFrames.hxx"
#include "core/RollbackSession.hxx"

namespace sfve {

// What the playback sees of the driver each engine frame.
struct SeekGround {
	int frame = 0;                 // the frames played: the next update plays this one
	reach::Span span;           // the game this frame is in (empty on a held frame)
	int ringFirst = -1;            // the earliest state held for this game, -1 for none
	bool canRestore = false;       // a state can be put back now
	size_t snapshots = 0;          // states held
};

class ReplaySeeker {
public:
	// A new playback: nothing is held.
	void Reset();
	// The frame the session is about to play is a fight frame whose state is
	// due: take it. Every frame, before its update.
	void Before(const RollbackSession& session, const ReplayFrames& frames);
	// `diagnostics`: the playback is a lagged or rollback-checked one (a test's
	// -sfve_replay_lag, -sfve_replay_check), which never seeks.
	SeekGround Ground(const RollbackSession& session, const ReplayFrames& frames, bool diagnostics);
	// Puts back the state nearest before `before`; the frame the session
	// stands at then, or -1 when none could be.
	int Restore(RollbackSession& session, int before);

	size_t Taken() const { return taken_; }
	size_t Restored() const { return restored_; }

private:
	// The game `frame` is in, kept while the frame stays in it.
	const reach::Span& SpanAt(const ReplayFrames& frames, int frame);
	PlaybackSnapshots snapshots_;
	reach::Span span_;
	size_t taken_ = 0, restored_ = 0;
};

} // namespace sfve
