#pragma once

// A two-player GGPO session over one GameAdapter.
//
// Follows the rules SF4 Ember settled on (docs/design/SFV_PORT.md, "carried
// over from Ember"):
//
// * Everything runs on the game thread. GGPO has no threads of its own; all
//   callbacks run inside ggpo_* calls made from Step() or Close().
// * The simulation gate (sf4e::gate::GgpoGateModel) decides whether a frame
//   may advance; a connection warning never blocks it, a prediction stall is
//   per-frame, a fatal error latches.
// * No callback ever closes the session: an abort inside a callback is latched
//   (sf4e::gate::AbortLatch) and completed once the GGPO call has returned.
// * Saves go into a preallocated pool sized from the layout at start; GGPO
//   only ever holds pointers into it.
//
// GGPO callbacks carry no context pointer, so the session that is making a
// GGPO call is published in a scoped static for the callbacks to find. Two
// sessions in one process (the loopback tests) work because calls never nest
// across sessions.

#include "core/GameAdapter.hxx"

#include "RollbackHud.hxx"
#include "sf4e__GgpoAbortLatch.hxx"
#include "sf4e__GgpoGate.hxx"

#include <ggponet.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sfve {

class RollbackSession {
public:
	// Every kCheckpointInterval frames the session also keeps one hash per
	// region, so peers that disagree can find where. The latest five minutes
	// are kept.
	// Frame and region hashes here are the peers' (HashScope::Peers): two
	// processes playing the same match agree on them.
	static constexpr int kCheckpointInterval = 60;

	struct Config {
		int localPlayer = 0;               // 0 = P1, 1 = P2
		uint16_t localPort = 0;
		std::string remoteIp = "127.0.0.1";
		uint16_t remotePort = 0;
		int inputDelay = 0;                // frames
		int disconnectTimeoutMs = 5000;
		int disconnectNotifyStartMs = 750;
		bool verifyLoads = false;          // re-hash every restored state (hashes every save)
		// Frames between the state hashes HashAt keeps. Hashing a state costs
		// several times what copying it does and GGPO never reads it, so by
		// default only checkpoints are hashed; the loopback tests hash every frame.
		int hashEveryFrames = kCheckpointInterval;
		// Diagnostics: HashAt keeps every hashed frame of the session (8 bytes
		// a frame) instead of the latest 256, so two copies' hashes can be
		// compared frame by frame after a match, back to the first that differs.
		bool keepAllHashes = false;
	};

	enum class StepResult {
		NoSession,        // not started, or closed after a failure
		Waiting,          // synchronizing, or the gate holds the frame
		Advanced,         // one deterministic frame ran
		PredictionStall,  // too far ahead of the remote; frame not run
		Skipped,          // GGPO refused this frame for a transient reason
		Failed            // the session just failed; see FailureReason()
	};

	struct Stats {
		uint64_t framesAdvanced = 0;
		uint64_t rollbacks = 0;
		uint64_t resimulatedFrames = 0;
		int longestRollback = 0;
		uint64_t predictionStalls = 0;
		uint64_t loadMismatches = 0;       // verifyLoads only
		int timesyncFramesAhead = 0;       // last recommendation; the host paces
		uint64_t timesyncEvents = 0;
		uint64_t heldFrames = 0;           // StepHeld's frames
		uint64_t heldReplays = 0;          // held frames a rollback replayed (never expected)
	};

	explicit RollbackSession(GameAdapter& game);
	~RollbackSession();
	RollbackSession(const RollbackSession&) = delete;
	RollbackSession& operator=(const RollbackSession&) = delete;

	bool Start(const Config& config, std::string* error);
	// One outer frame: pumps the network, then adds the local input and runs
	// at most one deterministic frame (plus any rollback GGPO asks for).
	StepResult Step(PackedInput localInput);
	// One held frame: the game stands still (no Advance) while the session
	// goes on, so the players' inputs keep travelling. Steps only once the
	// frame's inputs are confirmed (Waiting until then), so `onInputs` sees
	// both players' final inputs, every bit of them, and runs before the next
	// frame is saved: a change it makes to the game is that frame's, on every
	// peer alike. Held frames take no input for a misprediction, and a
	// rollback that replays one does not advance the game either. The next
	// Step ends the hold.
	StepResult StepHeld(PackedInput localInput, const std::function<void(const FrameInputs&)>& onInputs);
	// The frames StepHeld played since the hold began (0 when not holding).
	int HeldFrames() const { return holding_ ? frame_ - heldFrom_ : 0; }
	// Only pumps the network (a frame the host holds back still has to
	// receive the other side's inputs and keep the connection alive).
	void Poll();
	void Close();

	bool Live() const { return session_ != nullptr; }
	bool Running() const { return gate_.phase == sf4e::gate::PHASE_RUNNING; }
	bool Failed() const { return failed_; }
	const std::string& FailureReason() const { return failure_; }
	int Frame() const { return frame_; }
	int LastConfirmedFrame() const;
	// GGPO's time-sync view of the remote player: how many frames each side
	// sees itself behind the other (half the difference is how far ahead this
	// side runs), and the round-trip time.
	struct Rift {
		double localFramesBehind;
		double remoteFramesBehind;
		int pingMs;
	};
	bool ReadRift(Rift* out) const;

	// The link as SF4 Ember's HUD reads its session (LinkSample), with the
	// same meanings:
	// * a connection warning: GGPO reported the peer interrupted and has not
	//   yet resumed or dropped it. It never holds the game (the gate's rule);
	// * a prediction stall: the last step's input was refused for running too
	//   far ahead of the peer;
	// * while warned, the time until GGPO drops the peer, from the timeout its
	//   interruption reported (-1 otherwise, and with no timeout);
	// * the input delay GGPO took for the local player (-1 if it refused it);
	// * the deepest rollback of the last second: the frames replayed after
	//   each restore (sf4e::RollbackHud, SF4's window).
	bool ConnectionWarning() const { return session_ && gate_.connectionWarningActive; }
	bool PredictionStalled() const { return session_ && gate_.predictionStalled; }
	int DisconnectCountdownMs() const;
	int AppliedDelay() const { return session_ ? appliedDelay_ : -1; }
	unsigned RecentRollbackFrames() const;
	// GGPO dropped the peer: the session failed for it (FailureReason says so),
	// and stays so until the next Start.
	bool PeerDisconnected() const { return peerDisconnected_; }
	// The countdown's arithmetic, as SF4 Ember's (fSystem::DisconnectCountdownMs):
	// GetTickCount's milliseconds, so a wrap between the two readings still counts.
	static int DisconnectCountdown(bool warned, uint32_t warnedAtMs, int timeoutMs, uint32_t nowMs);
	// Hash of the state saved for `frame`, if it was hashed (see
	// Config::hashEveryFrames) and is still in the history (or kept, see
	// Config::keepAllHashes). After the frame is confirmed this is final, so
	// peers can compare it.
	bool HashAt(int frame, uint64_t* out) const;
	// The saved bytes of `frame` while its save is still in the pool (the
	// latest frames), laid out by StateLayout::SaveTo; null otherwise.
	const uint8_t* SavedState(int frame) const;
	// The peers' per-region hashes of checkpoint `frame`, in canonical order.
	bool RegionHashesAt(int frame, std::vector<RegionHash>* out) const;
	int LatestCheckpoint(int atOrBefore) const;
	const Stats& GetStats() const { return stats_; }
	const StateLayout& Layout() const { return game_.Layout(); }

private:
	struct Slot {
		bool used = false;
		bool hashed = false;
		int frame = -1;
		uint64_t hash = 0;      // the peers' hash
		uint64_t localHash = 0; // verifyLoads: the full hash a restore must reproduce
		std::vector<uint8_t> bytes;
	};
	struct HashRecord {
		int frame = -1;
		uint64_t hash = 0;
	};
	class ActiveScope;

	static bool __cdecl OnBeginGame(const char*);
	static bool __cdecl OnSave(unsigned char** buffer, int* len, int* checksum, int frame);
	static bool __cdecl OnLoad(unsigned char* buffer, int len);
	static bool __cdecl OnLogState(char*, unsigned char*, int);
	static void __cdecl OnFree(void* buffer);
	static bool __cdecl OnAdvance(int flags);
	static bool __cdecl OnEvent(GGPOEvent* info);

	bool Save(unsigned char** buffer, int* len, int* checksum, int frame);
	bool Load(unsigned char* buffer);
	void Free(void* buffer);
	bool AdvanceFromRollback();
	FrameInputs InputsForThisFrame(const FrameInputs& inputs);
	void Event(const GGPOEvent& info);

	// Aborts now, or latches until the GGPO call on the stack returns.
	void Fail(const char* reason);
	void CloseNow();
	bool DrainPendingAbort();
	bool HandleResult(int ggpoError, const char* call, StepResult* out);

	GameAdapter& game_;
	GGPOSession* session_ = nullptr;
	int localHandle_ = -1;
	int remoteHandle_ = -1;
	Config config_;
	sf4e::gate::GgpoGateModel gate_{};
	sf4e::gate::AbortLatch latch_;
	std::vector<Slot> pool_;
	std::vector<HashRecord> history_;
	std::vector<HashRecord> allHashes_; // Config::keepAllHashes, indexed by frame
	std::map<int, std::vector<RegionHash>> checkpoints_;
	int frame_ = 0;
	// Held frames (StepHeld): from heldFrom_ up to heldTo_ (exclusive; the
	// current frame while holding_).
	bool holding_ = false;
	int heldFrom_ = -1, heldTo_ = -1;
	bool Held(int frame) const { return frame >= heldFrom_ && (holding_ || frame < heldTo_); }
	bool failed_ = false;
	std::string failure_;
	Stats stats_;
	// The link (ConnectionWarning and the rest). Presentation only: none of it
	// is rollback state, and none of it changes what the session plays.
	int appliedDelay_ = -1;
	int disconnectTimeoutMs_ = 0; // the open interruption's, 0 when none
	bool peerDisconnected_ = false;
	mutable sf4e::RollbackHud rollbackHud_; // Recent() drops the bursts that expired
};

} // namespace sfve
