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
	if (session_) return refuse("a session is already running");
	if (config.localPlayer < 0 || config.localPlayer >= kPlayers) return refuse("local player out of range");
	if (game_.Layout().Empty()) return refuse("the game has no rollback state yet");

	config_ = config;
	failed_ = false;
	failure_.clear();
	stats_ = {};
	frame_ = 0;
	holding_ = false;
	heldFrom_ = heldTo_ = -1;
	latch_.Reset();
	for (HashRecord& h : history_) h = {};
	allHashes_.clear();
	checkpoints_.clear();
	pool_.assign(kPoolSlots, Slot{});
	for (Slot& s : pool_) s.bytes.resize(game_.Layout().TotalSize());

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
	GGPOErrorCode err = ggpo_start_session(&session_, &cb, "sfv-ember", kPlayers, sizeof(PackedInput), config.localPort);
	if (err != GGPO_OK || !session_) {
		session_ = nullptr;
		return refuse("ggpo_start_session failed (is the local port free?)");
	}

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
	ggpo_set_frame_delay(session_, localHandle_, config.inputDelay);
	ggpo_set_disconnect_timeout(session_, config.disconnectTimeoutMs);
	ggpo_set_disconnect_notify_start(session_, config.disconnectNotifyStartMs);

	gate_.Reset();
	gate_.OnSessionStarted();
	CoreLog("session: started as P%d, port %u -> %s:%u, delay %d, state %zu bytes in %zu regions",
		config.localPlayer + 1, config.localPort, config.remoteIp.c_str(), config.remotePort, config.inputDelay,
		game_.Layout().TotalSize(), game_.Layout().Regions().size());
	return true;
}

RollbackSession::StepResult RollbackSession::Step(PackedInput localInput)
{
	if (!session_) return failed_ ? StepResult::Failed : StepResult::NoSession;
	ActiveScope active(this);

	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return StepResult::Failed;
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
	if (!session_) return failed_ ? StepResult::Failed : StepResult::NoSession;
	ActiveScope active(this);

	ggpo_idle(session_, 0);
	if (DrainPendingAbort()) return StepResult::Failed;
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
	DrainPendingAbort();
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
	if (!session_) return;
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
	localHandle_ = remoteHandle_ = -1;
	gate_.OnSessionClosed();
	for (Slot& s : pool_) s.used = false;
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

bool RollbackSession::DrainPendingAbort()
{
	char reason[256];
	if (!latch_.Take(reason, sizeof(reason))) return false;
	CloseNow();
	return true;
}

int RollbackSession::LastConfirmedFrame() const
{
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
	StateLayout& layout = game_.Layout();
	LARGE_INTEGER started, copied, hashed, perSecond;
	QueryPerformanceCounter(&started);
	layout.SaveTo(slot->bytes.data());
	QueryPerformanceCounter(&copied);
	slot->used = true;
	slot->frame = frame;
	const bool checkpoint = frame % kCheckpointInterval == 0;
	slot->hashed = checkpoint || config_.verifyLoads || frame % config_.hashEveryFrames == 0;
	slot->hash = slot->hashed ? layout.HashSaved(slot->bytes.data(), HashScope::Peers) : 0;
	slot->localHash = config_.verifyLoads ? layout.HashSaved(slot->bytes.data()) : 0;
	QueryPerformanceCounter(&hashed);
	if (slot->hashed) {
		history_[frame % kHistory] = {frame, slot->hash};
		if (config_.keepAllHashes) {
			if ((size_t)frame >= allHashes_.size()) allHashes_.resize((size_t)frame + 1);
			allHashes_[frame] = {frame, slot->hash};
		}
	}
	if (checkpoint) {
		layout.PeerRegionHashesSaved(slot->bytes.data(), checkpoints_[frame]); // a replay overwrites it
		QueryPerformanceFrequency(&perSecond);
		const auto us = [&](const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
			return (b.QuadPart - a.QuadPart) * 1000000 / perSecond.QuadPart;
		};
		CoreLog("checkpoint: frame %d hash %016llx (copy %lld us, hash %lld us)", frame,
			(unsigned long long)slot->hash, us(started, copied), us(copied, hashed));
		while (checkpoints_.size() > kCheckpointsKept) checkpoints_.erase(checkpoints_.begin());
	}
	frame_ = frame;

	*buffer = reinterpret_cast<unsigned char*>(slot);
	*len = 1;      // GGPO asserts on a zero length; it never reads the bytes
	*checksum = 0; // nor the checksum (only its synctest backend would)
	return true;
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

	const int depth = frame_ - slot->frame;
	stats_.rollbacks++;
	if (depth > stats_.longestRollback) stats_.longestRollback = depth;
	frame_ = slot->frame;

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
	return matter ? Restricted(inputs, game_.InputBits()) : FrameInputs{};
}

bool RollbackSession::AdvanceFromRollback()
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	if (!session_ || failed_) return false;
	FrameInputs inputs;
	int disconnectFlags = 0;
	if (ggpo_synchronize_input(session_, inputs.p, sizeof(inputs.p), &disconnectFlags) != GGPO_OK) {
		Fail("synchronize_input failed during a rollback");
		return false;
	}
	if (Held(frame_)) {
		// Held frames are stepped confirmed, so none should ever be replayed;
		// if one is, the game still stands still on it.
		ggpo_set_inputs_ignored(session_, true);
		if (stats_.heldReplays++ == 0) CoreLog("session: a rollback replayed held frame %d", frame_);
		ggpo_advance_frame(session_);
		return true;
	}
	game_.Advance(InputsForThisFrame(inputs), true);
	stats_.resimulatedFrames++;
	ggpo_advance_frame(session_);
	return true;
}

void RollbackSession::Event(const GGPOEvent& info)
{
	sf4e::gate::AbortLatch::Scope scope(latch_);
	switch (info.code) {
	case GGPO_EVENTCODE_RUNNING:
		gate_.OnRunning();
		CoreLog("session: running");
		break;
	case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
		Fail("the other player disconnected");
		break;
	case GGPO_EVENTCODE_CONNECTION_INTERRUPTED:
		if (gate_.OnConnectionInterrupted(GetTickCount())) CoreLog("session: connection interrupted");
		break;
	case GGPO_EVENTCODE_CONNECTION_RESUMED:
		if (gate_.OnConnectionResumed()) CoreLog("session: connection resumed");
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
