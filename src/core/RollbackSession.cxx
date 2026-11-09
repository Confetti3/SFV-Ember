#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace sfve {

namespace {

constexpr int kPoolSlots = GGPO_MAX_PREDICTION_FRAMES + 4;
constexpr int kHistory = 256;
// Five minutes of checkpoints (about 550 regions x 16 bytes each), so a
// divergence that comes and goes is still found region by region after it.
constexpr size_t kCheckpointsKept = 300;

RollbackSession* s_active = nullptr;

void __cdecl OnGgpoAssert(const char* message)
{
	CoreLog("ggpo: assertion failed: %s", message ? message : "?");
}

} // namespace

// Publishes the session making a GGPO call to the static callbacks.
class RollbackSession::ActiveScope {
public:
	explicit ActiveScope(RollbackSession* s) : previous_(s_active) { s_active = s; }
	~ActiveScope() { s_active = previous_; }
	ActiveScope(const ActiveScope&) = delete;
	ActiveScope& operator=(const ActiveScope&) = delete;

private:
	RollbackSession* previous_;
};

RollbackSession::RollbackSession(GameAdapter& game) : game_(game)
{
	gate_.Reset();
	history_.resize(kHistory);
}

RollbackSession::~RollbackSession()
{
	Close();
}

bool RollbackSession::Start(const Config& config, std::string* error)
{
	const auto refuse = [&](const char* why) {
		if (error) *error = why;
		return false;
	};
	if (Live()) return refuse("a session is already running");
	const bool watches = config.watching || config.replay;
	if (!watches && (config.localPlayer < 0 || config.localPlayer >= kPlayers)) {
		return refuse("local player out of range");
	}
	if (game_.Layout().Empty()) return refuse("the game has no rollback state yet");
	if (config.hashEveryFrames < 1) return refuse("hashEveryFrames must be at least 1");

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
	// A watcher keeps no saves: one buffer holds the state it hashes.
	pool_.assign(watches ? 1 : kPoolSlots, Slot{});
	for (Slot& s : pool_) s.bytes.resize(game_.Layout().TotalSize());
	policy_.Start(GetTickCount64(), {}, config.watcherSyncDeadlineMs);
	links_.Start();
	watcherHandles_.clear();
	// A replay has no stream to wait for.
	streamWatch_.Start(GetTickCount64(), config.watching && !config.replay);
	watchedStartHashed_ = false;
	keptForWatchers_ = false;
	replaying_ = false;

	if (config.replay) {
		// No GGPO: the frames are here, all of them confirmed, so the session
		// runs from its first step.
		replaying_ = true;
		gate_.Reset();
		gate_.OnSessionStarted();
		gate_.OnRunning();
		CoreLog("session: playing a replay of %zu frame(s), state %zu bytes in %zu regions", config.replay->size(),
			game_.Layout().TotalSize(), game_.Layout().Regions().size());
		return true;
	}

	ggpo_set_assert_handler(OnGgpoAssert);

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
			config.remoteIp.c_str(), config.remotePort, game_.Layout().TotalSize(), game_.Layout().Regions().size());
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
		watching.size(), game_.Layout().TotalSize(), game_.Layout().Regions().size());
	return true;
}

RollbackSession::StepResult RollbackSession::Step(PackedInput localInput)
{
	if (!Live()) return failed_ ? StepResult::Failed : StepResult::NoSession;
	if (config_.watching) return StepWatched(nullptr);
	ActiveScope active(this);

	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return StepResult::Failed;
	PollWatchers();
	if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;
	if (holding_) {
		holding_ = false;
		heldTo_ = frame_;
		CoreLog("session: hold ended at frame %d (%d held frame(s))", frame_, frame_ - heldFrom_);
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

	game_.Advance(InputsForThisFrame(inputs), false);
	stats_.framesAdvanced++;
	err = ggpo_advance_frame(session_);
	if (DrainPendingAbort()) return StepResult::Failed;
	if (!HandleResult(err, "advance_frame", &result)) return result;
	return StepResult::Advanced;
}

RollbackSession::StepResult RollbackSession::StepHeld(PackedInput localInput,
	const std::function<void(const FrameInputs&)>& onInputs)
{
	if (!Live()) return failed_ ? StepResult::Failed : StepResult::NoSession;
	if (config_.watching) return StepWatched(&onInputs);
	ActiveScope active(this);

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
	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return;
	PollWatchers();
}

RollbackSession::StepResult RollbackSession::StepWatched(const std::function<void(const FrameInputs&)>* onHeld)
{
	FrameInputs inputs;
	if (replaying_) {
		// The recording's frame, in place of P1's stream.
		if (!gate_.CanAdvanceDeterministicFrame()) return StepResult::Waiting;
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
		// (InputsForThisFrame), without telling GGPO: a watcher never predicts.
		const bool matter = game_.InputsMatter();
		const FrameInputs given = matter ? Restricted(inputs, game_.InputBits()) : FrameInputs{};
		Report(given, matter ? RecordedFrame::kFight : 0);
		game_.Advance(given, false);
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
	HashWatched(frame_);
	return StepResult::Advanced;
}

bool RollbackSession::ReplayInputs(bool held, FrameInputs* inputs)
{
	const ReplayFrames& frames = *config_.replay;
	char why[128];
	if (frame_ < 0 || (size_t)frame_ >= frames.size()) {
		snprintf(why, sizeof(why), "the replay ends at frame %zu", frames.size());
		Fail(why);
		return false;
	}
	const RecordedFrame& recorded = frames[(size_t)frame_];
	// The game decides each frame's kind from its own state (the WIN held,
	// the fight reading inputs). One the recording did not have means the
	// playback is no longer the recorded battle.
	const bool fight = !held && game_.InputsMatter();
	if (recorded.Held() != held || (!held && recorded.Fight() != fight)) {
		snprintf(why, sizeof(why), "the playback left the recording at frame %d (recorded %s, played %s)", frame_,
			recorded.Held() ? "held" : recorded.Fight() ? "fight" : "ignored", held ? "held" : fight ? "fight" : "ignored");
		Fail(why);
		return false;
	}
	*inputs = recorded.inputs;
	return true;
}

void RollbackSession::HashWatched(int frame)
{
	if (frame % kCheckpointInterval != 0 && frame % config_.hashEveryFrames != 0) return;
	Slot& slot = pool_[0];
	LARGE_INTEGER started, copied;
	QueryPerformanceCounter(&started);
	game_.Layout().SaveTo(slot.bytes.data());
	QueryPerformanceCounter(&copied);
	slot.frame = frame;
	// A scratch copy, never a save GGPO holds: it stays unused.
	RecordHash(slot, frame, copied.QuadPart - started.QuadPart);
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
	CoreLog("session: kept for %zu watcher stream(s) after the fight", WatcherStreams());
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
		ggpo_close_session(session_);
		session_ = nullptr;
	}
	replaying_ = false;
	localHandle_ = remoteHandle_ = -1;
	gate_.OnSessionClosed();
	for (Slot& s : pool_) s.used = false;
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
	const char* why = game_.StateFailure();
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
	if (Live() && config_.watching) return frame_;
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

bool RollbackSession::HashAt(int frame, uint64_t* out) const
{
	if (frame < 0) return false;
	const HashRecord& r = (size_t)frame < allHashes_.size() ? allHashes_[frame] : history_[frame % kHistory];
	if (r.frame != frame) return false;
	*out = r.hash;
	return true;
}

const uint8_t* RollbackSession::SavedState(int frame) const
{
	for (const Slot& s : pool_) {
		if (s.used && s.frame == frame) return s.bytes.data();
	}
	return nullptr;
}

bool RollbackSession::RegionHashesAt(int frame, std::vector<RegionHash>* out) const
{
	const auto it = checkpoints_.find(frame);
	if (it == checkpoints_.end()) return false;
	*out = it->second;
	return true;
}

int RollbackSession::LatestCheckpoint(int atOrBefore) const
{
	int latest = -1;
	for (const auto& [frame, hashes] : checkpoints_) {
		if (frame <= atOrBefore) latest = frame;
	}
	return latest;
}

// --- GGPO callbacks -------------------------------------------------------

bool __cdecl RollbackSession::OnBeginGame(const char*) { return true; }
bool __cdecl RollbackSession::OnLogState(char*, unsigned char*, int) { return true; }

bool __cdecl RollbackSession::OnSave(unsigned char** buffer, int* len, int* checksum, int frame)
{
	return s_active && s_active->Save(buffer, len, checksum, frame);
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

bool RollbackSession::Save(unsigned char** buffer, int* len, int* checksum, int frame)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	// GGPO saves after every frame it advances, a rollback's replays
	// included, so this is where a failure the game latched while advancing
	// (or while a held frame changed it) ends the session, before a state it
	// can no longer restore is kept.
	if (FailedByGame()) return false;
	Slot* slot = nullptr;
	for (Slot& s : pool_) {
		if (!s.used) {
			slot = &s;
			break;
		}
	}
	if (!slot) {
		Fail("save pool exhausted");
		return false;
	}
	LARGE_INTEGER started, copied;
	QueryPerformanceCounter(&started);
	game_.Layout().SaveTo(slot->bytes.data());
	QueryPerformanceCounter(&copied);
	slot->used = true;
	slot->frame = frame;
	RecordHash(*slot, frame, copied.QuadPart - started.QuadPart);
	frame_ = frame;

	*buffer = reinterpret_cast<unsigned char*>(slot);
	*len = 1;      // GGPO asserts on a zero length; it never reads the bytes
	*checksum = 0; // nor the checksum (only its synctest backend would)
	return true;
}

void RollbackSession::RecordHash(Slot& slot, int frame, long long copyTicks)
{
	StateLayout& layout = game_.Layout();
	LARGE_INTEGER started, hashed, perSecond;
	QueryPerformanceCounter(&started);
	const bool checkpoint = frame % kCheckpointInterval == 0;
	slot.hashed = checkpoint || config_.verifyLoads || frame % config_.hashEveryFrames == 0;
	slot.hash = slot.hashed ? layout.HashSaved(slot.bytes.data(), HashScope::Peers) : 0;
	slot.localHash = config_.verifyLoads ? layout.HashSaved(slot.bytes.data()) : 0;
	QueryPerformanceCounter(&hashed);
	if (slot.hashed) {
		history_[frame % kHistory] = {frame, slot.hash};
		if (config_.keepAllHashes) {
			if ((size_t)frame >= allHashes_.size()) allHashes_.resize((size_t)frame + 1);
			allHashes_[frame] = {frame, slot.hash};
		}
	}
	if (checkpoint) {
		if (config_.onCheckpoint) config_.onCheckpoint(frame, slot.hash);
		layout.PeerRegionHashesSaved(slot.bytes.data(), checkpoints_[frame]); // a replay overwrites it
		QueryPerformanceFrequency(&perSecond);
		const auto us = [&](long long ticks) { return ticks * 1000000 / perSecond.QuadPart; };
		CoreLog("checkpoint: frame %d hash %016llx (copy %lld us, hash %lld us)", frame, (unsigned long long)slot.hash,
			us(copyTicks), us(hashed.QuadPart - started.QuadPart));
		while (checkpoints_.size() > kCheckpointsKept) checkpoints_.erase(checkpoints_.begin());
	}
}

bool RollbackSession::Load(unsigned char* buffer)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	Slot* slot = reinterpret_cast<Slot*>(buffer);
	if (!slot || !slot->used) {
		Fail("load of a freed state");
		return false;
	}
	StateLayout& layout = game_.Layout();
	layout.LoadFrom(slot->bytes.data());
	game_.AfterLoad();
	if (FailedByGame()) return false; // the restore did not reach all of the game

	const int depth = frame_ - slot->frame;
	stats_.rollbacks++;
	if (depth > stats_.longestRollback) stats_.longestRollback = depth;
	frame_ = slot->frame;
	rollbackHud_.Begin(GetTickCount64());

	if (config_.verifyLoads && layout.HashLive() != slot->localHash) {
		stats_.loadMismatches++;
		CoreLog("session: frame %d restored with a different hash", slot->frame);
	}
	return true;
}

void RollbackSession::Free(void* buffer)
{
	Slot* slot = static_cast<Slot*>(buffer);
	if (slot) slot->used = false;
}

// The frame about to be advanced: when the game ignores its inputs, GGPO is
// told (no misprediction on it) and the game gets neutral inputs, so what
// the players' copies happened to send cannot change the simulation; when it
// reads them, only the bits a pad can produce reach it.
FrameInputs RollbackSession::InputsForThisFrame(const FrameInputs& inputs)
{
	const bool matter = game_.InputsMatter();
	ggpo_set_inputs_ignored(session_, !matter);
	const FrameInputs given = matter ? Restricted(inputs, game_.InputBits()) : FrameInputs{};
	Report(given, matter ? RecordedFrame::kFight : 0);
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
	game_.Advance(InputsForThisFrame(inputs), true);
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
