// RollbackSession: watching (a watcher's frames from P1's stream, and a
// replay's, which play the same way), P1's watchers and the session kept for
// them after the fight.

#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"
#include "core/RollbackSessionScopes.hxx"
#include "core/SyncTest.hxx"

#include <stdio.h>
#include <windows.h>

namespace sfve {

RollbackSession::StepResult RollbackSession::StepWatched(const std::function<void(const FrameInputs&)>* onHeld)
{
	FrameInputs inputs;
	if (replaying_) {
		// The recording's frame, in place of P1's stream.
		if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;
		if (lagged_ && !laggedFinished_ && (size_t)frame_ >= config_.replay->size()) {
			// Past the last frame every input is known: the last guesses are
			// settled and the last checkpoints hashed, in a step of their own.
			lagged_->Finish();
			laggedFinished_ = true;
			HashLagged();
			return StepResult::Waiting;
		}
		if (lagged_ && onHeld) {
			Fail("a lagged replay cannot hold the game");
			return StepResult::Failed;
		}
		if (!ReplayInputs(onHeld != nullptr, &inputs)) return StepResult::Failed;
	} else {
		ActiveScope active(this);
		ggpo_idle(session_, 0);
		if (DrainPendingAbort()) return StepResult::Failed;
		// GGPO's spectator backend never times out; a stream that never ran ends
		// here (SF4's SpectatorStreamWatch), and the room says what was lost.
		if (streamWatch_.Poll(GetTickCount64(), false) != sf4e::SpectatorStreamWatch::Step::Wait) {
			char why[96];
			snprintf(why, sizeof(why), "P1's stream did not start within %llu s",
				(unsigned long long)(sf4e::SpectatorPolicy::StreamSyncTimeoutMs / 1000));
			Fail(why);
			return StepResult::Failed;
		}
		if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;

		int disconnectFlags = 0;
		const GGPOErrorCode err = ggpo_synchronize_input(session_, inputs.p, sizeof(inputs.p), &disconnectFlags);
		if (DrainPendingAbort()) return StepResult::Failed;
		// The spectator backend answers PREDICTION_THRESHOLD for a frame P1 has
		// not sent yet: it never predicts.
		if (err == GGPO_ERRORCODE_PREDICTION_THRESHOLD || err == GGPO_ERRORCODE_NOT_SYNCHRONIZED) return StepResult::Waiting;
		if (!GGPO_SUCCEEDED(err)) {
			// GENERAL_FAILURE: the frame was overwritten before it was played (the
			// backlog outgrew GGPO's ring), so the rest of the stream cannot be
			// played.
			char why[96];
			snprintf(why, sizeof(why), "P1's stream broke at frame %d (synchronize_input returned %d)", frame_, (int)err);
			Fail(why);
			return StepResult::Failed;
		}
	}
	// Frame 0, the battle's first state, is hashed before the first frame
	// runs, as P1 and P2 save it before they add their first input.
	if (!watchedStartHashed_) {
		watchedStartHashed_ = true;
		HashWatched(frame_);
	}
	if (onHeld) {
		if (!holding_) {
			holding_ = true;
			heldFrom_ = frame_;
			CoreLog("session: holding the game from frame %d", frame_);
		}
		Report(inputs, RecordedFrame::kHeld);
		(*onHeld)(inputs);
		stats_.heldFrames++;
	} else {
		if (holding_) {
			holding_ = false;
			heldTo_ = frame_;
			CoreLog("session: hold ended at frame %d (%d held frame(s))", frame_, frame_ - heldFrom_);
		}
		// What the fighters' sessions give the game for this frame
		// (InputsForThisFrame), without telling GGPO: a watcher never predicts,
		// and every frame it plays is confirmed.
		const FrameInputs given = GivenAndReported(inputs);
		// The replay's rollback checks play the frame themselves, as long as
		// their frames are the session's (a held frame ends them).
		if (lagged_) {
			lagged_->Step(frame_);
		} else if (replayCheck_ && replayCheck_->Report().framesRun == frame_) {
			replayCheck_->Step(given);
			LogReplayCheck();
		} else {
			game_->Advance(given, false);
		}
		stats_.framesAdvanced++;
		// A watcher saves nothing, so its frames are checked here instead.
		if (FailedByGame()) return StepResult::Failed;
	}
	if (session_) {
		const GGPOErrorCode advanced = ggpo_advance_frame(session_);
		if (DrainPendingAbort()) return StepResult::Failed;
		if (!GGPO_SUCCEEDED(advanced)) {
			char why[96];
			snprintf(why, sizeof(why), "advance_frame returned %d", (int)advanced);
			Fail(why);
			return StepResult::Failed;
		}
	}
	frame_++;
	if (lagged_) HashLagged();
	else HashWatched(frame_);
	return StepResult::Advanced;
}

void RollbackSession::HashWatched(int frame)
{
	if (frame % kCheckpointInterval != 0 && frame % config_.hashEveryFrames != 0) return;
	Slot& slot = pool_[0];
	LARGE_INTEGER started, copied;
	QueryPerformanceCounter(&started);
	game_->Layout().SaveTo(slot.bytes.data());
	QueryPerformanceCounter(&copied);
	slot.frame = frame;
	slot.used = true; // SavedState answers it (a dump)
	slot.copied = true;
	// A scratch copy, never a save GGPO holds.
	RecordHash(frame, slot.bytes.data(), copied.QuadPart - started.QuadPart);
}

int RollbackSession::WatchedBacklog() const
{
	int frames = -1;
	if (!session_ || !config_.watching || !GGPO_SUCCEEDED(ggpo_get_spectator_backlog(session_, &frames))) return -1;
	return frames;
}

size_t RollbackSession::WatcherStreams() const
{
	return session_ && !config_.watching ? policy_.Count() : 0;
}

void RollbackSession::KeepForWatchers()
{
	if (keptForWatchers_) return;
	keptForWatchers_ = true;
	// The frames final by now are hashed and reported while the game is
	// still the one they describe; the saves not final yet never will be.
	HashConfirmed();
	game_ = &detached_;
	config_.onFrame = nullptr;
	config_.onCheckpoint = nullptr;
	config_.onSaveSkipped = nullptr;
	config_.verifyLoads = false;
	config_.verifyLoadsEvery = 0;
	// The stand-in's state is nothing, so every save holds all of it: no load
	// GGPO makes from here can reach a save without its copy.
	config_.skipUnloadableSaves = false;
	for (Slot& s : pool_) {
		s.copied = s.used;
		s.pending = false;
	}
	CoreLog("session: kept for %zu watcher stream(s) after the fight; the game is let go", WatcherStreams());
}

bool RollbackSession::WatcherHandle(GGPOPlayerHandle handle) const
{
	if (config_.watching) return false;
	return handle != localHandle_ && handle != remoteHandle_;
}

void RollbackSession::PollWatchers()
{
	if (!session_ || config_.watching || !policy_.Count()) return;
	const uint64_t now = GetTickCount64();
	for (const int handle : policy_.SyncOverdue(now)) DropWatcher(handle, "not synchronized in time");
	if (!policy_.SampleDue(now)) return;
	for (const int handle : policy_.Handles()) {
		GGPONetworkStats stats = {};
		if (!GGPO_SUCCEEDED(ggpo_get_network_stats(session_, handle, &stats))) continue;
		if (policy_.Sample(handle, stats.network.send_queue_len, stats.network.ping)) DropWatcher(handle, "too far behind");
	}
}

void RollbackSession::DropWatcher(int handle, const char* why)
{
	GGPONetworkStats stats = {};
	ggpo_get_network_stats(session_, handle, &stats);
	CoreLog("session: dropping the watcher with handle %d (%s; ping %d ms, %d frames unacknowledged)", handle, why,
		stats.network.ping, stats.network.send_queue_len);
	// GGPO raises the drop's DISCONNECTED_FROM_PEER at once (Event), which
	// notes it for the room too; a watcher already gone answers DISCONNECTED.
	const GGPOErrorCode result = ggpo_disconnect_player(session_, handle);
	if (!GGPO_SUCCEEDED(result) && result != GGPO_ERRORCODE_PLAYER_DISCONNECTED) {
		CoreLog("session: the watcher with handle %d could not be dropped (%d)", handle, (int)result);
	}
	links_.Dropped(handle);
}

} // namespace sfve
