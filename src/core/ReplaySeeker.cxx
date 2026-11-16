#include "core/ReplaySeeker.hxx"

#include "core/CoreLog.hxx"

namespace sfve {

void ReplaySeeker::Reset()
{
	snapshots_ = PlaybackSnapshots();
	span_ = reach::Span();
	taken_ = restored_ = 0;
}

const reach::Span& ReplaySeeker::SpanAt(const ReplayFrames& frames, int frame)
{
	// The span is a fact of the recording: it is made again only when the
	// frame leaves it (the next game, or a held frame).
	if (!span_.Contains(frame) || span_.to <= span_.from) span_ = reach::SpanAt(frames, frame);
	return span_;
}

void ReplaySeeker::Before(const RollbackSession& session, const ReplayFrames& frames)
{
	const int frame = session.Frame();
	if (frame < 0 || (size_t)frame >= frames.size() || !frames[(size_t)frame].Fight()) return;
	const reach::Span& span = SpanAt(frames, frame);
	if (!span.Has()) return;
	if (snapshots_.Game() != span.from) {
		snapshots_.Clear(span.from);
		CoreLog("seek: the game from frame %d (fight frames %d to %d) starts its snapshots", span.from, span.first, span.last);
	}
	if (!snapshots_.Due(frame)) return;
	snapshots_.Take(session.Layout(), frame);
	taken_++;
}

SeekGround ReplaySeeker::Ground(const RollbackSession& session, const ReplayFrames& frames, bool diagnostics)
{
	SeekGround ground;
	ground.frame = session.Frame();
	ground.span = SpanAt(frames, ground.frame);
	ground.snapshots = snapshots_.Count();
	if (const PlaybackSnapshots::Snapshot* first = snapshots_.First()) {
		if (snapshots_.Game() == ground.span.from) ground.ringFirst = first->frame;
	}
	// Not while the game is held, not over a diagnostic playback's own
	// rollbacks, and not past the frames.
	ground.canRestore = !diagnostics && session.Replaying() && session.HeldFrames() == 0 && ground.span.Has();
	return ground;
}

int ReplaySeeker::Restore(RollbackSession& session, int before)
{
	const PlaybackSnapshots::Snapshot* snapshot = snapshots_.NearestBefore(before);
	if (!snapshot || !session.RestoreReplay(snapshot->bytes.data(), snapshot->frame)) return -1;
	restored_++;
	return snapshot->frame;
}

// Defined here, with the seek it serves, so the session's own file stays as it is.
bool RollbackSession::RestoreReplay(const uint8_t* state, int frame)
{
	if (!replaying_ || lagged_ || replayCheck_ || holding_ || failed_ || !state || frame < 0) return false;
	game_->Layout().LoadFrom(state);
	game_->AfterLoad();
	// The restore did not reach all of the game: the session ends (Fail).
	if (FailedByGame()) return false;
	frame_ = frame;
	CoreLog("session: the replay goes back to frame %d", frame);
	return true;
}

} // namespace sfve
