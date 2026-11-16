// RollbackSession: GGPO's callbacks and what they reach: the save pool
// (Save, Load, Free), a rollback's frames and GGPO's events.

#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"

#include <windows.h>

namespace sfve {

RollbackSession* RollbackSession::s_active = nullptr;

RollbackSession::Slot RollbackSession::refusedSave_;

// --- GGPO callbacks -------------------------------------------------------

bool __cdecl RollbackSession::OnBeginGame(const char*) { return true; }
bool __cdecl RollbackSession::OnLogState(char*, unsigned char*, int) { return true; }

bool __cdecl RollbackSession::OnSave(unsigned char** buffer, int* len, int* checksum, int frame)
{
	// GGPO keeps whatever it is handed and asserts, ending the process, when
	// it loads a frame it holds no buffer for. So every save hands one over: a
	// refused save the refusal, which a load fails on cleanly (Load).
	Slot* slot = s_active ? s_active->Save(frame) : nullptr;
	*buffer = reinterpret_cast<unsigned char*>(slot ? slot : &refusedSave_);
	*len = 1;      // GGPO asserts on a zero length; it never reads the bytes
	*checksum = 0; // nor the checksum (only its synctest backend would)
	return slot != nullptr;
}

bool __cdecl RollbackSession::OnLoad(unsigned char* buffer, int)
{
	return s_active && s_active->Load(buffer);
}

void __cdecl RollbackSession::OnFree(void* buffer)
{
	if (s_active) s_active->Free(buffer);
}

bool __cdecl RollbackSession::OnAdvance(int)
{
	return s_active && s_active->AdvanceFromRollback();
}

bool __cdecl RollbackSession::OnEvent(GGPOEvent* info)
{
	if (s_active && info) s_active->Event(*info);
	return true;
}

RollbackSession::Slot* RollbackSession::Save(int frame)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	// GGPO saves after every frame it advances, a rollback's replays
	// included, so this is where a failure the game latched while advancing
	// (or while a held frame changed it) ends the session, before a state it
	// can no longer restore is kept.
	if (FailedByGame()) return nullptr;
	// Nor after any other failure inside this call (a load that could not
	// restore): the live state is no frame's state any more.
	if (failed_) return nullptr;
	// The last frame a rollback replays: every save it left on the guessed
	// timeline has been replaced (GGPO freed this frame's old one first).
	if (rollingBack_ && frame >= rollbackEnd_) rollingBack_ = false;
	// The frames the last GGPO call confirmed are hashed first, so frames are
	// hashed in order, and confirmed_ is current for this save.
	HashConfirmed();
	Slot* slot = nullptr;
	for (Slot& s : pool_) {
		if (!s.used) {
			slot = &s;
			break;
		}
	}
	if (!slot) {
		Fail("save pool exhausted");
		return nullptr;
	}
	StateLayout& layout = game_->Layout();
	slot->used = true;
	slot->frame = frame;
	slot->pending = false;
	slot->copyTicks = 0;
	// GGPO loads the save of frame F only for a rollback to F, which starts
	// at the first frame whose inputs it guessed wrong, a frame after the
	// last confirmed one; or when a fighter disconnects, at that fighter's
	// last frame received, never before the last confirmed one. So once the
	// inputs of frame F + 1 are confirmed, no load reaches F's save
	// (docs/design/beta8/perf.md), and its copy is skipped.
	slot->copied = !config_.skipUnloadableSaves || frame == keepSave_ || confirmed_ < frame + 1;
	if (slot->copied) {
		LARGE_INTEGER started, copied;
		QueryPerformanceCounter(&started);
		layout.SaveTo(slot->bytes.data());
		QueryPerformanceCounter(&copied);
		slot->copyTicks = copied.QuadPart - started.QuadPart;
	} else {
		stats_.savesSkipped++;
		layout.CountSkippedSave();
		if (config_.onSaveSkipped) config_.onSaveSkipped(frame);
	}
	if (HashedFrame(frame)) {
		// A save with no copy is final (its inputs are confirmed past it), and
		// the live state is its state: hashed now, from memory.
		if (!slot->copied) RecordHash(frame, nullptr, 0);
		else if (!config_.hashOnConfirm || Final(frame)) RecordHash(frame, slot->bytes.data(), slot->copyTicks);
		else slot->pending = true;
	}
	frame_ = frame;
	return slot;
}

bool RollbackSession::Load(unsigned char* buffer)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	Slot* slot = reinterpret_cast<Slot*>(buffer);
	if (!slot || !slot->used) {
		Fail(slot == &refusedSave_ ? "load of a save the session refused" : "load of a freed state");
		return false;
	}
	// Nor into a game the layout no longer describes (GameAdapter::
	// StateFailure): the session fails before a byte is written.
	if (FailedByGame()) return false;
	// GGPO replays from here back to the current frame, saving each again.
	rollingBack_ = true;
	rollbackFrom_ = slot->frame;
	rollbackEnd_ = frame_;
	if (!slot->copied) {
		// Never expected (Save's rule): a load the rule says cannot come. The
		// state is not restored, and the session ends rather than play on
		// from the wrong one.
		stats_.skippedLoads++;
		CoreLog("session: GGPO loads frame %d, whose save kept no copy (the session at frame %d, confirmed through %d)",
			slot->frame, frame_, confirmed_);
		Fail("load of a save that kept no copy");
		return false;
	}
	StateLayout& layout = game_->Layout();
	layout.LoadFrom(slot->bytes.data());
	game_->AfterLoad();
	if (FailedByGame()) return false; // the restore did not reach all of the game

	const int depth = frame_ - slot->frame;
	stats_.rollbacks++;
	if (depth > stats_.longestRollback) stats_.longestRollback = depth;
	frame_ = slot->frame;
	rollbackHud_.Begin(GetTickCount64());

	// The save's bytes are as Save wrote them (nothing writes a slot in use),
	// so their hash now is the save's hash.
	loads_++;
	if (config_.verifyLoads || (config_.verifyLoadsEvery > 0 && loads_ % (uint64_t)config_.verifyLoadsEvery == 0)) {
		stats_.loadsVerified++;
		if (layout.HashLive() != layout.HashSaved(slot->bytes.data())) {
			stats_.loadMismatches++;
			CoreLog("session: frame %d restored with a different hash", slot->frame);
		}
	}
	return true;
}

void RollbackSession::Free(void* buffer)
{
	Slot* slot = static_cast<Slot*>(buffer);
	if (!slot) return;
	// A save GGPO lets go of before its frame was hashed. HashConfirmed runs
	// after every GGPO call that confirms frames, so one that is final now
	// was hashed already, but for certain it is hashed here. One a rollback
	// replaces (GGPO keeps one save a frame, and a rollback saves each frame
	// after the one it loads again) held the guessed timeline: the new save
	// is hashed instead. As GGPO closes, the frames still not final are left
	// unhashed: no peer compares a frame before it is confirmed.
	if (slot->pending && !closing_) {
		if (!Replaced(slot->frame) && Final(slot->frame)) {
			HashPending(*slot);
		} else {
			stats_.hashesDropped++;
			game_->Layout().CountDroppedHash();
		}
	}
	slot->used = slot->pending = slot->copied = false;
}

// The frame about to be advanced: when the game ignores its inputs, GGPO is
// told (no misprediction on it) and the game gets neutral inputs but for the
// bits it reads confirmed (Step waited for them), so what the players'
// copies happened to send cannot change the simulation; when it reads them,
// only the bits a pad can produce reach it.
FrameInputs RollbackSession::InputsForThisFrame(const FrameInputs& inputs)
{
	ggpo_set_inputs_ignored(session_, !game_->InputsMatter());
	return GivenAndReported(inputs);
}

FrameInputs RollbackSession::GivenAndReported(const FrameInputs& inputs)
{
	const bool matter = game_->InputsMatter();
	const FrameInputs given = GivenInputs(*game_, inputs, frame_);
	Report(given, matter ? RecordedFrame::kFight : 0);
	if (!matter && given != FrameInputs{}) {
		CoreLog("session: frame %d gives the game confirmed bits %08x %08x", frame_, given.p[0], given.p[1]);
	}
	return given;
}

bool RollbackSession::AdvanceFromRollback()
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	if (!session_) return false;
	FrameInputs inputs;
	int disconnectFlags = 0;
	if (!failed_ && ggpo_synchronize_input(session_, inputs.p, sizeof(inputs.p), &disconnectFlags) != GGPO_OK) {
		Fail("synchronize_input failed during a rollback");
	}
	if (failed_) {
		// The session failed during this rollback (a restore the game could
		// not complete, say) and closes once GGPO returns: the game plays no
		// more of it. GGPO still counts the frames back to where the rollback
		// started, and exits the process if they do not add up.
		ggpo_advance_frame(session_);
		return false;
	}
	if (Held(frame_)) {
		// Held frames are stepped confirmed, so none should ever be replayed;
		// if one is, the game still stands still on it.
		ggpo_set_inputs_ignored(session_, true);
		Report(inputs, RecordedFrame::kHeld);
		if (stats_.heldReplays++ == 0) CoreLog("session: a rollback replayed held frame %d", frame_);
		ggpo_advance_frame(session_);
		return true;
	}
	game_->Advance(InputsForThisFrame(inputs), true);
	stats_.resimulatedFrames++;
	rollbackHud_.Replayed(GetTickCount64());
	ggpo_advance_frame(session_);
	return true;
}

void RollbackSession::Event(const GGPOEvent& info)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	switch (info.code) {
	case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER:
		// A watcher's wait for synchronization starts once a fighter is
		// synchronized: before that the fight cannot start anyway.
		if (WatcherHandle(info.u.synchronized.player)) {
			policy_.OnSynchronized(info.u.synchronized.player);
			CoreLog("session: the watcher with handle %d is synchronized", info.u.synchronized.player);
		} else {
			policy_.OnFighterSynchronized(GetTickCount64());
		}
		break;
	case GGPO_EVENTCODE_RUNNING:
		policy_.OnRunning(GetTickCount64());
		streamWatch_.OnRunning();
		gate_.OnRunning();
		CoreLog("session: running");
		break;
	case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
		// A watcher's link is not the fight's: GGPO has stopped streaming to
		// it, and the fight goes on.
		if (WatcherHandle(info.u.disconnected.player)) {
			policy_.OnDisconnected(info.u.disconnected.player);
			links_.Dropped(info.u.disconnected.player);
			watcherHandles_.erase(info.u.disconnected.player);
			CoreLog("session: the watcher with handle %d disconnected%s", info.u.disconnected.player,
				keptForWatchers_ ? "" : "; the fight goes on");
			break;
		}
		// Kept only for the watchers: the other fighter leaving is expected.
		if (keptForWatchers_) break;
		peerDisconnected_ = true;
		disconnectTimeoutMs_ = 0;
		Fail(config_.watching ? "P1's stream ended" : "the other player disconnected");
		break;
	case GGPO_EVENTCODE_CONNECTION_INTERRUPTED:
		if (WatcherHandle(info.u.connection_interrupted.player) || keptForWatchers_) {
			if (!keptForWatchers_) {
				CoreLog("session: the watcher with handle %d is not answering", info.u.connection_interrupted.player);
			}
			break;
		}
		// The countdown runs from the episode's first report, as SF4's does.
		if (gate_.OnConnectionInterrupted(GetTickCount())) {
			disconnectTimeoutMs_ = info.u.connection_interrupted.disconnect_timeout;
			CoreLog("session: connection interrupted (dropped in %d ms without a reply)", disconnectTimeoutMs_);
		}
		break;
	case GGPO_EVENTCODE_CONNECTION_RESUMED:
		if (WatcherHandle(info.u.connection_resumed.player)) break;
		if (gate_.OnConnectionResumed()) {
			disconnectTimeoutMs_ = 0;
			CoreLog("session: connection resumed");
		}
		break;
	case GGPO_EVENTCODE_TIMESYNC:
		// A fresh estimate, not a delta (Ember's PacingController notes).
		stats_.timesyncFramesAhead = info.u.timesync.frames_ahead;
		stats_.timesyncEvents++;
		break;
	default:
		break;
	}
}

} // namespace sfve
