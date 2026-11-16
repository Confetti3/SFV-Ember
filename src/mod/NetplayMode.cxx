#include "mod/NetplayMode.hxx"

#include "ember/DesyncReporter.hxx"
#include "ember/DesyncTrace.hxx"
#include "ember/EmberHost.hxx"
#include "ember/RoomCommands.hxx"
#include "ember/SfvFrameMeter.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvReplays.hxx"
#include "ember/SfvShell.hxx"
#include "ember/SfvTraining.hxx"
#include "ember/SfvVerification.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameExit.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/VersusLaunch.hxx"

#include "SpectatorCatchUp.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include <optional>

namespace sfve::mod {

namespace {

const char* StepName(RollbackSession::StepResult r)
{
	switch (r) {
	case RollbackSession::StepResult::NoSession: return "no session";
	case RollbackSession::StepResult::Waiting: return "waiting";
	case RollbackSession::StepResult::Advanced: return "advanced";
	case RollbackSession::StepResult::PredictionStall: return "prediction stall";
	case RollbackSession::StepResult::Skipped: return "skipped";
	case RollbackSession::StepResult::Failed: return "failed";
	}
	return "?";
}

} // namespace

void NetplayMode::Install()
{
	RegisterCommands();
	// The mode is never destroyed (the driver lives as long as the process):
	// its sessions close in the game's exit work, with every thread still
	// running, after the room's leave has gone out (EmberHost's steps).
	OnGameExit({nullptr, nullptr, [this] {
		std::lock_guard lock(mutex_);
		EndDrain();
		if (session_ && session_->Live()) session_->Close();
	}});
	if (!config_.room) return;
	adapter::RegisterRoomCommands();
	adapter::RegisterReplayCommands();
	// The runtime ticks this mode for the process's life (the driver holds it).
	adapter::InstallEmberHost({config_.instance, config_.ownershipGate, config_.unowned, config_.uninstalled}, *this);
	adapter::InstallEmberShell();
	// SFV's Training under Ember: followed once a frame, after Ember's tick
	// has set the room's call (EMBER_LIBRARY.md section 27), with Ember's
	// frame meter fed after each of its ticks (section 39).
	adapter::InstallSfvTraining();
	adapter::InstallSfvFrameMeter();
}

void NetplayMode::NewBattle(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	// The last battle's watchers have had their time: the next session takes
	// the room's port.
	EndDrain();
	// A battle that never got back to the main menu: its replay is saved,
	// or its playback ends, before the next binds.
	ConfirmRecording();
	recording_.Finish();
	if (room_.Replaying()) adapter::EndPlayback("another battle began");
	auto session = std::make_unique<RollbackSession>(adapter);
	room_.Bind(session_.get(), *session, adapter, [this](const char* why) {
		Log("netplay: the room asks the battle to end (%s)", why);
		exitAsked_ = true;
	});
	session_ = std::move(session);
	// The pace line's counts (mod/Pace.hxx), read on the battle's thread
	// inside the session's own calls, under Frame's lock. Of the battle's
	// session, whichever that is now: none once it is kept for the watchers
	// (PollBattle), so the adapter never holds a session the drain frees.
	adapter.SetPaceSource([this]() {
		if (!session_) return SfvAdapter::PaceCounts{};
		const RollbackSession::Stats& s = session_->GetStats();
		return SfvAdapter::PaceCounts{session_->Frame(), s.rollbacks, s.waitingSteps + s.predictionStalls};
	});
	replay_.reset();
	replayEnd_.clear();
	seeker_.Reset();
	if (TakeReplayBattle()) {
		// The battle the Replays screen's Watch now launched.
		replay_ = adapter::TakeArmedReplay();
		if (replay_) room_.BindReplay();
		else Log("netplay: a replay's battle loaded with no replay to play; it plays offline");
	}
	started_ = leaveWanted_ = leaving_ = false;
	prepared_ = false;
	EndStartAgreement();
	startHash_ = 0;
	exitAsked_ = false;
	introSkip_ = {};
	scenes_.Reset();
	ForgetEndedVersusBattle();
	pacer_.Reset();
}

void NetplayMode::Frame(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	if (!session_) {
		// No battle bound since the last one's session was kept for its
		// watchers (PollBattle); the driver binds one before any frame.
		adapter.RunOriginal();
		return;
	}
	// The frames the last steps made final, for the replay.
	ConfirmRecording();
	if (replay_ && adapter::TakePlaybackStop()) {
		// Stop watching (View, Esc): the playback ends here.
		if (replayEnd_.empty()) replayEnd_ = "stopped by the player";
		exitAsked_ = true;
	}
	// The runtime retired or aborted the match. Over the fighters' held WIN
	// the exit rides in the held frames as this fighter's decline, so both
	// copies leave on the same confirmed frame (RoomBattle::DeclineToExit);
	// anywhere else no session goes on, and the battle goes back to the main
	// menu now. The ask stands while the WIN carries it: one that ends in a
	// rematch both had already chosen leaves on the next frame.
	if (exitAsked_ && !(session_->Live() && room_.DeclineToExit())) {
		exitAsked_ = false;
		room_.FlushHeld("the runtime ended the battle");
		if (session_->Live()) session_->Close();
		started_ = true;
		Leave();
	}
	if (leaveWanted_) {
		if (!leaving_) Leave();
		// The battle stands still until the main menu replaces it; the
		// session keeps answering meanwhile, so the other copy still gets the
		// inputs it may be missing for the last frames, and P1's watchers
		// theirs.
		if (session_->Live()) session_->Poll();
		adapter::RoomBattle::EndDroppedWatcherLinks(*session_);
		return;
	}
	if (!started_ && !StartSession(adapter)) return; // the battle waits for the match
	if (!session_->Live()) {
		if (room_.Active()) {
			// A room battle with no session (it broke, or never started) has
			// no other player: back to the main menu and the room.
			Log("netplay: no session (%s); back to the main menu", session_->FailureReason().c_str());
			if (replay_ && replayEnd_.empty()) replayEnd_ = session_->FailureReason();
			room_.NoteLink(*session_); // the opponent dropped, if they did
			room_.FlushHeld("the session ended");
			Leave();
			return;
		}
		// No session (none was asked for): the battle carries on offline
		// rather than freezing.
		adapter.RunOriginal();
		return;
	}
	room_.NoteSyncPhase(*session_);
	room_.NoteLink(*session_);
	adapter::RoomBattle::EndDroppedWatcherLinks(*session_);
	if (session_->Watching()) {
		// A replay steps as its transport says: paused, slower, a step, or a seek.
		if (replay_) return PlayReplay(adapter);
		return WatchFrames(adapter);
	}
	const bool frozen = dev_.Frozen(*session_);
	if (room_.Prompting() && !frozen) {
		// Over the WIN the battle stands still; held frames carry both
		// fighters' answers, each stepped once its inputs are confirmed, and
		// both copies decide on the same one (RoomBattle, EMBER_LIBRARY.md 19).
		const PackedInput answer = room_.AnswerBits();
		adapter::RoomBattle::Answer decided = adapter::RoomBattle::Answer::Wait;
		last_ = session_->StepHeld(answer, [&](const FrameInputs& inputs) {
			decided = room_.HeldFrame(inputs, session_->HeldFrames(), adapter, seed_);
		});
		if (last_ != RollbackSession::StepResult::Advanced) return;
		room_.Decided(decided, *session_);
		dev_.DumpIfRequested(session_.get(), config_.instance);
		if (decided == adapter::RoomBattle::Answer::Rematch) {
			startTime_ = adapter.StartTime();
			// The restart keeps the last match decided for a few frames: the
			// answer's button, still down, is no press to skip anything.
			introSkip_ = {};
		} else if (decided == adapter::RoomBattle::Answer::Leave) {
			recording_.MarkWhole();
			exitAsked_ = false; // a runtime's exit the WIN carried is this leave
			Leave(); // both copies leave on this same confirmed frame
		}
		return;
	}
	// A room match that ended is decided only on a confirmed frame: nothing
	// before the decision can be rolled back then, on either copy (MatchEnd).
	const bool endPending = room_.EndPending();
	if (frozen || (endPending && session_->LastConfirmedFrame() < session_->Frame())) {
		session_->Poll(); // held frames still receive inputs
		dev_.DumpIfRequested(session_.get(), config_.instance); // and get confirmed
		return;
	}
	if (session_->Running()) pacer_.Frame(*session_, last_ == RollbackSession::StepResult::PredictionStall);
	const int frame = session_->Frame();
	scenes_.Note(adapter, frame);
	// Sent as the player plays, with the skip request of the intro, of the
	// scenes between two rounds and of a decided match's (IntroSkip.hxx);
	// frames whose inputs the game ignores (outside the fight) get neutral
	// ones in RollbackSession, whatever was sent, but for the request on the
	// skip frames, which are stepped confirmed.
	const bool asked = introSkip_.Requested();
	const PackedInput local = introSkip_.Next(adapter.FlowState(), LocalInput(adapter, frame), adapter.MatchDecided(), frame);
	if (!asked && introSkip_.Requested() && scenes_.From() >= 0) {
		Log("netplay: this copy asks to skip the scenes from frame %d, %d frame(s) into them", frame, scenes_.Shown(frame));
	}

	// The game's judgement, the same on both copies (it is restored with the
	// rest of the state on rollback): both copies hold the battle for the
	// WIN's answers from this same confirmed frame.
	if (endPending && room_.TakeEnd(adapter, *session_)) {
		scenes_.End(session_->Frame());
		recording_.NoteGame(session_->Frame());
		return;
	}
	// The match's start time lies outside the rollback state, and a match
	// data reset takes it from the clock; the fighters' generators are
	// reseeded from it at each round's start. It stays the session's.
	room_.NoteFightFrame();
	NoteFightStart(adapter, frame);
	if (room_.Active() && adapter.StartTime() != startTime_) {
		Log("netplay: the match's start time became %u at frame %d; set back to %u", adapter.StartTime(), frame,
			startTime_);
		adapter.SetStartTime(startTime_);
	}
	adapter.NoteUpdateDue();
	last_ = session_->Step(local);
	// The frame just played (the session's next is one on).
	scenes_.Skip(adapter, session_->Frame() - 1);
	dev_.DumpIfRequested(session_.get(), config_.instance);
	dev_.PerturbIfDue(session_.get());
}

PackedInput NetplayMode::LocalInput(SfvAdapter& adapter, int frame)
{
	if (!intro_skip::Scenes(adapter.FlowState(), adapter.MatchDecided())) {
		// A scripted player's blocks of moves face the way its fighter does.
		const std::optional<bool> facing = config_.script.seed ? adapter.FacingRight(session_->LocalPlayer()) : std::nullopt;
		return script_.At(frame, facing).value_or(adapter.Sampled(0));
	}
	// The scenes (between two rounds, or a decided match's) read nothing but
	// the skip request, so only a press counts here: the pad as the battle
	// records it and, should that record stand still, as the game asks its
	// pads itself (its own way after the decision, unhooked). A test's
	// script presses only where "skipafter" says, so a scripted match plays
	// its scenes whole unless asked.
	PackedInput local = adapter.Sampled(0);
	if (adapter.LocalPadsAny(intro_skip::kPressBits)) local |= intro_skip::kSkipBit;
	if (intro_skip::ScriptedPress(scenes_.From(), frame, dev_.SkipAfter())) local |= input::kLP;
	return local;
}

bool NetplayMode::GamePauseAllowed() const
{
	// Called from the battle's update inside Frame, on the game thread that
	// holds mutex_, so the members are read as Frame sees them.
	return !(room_.Active() || (session_ && session_->Live()));
}

void NetplayMode::WatchFrames(SfvAdapter& adapter)
{
	const bool advanced = WatchFrame(adapter);
	if (replay_) dev_.DumpIfRequested(session_.get(), config_.instance); // "dump <frame>" during a playback
	if (!advanced) return;
	// The backlog after this update's own frame, as SF4's CatchUpSpectator
	// reads it; each extra frame goes through the same decisions, so a frame
	// the fighters held is held here too.
	const int extra = sf4e::SpectatorCatchUp::ExtraFrames(session_->WatchedBacklog());
	for (int played = 0; played < extra && !leaveWanted_ && session_->Live(); played++) {
		if (!WatchFrame(adapter)) return;
	}
}

bool NetplayMode::WatchFrame(SfvAdapter& adapter)
{
	if (dev_.Frozen(*session_)) {
		session_->Poll();
		return false;
	}
	if (room_.Prompting()) {
		// The fighters' answers, from the held frames P1 streamed: the same
		// decision on the same frame as theirs (RoomBattle).
		adapter::RoomBattle::Answer decided = adapter::RoomBattle::Answer::Wait;
		last_ = session_->StepHeld(0, [&](const FrameInputs& inputs) {
			decided = room_.HeldFrame(inputs, session_->HeldFrames(), adapter, seed_);
		});
		if (last_ != RollbackSession::StepResult::Advanced) return false;
		room_.Decided(decided, *session_);
		if (replay_) adapter::NotePlaybackFrame(session_->Frame() - 1, false, room_.GamesInPlace());
		if (decided == adapter::RoomBattle::Answer::Rematch) {
			startTime_ = adapter.StartTime();
		} else if (decided == adapter::RoomBattle::Answer::Leave) {
			recording_.MarkWhole();
			if (replay_ && replayEnd_.empty()) replayEnd_ = "the recording's last WIN went back";
			Leave();
		}
		return true;
	}
	// Every frame a watcher has is confirmed, so the match's end is taken on
	// the frame it is seen, the one the fighters take it on.
	scenes_.Note(adapter, session_->Frame());
	if (room_.EndPending() && room_.TakeEnd(adapter, *session_)) {
		scenes_.End(session_->Frame());
		recording_.NoteGame(session_->Frame());
		return false;
	}
	if (adapter.StartTime() != startTime_) {
		Log("netplay: the match's start time became %u at frame %d; set back to %u", adapter.StartTime(),
			session_->Frame(), startTime_);
		adapter.SetStartTime(startTime_);
	}
	NoteFightStart(adapter, session_->Frame());
	adapter.NoteUpdateDue();
	// The state at the start of a fight frame, when a seek back may need it.
	if (replay_) seeker_.Before(*session_, replay_->frames);
	last_ = session_->Step(0);
	scenes_.Skip(adapter, session_->Frame() - 1);
	const bool advanced = last_ == RollbackSession::StepResult::Advanced;
	if (advanced && replay_) {
		const int played = session_->Frame() - 1;
		adapter::NotePlaybackFrame(played, played >= 0 && replay_->frames[(size_t)played].Fight(), room_.GamesInPlace());
	}
	return advanced;
}

void NetplayMode::PlayReplay(SfvAdapter& adapter)
{
	// A lagged or rollback-checked playback (a test's) plays its own rollbacks: it never seeks.
	const bool diagnostics = config_.replayLag > 0 || config_.replayCheck > 0;
	const adapter::PlaybackTurn turn = adapter::PlaybackTurnFor(seeker_.Ground(*session_, replay_->frames, diagnostics));
	if (turn.restoreBefore >= 0) {
		const int restored = seeker_.Restore(*session_, turn.restoreBefore);
		adapter::NotePlaybackRestored(restored);
		if (restored < 0) return;
	}
	if (turn.goal < 0) {
		if (turn.frames) WatchFrames(adapter);
		return;
	}
	for (unsigned played = 0; played < sf4e::replaytransport::kMostSeekFrames && session_->Frame() < turn.goal &&
		!leaveWanted_ && session_->Live(); played++) {
		if (!WatchFrame(adapter)) return;
	}
}

void NetplayMode::EndDrain()
{
	if (!drain_) return;
	adapter::SetWatcherDrain(nullptr);
	if (drain_->Live()) drain_->Close();
	drain_.reset();
}

void NetplayMode::PollBattle()
{
	std::lock_guard lock(mutex_);
	if (drain_) {
		// The runtime ends the drain once every watcher has left or its time
		// is up; until then the session sends what the watchers still miss.
		if (drain_->Live() && sf4e::NetplayFacade::DrainingSpectators()) {
			drain_->Poll();
			adapter::RoomBattle::EndDroppedWatcherLinks(*drain_);
		} else {
			Log("netplay: the session kept for the watchers is done");
			EndDrain();
		}
	}
	if (!room_.Active() || !MainMenuInFront()) return;
	ConfirmRecording();
	const bool replayed = room_.Replaying();
	if (room_.Close(session_.get())) {
		drain_ = std::move(session_);
		adapter::SetWatcherDrain(drain_.get());
	}
	if (replayed) {
		adapter::EndPlayback(replayEnd_.empty() ? "the battle went back to the main menu" : replayEnd_);
		replay_.reset();
	}
	recording_.Finish();
	leaveWanted_ = leaving_ = false;
}

void NetplayMode::ConfirmRecording()
{
	recording_.Confirm(session_ ? session_.get() : drain_.get());
}

void NetplayMode::Leave()
{
	EndStartAgreement(); // a battle left while its copies compared start states
	if (!leaveWanted_) SetLinkedMatchEnd(false);
	const bool retry = leaveWanted_;
	leaveWanted_ = true;
	leaving_ = LeaveBattle();
	if (!leaving_ && !retry) Log("netplay: could not leave the battle yet (no scene manager); asking again each frame");
}

void NetplayMode::NoteFightStart(SfvAdapter& adapter, int frame)
{
	// Called from Frame and WatchFrame, which hold mutex_ for the whole step:
	// the "vital" command's shares, which the pipe thread sets under it, are
	// read under it here too (locking again would deadlock).
	if (!scenes_.FightStart(adapter, frame)) return;
	dev_.GiveVital(adapter, frame);
}

std::string NetplayMode::Describe() const
{
	std::lock_guard lock(mutex_);
	if (!session_) return "null";
	const RollbackSession::Stats& s = session_->GetStats();
	char text[512];
	snprintf(text, sizeof(text),
		"{\"room\":%s,\"live\":%s,\"running\":%s,\"failure\":%s,\"frame\":%d,\"confirmed\":%d,\"last\":\"%s\","
		"\"advanced\":%llu,\"rollbacks\":%llu,\"resimulated\":%llu,\"longestRollback\":%d,\"stalls\":%llu,"
		"\"pacing\":",
		room_.Active() ? "true" : "false", session_->Live() ? "true" : "false", session_->Running() ? "true" : "false",
		JsonString(session_->FailureReason()).c_str(), session_->Frame(), session_->LastConfirmedFrame(),
		StepName(last_), (unsigned long long)s.framesAdvanced, (unsigned long long)s.rollbacks,
		(unsigned long long)s.resimulatedFrames, s.longestRollback, (unsigned long long)s.predictionStalls);
	// The link as the HUD reads it (RollbackSession's ConnectionWarning and
	// the rest), and the watchers: whether this copy watches, its backlog of
	// P1's stream, and P1's watcher streams (also those of the session kept
	// for them after the battle).
	char link[320];
	snprintf(link, sizeof(link),
		",\"link\":{\"warning\":%s,\"stalled\":%s,\"countdownMs\":%d,\"delay\":%d,\"recentRollback\":%u,\"peerDropped\":%s,"
		"\"watching\":%s,\"backlog\":%d,\"watchers\":%zu,\"keptForWatchers\":%zu}",
		session_->ConnectionWarning() ? "true" : "false", session_->PredictionStalled() ? "true" : "false",
		session_->DisconnectCountdownMs(), session_->AppliedDelay(), session_->RecentRollbackFrames(),
		session_->PeerDisconnected() ? "true" : "false", session_->Watching() ? "true" : "false",
		session_->WatchedBacklog(), session_->WatcherStreams(), drain_ ? drain_->WatcherStreams() : (size_t)0);
	return text + pacer_.Describe() + ",\"lastResult\":" + room_.LastResult() + ",\"rematch\":" + room_.DescribeRematch() +
		",\"heldFrames\":" + std::to_string(s.heldFrames) + ",\"heldReplays\":" + std::to_string(s.heldReplays) + link + "}";
}

void NetplayMode::RegisterCommands()
{
	dev_.Register(mutex_, session_, pacer_);
}

} // namespace sfve::mod
