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
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/SfvFighterOffsets.hxx"
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
	FinishRecording();
	if (room_.Replaying()) adapter::EndPlayback("another battle began");
	auto session = std::make_unique<RollbackSession>(adapter);
	room_.Bind(session_.get(), *session, adapter, [this](const char* why) {
		Log("netplay: the room asks the battle to end (%s)", why);
		exitAsked_ = true;
	});
	session_ = std::move(session);
	replay_.reset();
	replayEnd_.clear();
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
	scenes_ = {};
	scenesSkips_ = 0;
	roundScenes_ = false;
	fightSeen_ = false;
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
		// A replay steps as its transport says: paused, slower, or a step.
		if (replay_ && !adapter::PlaybackFrameWanted()) return;
		return WatchFrames(adapter);
	}
	const bool frozen = freezeFrame_ >= 0 && session_->Frame() >= freezeFrame_;
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
		DumpIfRequested();
		if (decided == adapter::RoomBattle::Answer::Rematch) {
			startTime_ = adapter.StartTime();
			// The restart keeps the last match decided for a few frames: the
			// answer's button, still down, is no press to skip anything.
			introSkip_ = {};
		} else if (decided == adapter::RoomBattle::Answer::Leave) {
			recordingWhole_ = true;
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
		DumpIfRequested(); // and get confirmed
		return;
	}
	if (session_->Running()) pacer_.Frame(*session_, last_ == RollbackSession::StepResult::PredictionStall);
	const int frame = session_->Frame();
	NoteScenes(adapter, frame);
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
		NoteScenesEnd();
		NoteRecordedGame();
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
	last_ = session_->Step(local);
	NoteScenesSkip(adapter);
	DumpIfRequested();
	PerturbIfDue();
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
	if (intro_skip::ScriptedPress(scenes_.From(), frame, skipAfter_)) local |= input::kLP;
	return local;
}

void NetplayMode::NoteScenes(SfvAdapter& adapter, int frame)
{
	const int before = scenes_.From();
	const bool decided = adapter.MatchDecided();
	scenes_.Note(intro_skip::Scenes(adapter.FlowState(), decided), frame);
	if (before < 0 && scenes_.From() >= 0) {
		// A round that does not decide the match ends in the scenes between
		// two rounds; the deciding one's are the match's (the decision is
		// made in the KO's own tick, before Finish).
		scenesSkips_ = 0;
		roundScenes_ = !decided;
		if (roundScenes_) {
			Log("netplay: a round is over at frame %d (flow state %d, %u input tick(s) in the last update); its scenes "
				"play to the next round's call", frame, adapter.FlowState(), adapter.TicksInLastUpdate());
		} else {
			Log("netplay: the match is decided at frame %d (flow state %d, %u input tick(s) in the last update); its "
				"scenes play to the result", frame, adapter.FlowState(), adapter.TicksInLastUpdate());
		}
	} else if (before >= 0 && scenes_.From() < 0) {
		if (roundScenes_ && adapter.FlowState() == sfv::flow::kFight) {
			// A rollback took back the KO this copy guessed: the fight again.
			Log("netplay: the round's end seen at frame %d was taken back by a rollback at frame %d", before, frame);
		} else if (roundScenes_) {
			Log("netplay: the round's scenes took %d frame(s), from the round's end at frame %d to the next round at "
				"frame %d, flow state %d (%d skip(s) applied)", frame - before, before, frame, adapter.FlowState(), scenesSkips_);
		} else {
			Log("netplay: the match is no longer decided at frame %d (flow state %d): a rematch in place, or a rollback "
				"took the deciding hit back", frame, adapter.FlowState());
		}
	}
}

void NetplayMode::NoteScenesSkip(SfvAdapter& adapter)
{
	const char* skipped = adapter.TakeScenesSkip();
	if (!skipped) return;
	scenesSkips_++;
	// The frame just played (the session's next is one on).
	Log("netplay: scenes skipped at frame %d: %s", session_->Frame() - 1, skipped);
}

void NetplayMode::NoteScenesEnd()
{
	if (scenes_.From() < 0) return;
	const int frame = session_->Frame();
	Log("netplay: the scenes took %d frame(s), from the decision at frame %d to the result at frame %d (%d skip(s) applied)",
		scenes_.Shown(frame), scenes_.From(), frame, scenesSkips_);
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
	if (replay_) DumpIfRequested(); // "dump <frame>" during a playback
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
	if (freezeFrame_ >= 0 && session_->Frame() >= freezeFrame_) {
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
		if (replay_) adapter::NotePlaybackFrame(false, room_.GamesInPlace());
		if (decided == adapter::RoomBattle::Answer::Rematch) {
			startTime_ = adapter.StartTime();
		} else if (decided == adapter::RoomBattle::Answer::Leave) {
			recordingWhole_ = true;
			if (replay_ && replayEnd_.empty()) replayEnd_ = "the recording's last WIN went back";
			Leave();
		}
		return true;
	}
	// Every frame a watcher has is confirmed, so the match's end is taken on
	// the frame it is seen, the one the fighters take it on.
	NoteScenes(adapter, session_->Frame());
	if (room_.EndPending() && room_.TakeEnd(adapter, *session_)) {
		NoteScenesEnd();
		NoteRecordedGame();
		return false;
	}
	if (adapter.StartTime() != startTime_) {
		Log("netplay: the match's start time became %u at frame %d; set back to %u", adapter.StartTime(),
			session_->Frame(), startTime_);
		adapter.SetStartTime(startTime_);
	}
	NoteFightStart(adapter, session_->Frame());
	last_ = session_->Step(0);
	NoteScenesSkip(adapter);
	const bool advanced = last_ == RollbackSession::StepResult::Advanced;
	if (advanced && replay_) {
		const int played = session_->Frame() - 1;
		adapter::NotePlaybackFrame(played >= 0 && replay_->frames[(size_t)played].Fight(), room_.GamesInPlace());
	}
	return advanced;
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
	FinishRecording();
	leaveWanted_ = leaving_ = false;
}

void NetplayMode::ConfirmRecording()
{
	// Played, and confirmed on both fighters' copies (a watcher's every frame
	// is): no rollback reports these frames again.
	const RollbackSession* session = session_ ? session_.get() : drain_.get();
	if (!recorder_.Active() || !session || !session->Live()) return;
	recorder_.Confirm(std::min(session->Frame(), session->LastConfirmedFrame() + 1));
}

void NetplayMode::NoteRecordedGame()
{
	if (!recorder_.Active()) return;
	const std::optional<MatchResult> result = DecidedMatchResult();
	replay::Game game;
	if (result) {
		game.decided = result->decided;
		game.winner = result->winner;
		game.wins[0] = result->wins[0];
		game.wins[1] = result->wins[1];
		game.rounds = result->rounds;
		game.bestOf = result->bestOf;
	}
	game.endFrame = session_->Frame();
	recording_.games.push_back(game);
}

void NetplayMode::FinishRecording()
{
	if (!recorder_.Active()) return;
	recorder_.Stop();
	if (recorder_.Broken()) {
		Log("netplay: the battle's frames came out of order; its replay is not saved");
		return;
	}
	replay::Replay r;
	r.header = recording_;
	r.header.complete = recordingWhole_;
	recorder_.Take(&r.frames, &r.checkpoints);
	if (r.frames.empty()) {
		Log("netplay: the battle played no frame; no replay of it");
		return;
	}
	adapter::SaveReplay(std::move(r));
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

bool NetplayMode::StartSession(SfvAdapter& adapter)
{
	if (!prepared_) {
		switch (PrepareStart(adapter)) {
		case Prepared::Waiting: return false;
		case Prepared::NoSession: started_ = true; return true;
		case Prepared::Ready: break;
		}
	}
	// Every copy stands still at its start state until they agree on it
	// (core/StartAgreement.hxx); the battle waits meanwhile.
	if (startAgreement_ && !AgreeStart(adapter)) return false;
	started_ = true;
	RollbackSession::Config& c = pending_.config;
	const bool practice = pending_.practice;
	const uint32_t seed = pending_.seed;
	// A room battle is recorded from its first frame (EMBER_LIBRARY.md
	// section 26): the session reports each frame it plays to the recorder.
	// A watched one only with Settings' "Save matches you watch", and never a
	// Practice room's (its refilling gauges are set outside the frames, and
	// it reaches no result), as SF4's (UserApp's recordReplay).
	const bool record = room_.Active() && !replay_ && !practice &&
		(!room_.Watching() || sf4e::NetplayFacade::GetRuntimeSnapshotShared()->preferences.recordWatched);
	if (record) {
		recorder_.Start();
		recording_ = {};
		recordingWhole_ = false;
		c.onCheckpoint = [this](int frame, uint64_t hash) { recorder_.OnCheckpoint(frame, hash); };
	}
	// A desync's report needs the inputs of the frames before it whether or not
	// the battle is recorded (ember/DesyncTrace.hxx), so every room battle's
	// frames go to the trace as well; the recorder's call is as it was.
	sfve::adapter::Trace().Reset();
	if (room_.Active()) {
		c.onFrame = [this, record](int frame, const RecordedFrame& played) {
			if (record) recorder_.OnFrame(frame, played);
			sfve::adapter::Trace().NoteFrame(frame, played);
		};
	}
	std::string error;
	const bool started = session_->Start(c, &error);
	if (!started) Log("netplay: could not start: %s", error.c_str());
	if (room_.Active() && !replay_) room_.NoteSessionStart(started);
	if (record) {
		// The room's names for both sides are known once the start is noted.
		const int recordedDelay = room_.Watching() ? room_.WatchedDelay() : c.inputDelay;
		if (!started || !adapter::RecordingHeader((int)room_.LocalSlot(), seed, recordedDelay, &recording_)) {
			if (started) Log("netplay: the room's match is not known; this battle is not recorded");
			recorder_.Stop();
		}
		// P1's start state, which a playback compares its own with.
		recording_.startHash = startHash_;
	}
	// The results menu would let each player leave the match their own way.
	// A replay's battle holds its WIN as the room's did.
	SetLinkedMatchEnd(room_.Active() && session_->Live());
	return true;
}

NetplayMode::Prepared NetplayMode::PrepareStart(SfvAdapter& adapter)
{
	script_ = input::ScriptedInput(config_.script, config_.player);
	RollbackSession::Config c;
	uint32_t seed = kSessionSeed;
	int delay = config_.delay;
	if (replay_) {
		// A replay: its own frames, seed and checkpoints, and no network.
		c.replay = &replay_->frames;
		c.onCheckpoint = [this](int frame, uint64_t hash) {
			adapter::NotePlaybackCheckpoint(frame, hash);
			if (config_.replayRegionsFrom > 0 && frame >= config_.replayRegionsFrom) adapter::LogCheckpointRegions(*session_, frame);
		};
		c.replayCheckDistance = config_.replayCheck;
		c.replayLag = config_.replayLag;
		c.replayLagPlayer = config_.replayLagPlayer;
		// Each playback in this process its own schedule.
		static uint32_t s_playbacks = 0;
		if (config_.replayLagJitter) c.replayLagJitter = config_.replayLagJitter + s_playbacks++;
		c.replayCheckSeed = config_.scheduleSeed;
		seed = replay_->header.seed;
		delay = 0;
	} else if (room_.Active()) {
		switch (room_.SessionConfig(&c, &seed, &delay)) {
		case adapter::RoomBattle::Start::Waiting: return Prepared::Waiting;
		case adapter::RoomBattle::Start::Gone: return Prepared::NoSession;
		case adapter::RoomBattle::Start::Ready: break;
		}
	} else if (config_.room) {
		// A battle of the player's own while rooms are on: it plays offline.
		return Prepared::NoSession;
	} else {
		c.localPlayer = config_.player;
		c.localPort = config_.port;
		c.remotePort = config_.peerPort;
	}
	prepared_ = true;
	// At least one frame: a decision held for a confirmed frame (match end)
	// waits for the other side's input for the next frame, which it has
	// always sent by then.
	c.inputDelay = delay < 1 ? 1 : delay;
	if (config_.hashEvery > 0) {
		c.hashEveryFrames = config_.hashEvery;
		c.keepAllHashes = true;
	}
	// Cheaper rollbacks (docs/design/beta8/perf.md); a replay's playback
	// makes no GGPO saves, so they change nothing there.
	c.skipUnloadableSaves = config_.skipSaves;
	c.hashOnConfirm = config_.hashOnConfirm;
	c.verifyLoadsEvery = config_.verifyLoadsEvery;
	// Both copies are at the battle's first frame: give the clock-seeded
	// random generators the same seeds before anything is simulated.
	Log("netplay: reseeded %d random generator(s)", adapter.ReseedRandom(seed));
	// What the battle's ticks before the session recorded for each player,
	// which the session's first frames read back: nothing pressed for a
	// room's or a replay's battle on every copy (BattleDriver). Anything else
	// is logged, as the two copies would start from different inputs.
	for (int player = 0; player < kPlayers; player++) {
		if (const PackedInput held = adapter.RecordedInputs(player)) {
			Log("netplay: P%d's input record holds %08x before frame 0", player + 1, held);
		}
	}
	// A Practice room's game: Training's refilling gauges on both copies at
	// this same frame, before anything is simulated (EMBER_LIBRARY.md 27).
	// A test's -sfve_practice does the same outside rooms.
	const bool practice = room_.Practice() || config_.practiceGauges;
	if (practice && !adapter.SetPracticeGauges()) Log("netplay: the Practice match plays with a match's gauges");
	seed_ = seed;
	startTime_ = adapter.StartTime();
	// A test's difference in this copy's start state (-sfve_start_perturb),
	// for the agreement below to find and repair.
	PerturbStart(adapter);
	pending_ = {c, seed, practice};
	startHash_ = 0;
	if (replay_) {
		// No peer to agree with: the recording says what P1 started from.
		adapter::NotePlaybackStart(replay_->header.startHash, adapter.Layout().HashLive(HashScope::Peers));
	} else {
		BeginStartAgreement(adapter);
	}
	return Prepared::Ready;
}

void NetplayMode::BeginStartAgreement(SfvAdapter& adapter)
{
	const RollbackSession::Config& c = pending_.config;
	StartAgreement::Config agreement;
	agreement.token = pending_.seed;
	// "game.N" is a place in the game's table, which two processes order
	// differently; every other region name is its owner's (RegionNames,
	// SfvLayoutRules) and the same in every process.
	agreement.stableName = [](const std::string& name) { return name.rfind("game.", 0) != 0; };
	std::vector<uint16_t> ports{c.remotePort};
	if (!c.watching && c.localPlayer == 0) {
		// P1: P2 first, then each watcher it streams to.
		agreement.role = StartAgreement::Role::Reference;
		agreement.requiredPeers = 1;
		for (const RollbackSession::Config::Watcher& w : c.watchers) ports.push_back(w.port);
	} else {
		agreement.role = StartAgreement::Role::Follower;
		// P1 drops a watcher that has not synchronized 5 s after a fighter
		// has (SpectatorPolicy::SyncDeadlineMs), and serves watchers only a
		// second after P2 is done: a watcher gives up sooner than a fighter.
		if (c.watching) agreement.timeLimitMs = 3000;
	}
	auto channel = std::make_unique<UdpStartChannel>();
	if (!channel->Open(c.localPort, c.remoteIp, ports)) {
		Log("netplay: no start state agreement on port %u; the session starts as it is", c.localPort);
		return;
	}
	startChannel_ = std::move(channel);
	startAgreement_ = std::make_unique<StartAgreement>(adapter, *startChannel_, std::move(agreement));
	startAgreement_->Begin(GetTickCount64(), ports.size());
}

bool NetplayMode::AgreeStart(SfvAdapter& adapter)
{
	if (!startAgreement_->Pump(GetTickCount64())) return false;
	const StartAgreement::Report& report = startAgreement_->GetReport();
	startHash_ = report.reference ? report.reference : report.localBefore;
	// A repair is reported as a desync is (it was one, before frame 0).
	if (!report.regions.empty() && room_.Active()) {
		adapter::ReportStartRepair(report, adapter.Layout(), room_.Watching(), pending_.config.inputDelay);
	}
	if (const char* failure = adapter.StateFailure()) Log("netplay: after the start state's agreement: %s", failure);
	EndStartAgreement(); // the port is GGPO's from here
	return true;
}

void NetplayMode::EndStartAgreement()
{
	startAgreement_.reset();
	startChannel_.reset();
}

void NetplayMode::PerturbStart(SfvAdapter& adapter)
{
	if (config_.startPerturb.empty()) return;
	char region[96] = {};
	size_t offset = 0;
	int32_t add = 0;
	// "<region>,<offset>,<add>": a region name has no comma.
	const std::string& text = config_.startPerturb;
	const size_t first = text.find(','), second = text.find(',', first + 1);
	if (first == std::string::npos || second == std::string::npos || first >= sizeof(region)) {
		Log("netplay: -sfve_start_perturb=%s does not read as <region>,<offset>,<add>", text.c_str());
		return;
	}
	text.copy(region, first);
	offset = strtoull(text.c_str() + first + 1, nullptr, 0);
	add = (int32_t)strtol(text.c_str() + second + 1, nullptr, 0);
	const StateRegion* r = adapter.Layout().FindByNameOrSize(region);
	if (!r || offset + 4 > r->size) {
		Log("netplay: nothing to perturb at %s+0x%zX before frame 0", region, offset);
		return;
	}
	int32_t& value = *reinterpret_cast<int32_t*>(r->ptr + offset);
	value += add;
	Log("netplay: perturbed %s+0x%zX before frame 0 (now %d), for the start state's agreement to repair", r->name.c_str(),
		offset, value);
}

// Layout: per region {uint64 size, uint64 address}, then its bytes. Written
// once the frame is confirmed, so two copies' dumps of a frame are final. The
// session may free the frame's save before then, so its latest save (a
// rollback replays and saves it again) is copied while it is kept.
void NetplayMode::DumpIfRequested()
{
	if (dumpFrame_ < 0) return;
	session_->KeepSave(dumpFrame_); // a session started since the request keeps it too
	if (const uint8_t* saved = session_->SavedState(dumpFrame_)) {
		dumpBytes_.assign(saved, saved + session_->Layout().TotalSize());
	}
	// With input delay a frame's inputs can be confirmed before it is played.
	if (session_->Frame() < dumpFrame_ || session_->LastConfirmedFrame() < dumpFrame_) return;
	if (dumpBytes_.empty()) {
		Log("netplay: frame %d was not kept; nothing dumped", dumpFrame_);
		dumpFrame_ = -1;
		return;
	}
	const uint8_t* bytes = dumpBytes_.data();
	const std::wstring path = LogFolder() + L"\\state-" + std::to_wstring(config_.instance) + L"-" +
		std::to_wstring(dumpFrame_) + L".bin";
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
	for (const StateRegion& r : session_->Layout().Regions()) {
		const uint64_t header[2] = {r.size, reinterpret_cast<uint64_t>(r.ptr)};
		fwrite(header, sizeof(header), 1, f);
		fwrite(bytes, 1, r.size, f);
		bytes += r.size;
	}
	fclose(f);
	Log("netplay: dumped frame %d to %ls", dumpFrame_, path.c_str());
	dumpFrame_ = -1;
	dumpBytes_.clear();
}

void NetplayMode::NoteFightStart(SfvAdapter& adapter, int frame)
{
	namespace fighter = sfv::fighter;
	if (!adapter.Fighting()) {
		fightSeen_ = false;
		return;
	}
	if (fightSeen_) return;
	fightSeen_ = true;
	// What a round's end must leave clean for the next round (docs/design/beta9/skip.md):
	// the battle frame's state word (pause bits 0x7, the KO's 0x20) and its
	// slow motion (+0x1A0).
	uint32_t state = 0, slow = 0;
	uint8_t* battleFrame = adapter.BattleFrame();
	if (battleFrame) {
		TryRead(battleFrame + sfv::battleframe::kState, &state);
		TryRead(battleFrame + 0x1A0, &slow);
	}
	Log("netplay: a round's fight starts at frame %d; battle frame state word %08x (pause bits %x, KO bit %d), slow motion %08x",
		frame, state, state & sfv::pause::kBits, (state & 0x20) != 0, slow);
	uint8_t* anyUnit = adapter.AnyUnit();
	if ((vitalPercent_[0] <= 0 && vitalPercent_[1] <= 0) || !anyUnit) return;
	for (uint32_t index = 0; index < 2; index++) {
		uint8_t* unit = GameFindUnit(anyUnit, sfv::unit::kCharaUnitType, index);
		uint8_t* status = unit ? GameFindPart(unit, fighter::kStatusPartType) : nullptr;
		int32_t side = -1, vitalMax = 0;
		if (!status || !TryRead(unit + fighter::kSide, &side) || side < 0 || side > 1 ||
			!TryRead(status + fighter::kVitalMax, &vitalMax)) {
			Log("netplay: vital: fighter %u not found at frame %d", index, frame);
			continue;
		}
		const int percent = vitalPercent_[side];
		if (percent <= 0 || percent >= 100) continue;
		const int32_t vital = static_cast<int32_t>(static_cast<int64_t>(vitalMax) * percent / 100);
		const bool ok = TryWrite(status + fighter::kVital, vital);
		Log("netplay: vital: P%d's set to %d%% (%d of %d, 16.16) at frame %d%s", side + 1, percent, vital, vitalMax, frame,
			ok ? "" : ": the write failed");
	}
}

// The live state is the played frame's, already saved: a rollback to that
// frame or earlier undoes the change, so it is meant for a frame no rollback
// crosses (before the players' inputs start).
void NetplayMode::PerturbIfDue()
{
	if (perturb_.frame < 0 || session_->Frame() < perturb_.frame) return;
	const StateRegion* r = session_->Layout().FindByNameOrSize(perturb_.region);
	if (r && perturb_.offset + 4 <= r->size) {
		int32_t& value = *reinterpret_cast<int32_t*>(r->ptr + perturb_.offset);
		value += perturb_.add;
		Log("netplay: perturbed %s+0x%zX at frame %d (now %d)", r->name.c_str(), perturb_.offset, session_->Frame(), value);
	} else {
		Log("netplay: nothing to perturb at %s+0x%zX", perturb_.region.c_str(), perturb_.offset);
	}
	perturb_.frame = -1;
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
	RegisterCommand("freeze", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		freezeFrame_ = args.empty() ? -1 : atoi(args.c_str());
		return std::string("ok");
	});
	// "riftgain N": the pacer's rift gain as N/120 (tuning; FramePacer).
	RegisterCommand("riftgain", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		pacer_.SetRiftGain(atof(args.c_str()) / 120.0);
		return std::string("ok");
	});
	// "skipafter <n>": this copy presses an attack n frames into each decided
	// match's scenes (-1, or nothing given: never), for runroom's -skipAfter.
	RegisterCommand("skipafter", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		skipAfter_ = args.empty() ? -1 : atoi(args.c_str());
		return std::string("ok");
	});
	// "vital <P1 percent> <P2 percent>": each fighter's vital set to that share
	// of its maximum at every round's first fight frame (0 or 100: left as
	// the round gives it), for runroom's KO endings. Not rollback-proof: runs
	// that use it play without rollbacks.
	RegisterCommand("vital", [this](const std::string& args) {
		int p1 = 0, p2 = 0;
		if (sscanf_s(args.c_str(), "%d %d", &p1, &p2) != 2 || p1 < 0 || p1 > 100 || p2 < 0 || p2 > 100) {
			return std::string("error usage: vital <P1 percent> <P2 percent> (0 or 100: as the round gives it)");
		}
		std::lock_guard lock(mutex_);
		vitalPercent_[0] = p1;
		vitalPercent_[1] = p2;
		return std::string("ok");
	});
	RegisterCommand("dump", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		dumpFrame_ = atoi(args.c_str());
		dumpBytes_.clear();
		// Its save keeps its copy even where no load can reach it.
		if (session_) session_->KeepSave(dumpFrame_);
		return std::string("ok");
	});
	RegisterCommand("perturb", [this](const std::string& args) {
		char region[64] = {};
		Perturbation p;
		if (sscanf_s(args.c_str(), "%d %63s %zi %d", &p.frame, region, (unsigned)sizeof(region), &p.offset, &p.add) != 4 ||
			p.frame < 0) {
			return std::string("error usage: perturb <frame> <region name or size> <offset> <add>");
		}
		p.region = region;
		std::lock_guard lock(mutex_);
		perturb_ = p;
		return std::string("ok");
	});
	RegisterCommand("hash", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		uint64_t hash = 0;
		const int frame = atoi(args.c_str());
		if (!session_ || !session_->HashAt(frame, &hash)) return std::string("error frame not kept");
		char text[32];
		snprintf(text, sizeof(text), "ok %016llx", (unsigned long long)hash);
		return std::string(text);
	});
	RegisterCommand("checkpoint", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		if (!session_) return std::string("error no session");
		const int atOrBefore = args.empty() ? session_->LastConfirmedFrame() : atoi(args.c_str());
		const int frame = session_->LatestCheckpoint(atOrBefore);
		std::vector<RegionHash> hashes;
		if (frame < 0 || !session_->RegionHashesAt(frame, &hashes)) return std::string("error no checkpoint");
		// size:hash per region, in the canonical order peers compare.
		std::string out = "ok " + std::to_string(frame);
		char item[48];
		for (const RegionHash& h : hashes) {
			snprintf(item, sizeof(item), " %llu:%016llx", (unsigned long long)h.size, (unsigned long long)h.hash);
			out += item;
		}
		return out;
	});
}

} // namespace sfve::mod
