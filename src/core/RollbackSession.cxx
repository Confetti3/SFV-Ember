// RollbackSession: the session's life (Start, Close, a failure) and the
// fighters' steps through GGPO. The rest of the class lives beside it
// (RollbackSession.hxx says where).

#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"
#include "core/RollbackSessionScopes.hxx"
#include "core/SyncTest.hxx"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace sfve {

namespace {

constexpr int kPoolSlots = GGPO_MAX_PREDICTION_FRAMES + 4;

} // namespace

RollbackSession::RollbackSession(GameAdapter& game) : driven_(game), game_(&game)
{
	gate_.Reset();
	history_.resize(kHistory);
}

RollbackSession::~RollbackSession()
{
	// The game and the owners of the callbacks may be gone already: nothing
	// more is hashed or reported.
	tearingDown_ = true;
	Close();
}

bool RollbackSession::Start(const Config& config, std::string* error)
{
	const auto refuse = [&](const char* why) {
		if (error) *error = why;
		return false;
	};
	if (Live()) return refuse("a session is already running");
	game_ = &driven_; // again, if a keep for watchers let it go
	const bool watches = config.watching || config.replay;
	if (!watches && (config.localPlayer < 0 || config.localPlayer >= kPlayers)) {
		return refuse("local player out of range");
	}
	if (game_->Layout().Empty()) return refuse("the game has no rollback state yet");
	if (config.hashEveryFrames < 1) return refuse("hashEveryFrames must be at least 1");
	if (config.verifyLoadsEvery < 0) return refuse("verifyLoadsEvery must not be negative");

	config_ = config;
	config_.watching = watches;
	failed_ = false;
	failure_.clear();
	stats_ = {};
	appliedDelay_ = -1;
	disconnectTimeoutMs_ = 0;
	peerDisconnected_ = false;
	rollbackHud_.Reset();
	frame_ = 0;
	holding_ = false;
	heldFrom_ = heldTo_ = -1;
	latch_.Reset();
	for (HashRecord& h : history_) h = {};
	allHashes_.clear();
	checkpoints_.clear();
	// Every buffer of a state is made here, before frame 0, so the battle's
	// first frames allocate none (its first checkpoints' kept states were
	// made as they came, a state's size each: a dip in the pace line).
	const size_t stateSize = game_->Layout().TotalSize();
	for (KeptState& kept : checkpointStates_) {
		kept.frame = -1;
		kept.bytes.resize(stateSize);
	}
	// A watcher keeps no saves: one buffer holds the state it hashes.
	pool_.assign(watches ? 1 : kPoolSlots, Slot{});
	for (Slot& s : pool_) s.bytes.resize(stateSize);
	confirmed_ = -1;
	rollingBack_ = false;
	rollbackFrom_ = rollbackEnd_ = -1;
	closing_ = false;
	loads_ = 0;
	policy_.Start(GetTickCount64(), {}, config.watcherSyncDeadlineMs);
	links_.Start();
	watcherHandles_.clear();
	// A replay has no stream to wait for.
	streamWatch_.Start(GetTickCount64(), config.watching && !config.replay);
	watchedStartHashed_ = false;
	keptForWatchers_ = false;
	replaying_ = false;
	replayCheck_.reset();
	lagged_.reset();
	laggedHashed_ = 0;
	laggedFinished_ = false;
	replayCheckFailures_ = 0;
	replayCheckLogged_.clear();

	if (config.replay) {
		// No GGPO: the frames are here, all of them confirmed, so the session
		// runs from its first step.
		replaying_ = true;
		if (config.replayLag > 0) {
			lagged_ = std::make_unique<LaggedReplay>(*game_, *config.replay, config.replayLagPlayer, config.replayLag,
				config.replayLagJitter);
			lagged_->onGuessedState = [this](int frame, const uint8_t* state) {
				if (frame % kCheckpointInterval != 0) return;
				CoreLog("replay lag: checkpoint %d on guessed inputs, hash %016llx", frame,
					(unsigned long long)game_->Layout().HashSaved(state, HashScope::Peers));
			};
			CoreLog("replay lag: P%d's inputs arrive %s%d frame(s) late (jitter seed %u); guessed frames are played and "
				"rolled back", config.replayLagPlayer + 1, config.replayLagJitter ? "up to " : "",
				config.replayLag, config.replayLagJitter);
		} else if (config.replayCheckDistance > 0) {
			const uint32_t seed = config.replayCheckSeed ? config.replayCheckSeed : 1;
			replayCheck_ = std::make_unique<SyncTester>(*game_, config.replayCheckDistance, true, true, seed);
			CoreLog("replay check: rollbacks of up to %d frame(s), schedule %u, each first played with wrong inputs",
				config.replayCheckDistance, seed);
		}
		gate_.Reset();
		gate_.OnSessionStarted();
		gate_.OnRunning();
		CoreLog("session: playing a replay of %zu frame(s), state %zu bytes in %zu regions", config.replay->size(),
			game_->Layout().TotalSize(), game_->Layout().Regions().size());
		return true;
	}

	GGPOSessionCallbacks cb = {};
	cb.begin_game = OnBeginGame;
	cb.save_game_state = OnSave;
	cb.load_game_state = OnLoad;
	cb.log_game_state = OnLogState;
	cb.free_buffer = OnFree;
	cb.advance_frame = OnAdvance;
	cb.on_event = OnEvent;

	ActiveScope active(this);
	if (config.watching) {
		// GGPO's spectator backend: one link, to P1, which streams the
		// fighters' confirmed inputs. It takes no frame delay and no
		// disconnect settings (it refuses both); the room ends a link that
		// goes quiet (SpectatorStreamWatch below, and the match session).
		char host[32] = {};
		strncpy_s(host, config.remoteIp.c_str(), _TRUNCATE);
		const GGPOErrorCode started = ggpo_start_spectating(&session_, &cb, "sfv-ember", kPlayers, sizeof(PackedInput),
			config.localPort, host, config.remotePort);
		if (started != GGPO_OK || !session_) {
			session_ = nullptr;
			return refuse("ggpo_start_spectating failed (is the local port free?)");
		}
		gate_.Reset();
		gate_.OnSessionStarted();
		CoreLog("session: watching, port %u <- P1 at %s:%u, state %zu bytes in %zu regions", config.localPort,
			config.remoteIp.c_str(), config.remotePort, game_->Layout().TotalSize(), game_->Layout().Regions().size());
		return true;
	}
	GGPOErrorCode err = ggpo_start_session(&session_, &cb, "sfv-ember", kPlayers, sizeof(PackedInput), config.localPort);
	if (err != GGPO_OK || !session_) {
		session_ = nullptr;
		return refuse("ggpo_start_session failed (is the local port free?)");
	}
	// Before any player is added: GGPO gives a watcher's link the settings
	// current when it is added (AddSpectator), the fighters' links as they change.
	ggpo_set_disconnect_timeout(session_, config.disconnectTimeoutMs);
	ggpo_set_disconnect_notify_start(session_, config.disconnectNotifyStartMs);

	for (int player = 0; player < kPlayers; player++) {
		GGPOPlayer p = {};
		p.size = sizeof(p);
		p.player_num = player + 1;
		if (player == config.localPlayer) {
			p.type = GGPO_PLAYERTYPE_LOCAL;
		} else {
			p.type = GGPO_PLAYERTYPE_REMOTE;
			strncpy_s(p.u.remote.ip_address, config.remoteIp.c_str(), _TRUNCATE);
			p.u.remote.port = config.remotePort;
		}
		GGPOPlayerHandle handle = -1;
		err = ggpo_add_player(session_, &p, &handle);
		if (err != GGPO_OK) {
			CloseNow();
			return refuse("ggpo_add_player failed");
		}
		(player == config.localPlayer ? localHandle_ : remoteHandle_) = handle;
	}
	// P1's watchers, after both fighters. One GGPO cannot add sits this game
	// out rather than stop the fighters'; GGPO answers each with its own
	// handle (spectator-handle-control.patch).
	std::vector<int> watching;
	std::map<int, size_t> slots;
	for (const Config::Watcher& w : config.watchers) {
		if (config.localPlayer != 0) break; // only P1 streams
		GGPOPlayer p = {};
		p.size = sizeof(p);
		p.type = GGPO_PLAYERTYPE_SPECTATOR;
		p.player_num = (int)w.slot + 1;
		strncpy_s(p.u.remote.ip_address, config.remoteIp.c_str(), _TRUNCATE);
		p.u.remote.port = w.port;
		GGPOPlayerHandle handle = GGPO_INVALID_HANDLE;
		err = ggpo_add_player(session_, &p, &handle);
		if (err != GGPO_OK || handle == GGPO_INVALID_HANDLE) {
			CoreLog("session: the watcher in slot %zu (port %u) could not be added (%d); it sits this game out", w.slot,
				w.port, (int)err);
			continue;
		}
		watching.push_back(handle);
		slots[handle] = w.slot;
		watcherHandles_.insert(handle);
	}
	policy_.Start(GetTickCount64(), watching, config.watcherSyncDeadlineMs);
	links_.Start(std::move(slots));
	const GGPOErrorCode delayed = ggpo_set_frame_delay(session_, localHandle_, config.inputDelay);
	appliedDelay_ = GGPO_SUCCEEDED(delayed) ? config.inputDelay : -1;
	if (!GGPO_SUCCEEDED(delayed)) CoreLog("session: set_frame_delay(%d) returned %d", config.inputDelay, (int)delayed);

	gate_.Reset();
	gate_.OnSessionStarted();
	CoreLog("session: started as P%d, port %u -> %s:%u, delay %d, %zu watcher(s), state %zu bytes in %zu regions",
		config.localPlayer + 1, config.localPort, config.remoteIp.c_str(), config.remotePort, config.inputDelay,
		watching.size(), game_->Layout().TotalSize(), game_->Layout().Regions().size());
	char verified[48] = "never";
	if (config.verifyLoadsEvery == 1) snprintf(verified, sizeof(verified), "every one");
	else if (config.verifyLoadsEvery > 0) snprintf(verified, sizeof(verified), "one in %d", config.verifyLoadsEvery);
	CoreLog("session: a save no load can reach keeps no copy; frames hashed once final; restores re-hashed: %s", verified);
	return true;
}

RollbackSession::StepResult RollbackSession::Step(PackedInput localInput)
{
	const StepResult result = StepBody(localInput);
	if (result == StepResult::Waiting) stats_.waitingSteps++;
	return result;
}

RollbackSession::StepResult RollbackSession::StepBody(PackedInput localInput)
{
	if (!Live()) return failed_ ? StepResult::Failed : StepResult::NoSession;
	if (config_.watching) return StepWatched(nullptr);
	ActiveScope active(this);
	ConfirmedOnReturn confirmed(*this);

	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return StepResult::Failed;
	PollWatchers();
	if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;
	if (holding_) {
		holding_ = false;
		heldTo_ = frame_;
		CoreLog("session: hold ended at frame %d (%d held frame(s))", frame_, frame_ - heldFrom_);
	}
	// A frame that reads bits only confirmed (GameAdapter::ConfirmedBits)
	// waits for its inputs to be final, as a held frame does.
	if (ReadsConfirmed(*game_, frame_) && LastConfirmedFrame() < frame_) return StepResult::Waiting;

	StepResult result = StepResult::Advanced;
	GGPOErrorCode err = ggpo_add_local_input(session_, localHandle_, &localInput, sizeof(localInput));
	if (DrainPendingAbort()) return StepResult::Failed;
	if (!HandleResult(err, "add_local_input", &result)) return result;
	gate_.OnFrameAccepted();

	FrameInputs inputs;
	int disconnectFlags = 0;
	err = ggpo_synchronize_input(session_, inputs.p, sizeof(inputs.p), &disconnectFlags);
	if (!HandleResult(err, "synchronize_input", &result)) return result;

	game_->Advance(InputsForThisFrame(inputs), false);
	stats_.framesAdvanced++;
	err = ggpo_advance_frame(session_);
	if (DrainPendingAbort()) return StepResult::Failed;
	if (!HandleResult(err, "advance_frame", &result)) return result;
	return StepResult::Advanced;
}

RollbackSession::StepResult RollbackSession::StepHeld(PackedInput localInput,
	const std::function<void(const FrameInputs&)>& onInputs)
{
	const StepResult result = StepHeldBody(localInput, onInputs);
	if (result == StepResult::Waiting) stats_.waitingSteps++;
	return result;
}

RollbackSession::StepResult RollbackSession::StepHeldBody(PackedInput localInput,
	const std::function<void(const FrameInputs&)>& onInputs)
{
	if (!Live()) return failed_ ? StepResult::Failed : StepResult::NoSession;
	if (config_.watching) return StepWatched(&onInputs);
	ActiveScope active(this);
	ConfirmedOnReturn confirmed(*this);

	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return StepResult::Failed;
	PollWatchers();
	if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;
	// The frame's inputs are final only once confirmed: a held frame takes no
	// input for a misprediction, so a guess would never be corrected.
	if (LastConfirmedFrame() < frame_) return StepResult::Waiting;
	if (!holding_) {
		holding_ = true;
		heldFrom_ = frame_;
		CoreLog("session: holding the game from frame %d", frame_);
	}

	StepResult result = StepResult::Advanced;
	GGPOErrorCode err = ggpo_add_local_input(session_, localHandle_, &localInput, sizeof(localInput));
	if (DrainPendingAbort()) return StepResult::Failed;
	if (!HandleResult(err, "add_local_input", &result)) return result;
	gate_.OnFrameAccepted();

	FrameInputs inputs;
	int disconnectFlags = 0;
	err = ggpo_synchronize_input(session_, inputs.p, sizeof(inputs.p), &disconnectFlags);
	if (!HandleResult(err, "synchronize_input", &result)) return result;
	ggpo_set_inputs_ignored(session_, true);
	Report(inputs, RecordedFrame::kHeld);
	if (onInputs) onInputs(inputs);
	stats_.heldFrames++;
	err = ggpo_advance_frame(session_);
	if (DrainPendingAbort()) return StepResult::Failed;
	if (!HandleResult(err, "advance_frame", &result)) return result;
	return StepResult::Advanced;
}

void RollbackSession::Poll()
{
	if (!session_) return;
	ActiveScope active(this);
	ConfirmedOnReturn confirmed(*this);
	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return;
	PollWatchers();
}

bool RollbackSession::HandleResult(int ggpoError, const char* call, StepResult* out)
{
	switch (sf4e::gate::ClassifyGgpoResult(ggpoError)) {
	case sf4e::gate::POLICY_CONTINUE:
		return true;
	case sf4e::gate::POLICY_STALL_PREDICTION:
		gate_.OnPredictionThreshold();
		stats_.predictionStalls++;
		*out = StepResult::PredictionStall;
		return false;
	case sf4e::gate::POLICY_SKIP_NOT_SYNCED:
		*out = StepResult::Waiting;
		return false;
	case sf4e::gate::POLICY_SKIP_OTHER:
		CoreLog("session: %s returned %d; frame skipped", call, ggpoError);
		*out = StepResult::Skipped;
		return false;
	default: {
		char why[96];
		snprintf(why, sizeof(why), "%s returned %d", call, ggpoError);
		Fail(why);
		*out = StepResult::Failed;
		return false;
	}
	}
}

void RollbackSession::Close()
{
	if (!Live()) return;
	if (latch_.InCallback()) {
		latch_.Request("closed from a callback");
		return;
	}
	CloseNow();
}

void RollbackSession::CloseNow()
{
	if (session_) {
		ActiveScope active(this);
		if (!config_.watching) {
			// The frames final by now are hashed before GGPO lets their saves go.
			if (!tearingDown_) HashConfirmed();
			const Stats& s = stats_;
			CoreLog("session costs: %llu frame(s) played, %llu replayed in %llu rollback(s); %llu save(s) kept no copy; "
				"%llu frame(s) hashed once final, %llu save(s) replaced before final; %llu restore(s) re-hashed, %llu "
				"differed", (unsigned long long)s.framesAdvanced,
				(unsigned long long)s.resimulatedFrames, (unsigned long long)s.rollbacks, (unsigned long long)s.savesSkipped,
				(unsigned long long)s.hashesOnConfirm, (unsigned long long)s.hashesDropped, (unsigned long long)s.loadsVerified,
				(unsigned long long)s.loadMismatches);
		}
		closing_ = true;
		ggpo_close_session(session_);
		closing_ = false;
		session_ = nullptr;
	}
	replaying_ = false;
	if (lagged_) {
		CoreLog("replay lag: %d frame(s), %d rollback(s), the longest %d frame(s)", frame_,
			lagged_->Rollbacks(), lagged_->LongestRollback());
		lagged_.reset();
	}
	if (replayCheck_) {
		const SyncTestReport& r = replayCheck_->Report();
		CoreLog("replay check: %d frame(s), %d rollback(s), %d differed", r.framesRun, r.rollbacks, r.failures);
		replayCheck_.reset();
	}
	localHandle_ = remoteHandle_ = -1;
	gate_.OnSessionClosed();
	for (Slot& s : pool_) s.used = s.pending = s.copied = false;
	// No watcher is served any more. A drop not yet handed to the room was
	// this session's, and the room ends this match's links itself (SF4's
	// RetireGgpoSession forgets it the same way).
	policy_.Start(GetTickCount64(), {}, config_.watcherSyncDeadlineMs);
	links_.Start();
	watcherHandles_.clear();
	keptForWatchers_ = false;
}

void RollbackSession::Fail(const char* reason)
{
	if (!failed_) {
		failed_ = true;
		failure_ = reason ? reason : "";
		CoreLog("session: failed: %s", failure_.c_str());
	}
	gate_.OnFatal();
	if (latch_.Request(reason)) return; // completed by DrainPendingAbort
	CloseNow();
}

bool RollbackSession::FailedByGame()
{
	const char* why = game_->StateFailure();
	if (!why) return false;
	Fail(why);
	return true;
}

bool RollbackSession::DrainPendingAbort()
{
	char reason[256];
	if (!latch_.Take(reason, sizeof(reason))) return false;
	CloseNow();
	return true;
}

int RollbackSession::LastConfirmedFrame() const
{
	// A lagged replay's frames are final only once its guesses are settled.
	if (Live() && config_.watching) return lagged_ ? lagged_->ConfirmedThrough() : frame_;
	int frame = -1;
	if (session_ && ggpo_get_last_confirmed_frame(session_, &frame) == GGPO_OK) return frame;
	return -1;
}

bool RollbackSession::ReadRift(Rift* out) const
{
	if (!session_ || remoteHandle_ < 0) return false;
	GGPONetworkStats stats{};
	if (ggpo_get_network_stats(session_, remoteHandle_, &stats) != GGPO_OK) return false;
	*out = {(double)stats.timesync.local_frames_behind, (double)stats.timesync.remote_frames_behind,
		stats.network.ping};
	return true;
}

int RollbackSession::DisconnectCountdown(bool warned, uint32_t warnedAtMs, int timeoutMs, uint32_t nowMs)
{
	if (!warned || timeoutMs <= 0) return -1;
	const uint32_t elapsed = nowMs - warnedAtMs;
	return elapsed >= (uint32_t)timeoutMs ? 0 : (int)((uint32_t)timeoutMs - elapsed);
}

int RollbackSession::DisconnectCountdownMs() const
{
	return DisconnectCountdown(ConnectionWarning(), gate_.connectionWarningStartedAtMs, disconnectTimeoutMs_, GetTickCount());
}

unsigned RollbackSession::RecentRollbackFrames() const
{
	return session_ ? rollbackHud_.Recent(GetTickCount64()) : 0;
}

} // namespace sfve
