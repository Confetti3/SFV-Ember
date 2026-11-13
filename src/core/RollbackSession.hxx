#pragma once

// A two-player GGPO session over one GameAdapter, and its watchers.
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
//   only ever holds pointers into it. A save no load can reach keeps no copy,
//   and a frame is hashed once, when its state is final, from its save
//   (docs/design/beta8/perf.md).
//
// GGPO callbacks carry no context pointer, so the session that is making a
// GGPO call is published in a scoped static for the callbacks to find. Two
// sessions in one process (the loopback tests) work because calls never nest
// across sessions.
//
// Watchers (EMBER_LIBRARY.md section 25), as SF4 Ember has them:
// * P1 streams every confirmed frame to each watcher (GGPO's spectator
//   players). A watcher never holds the fighters: one not synchronized in
//   time, or too far behind, is dropped (sf4e::SpectatorPolicy), and its
//   link's events never touch the fight's gate or end the fight.
// * A watching copy plays those frames from GGPO's spectator backend: the
//   same confirmed inputs both fighters played, so the same states. It keeps
//   no saves and never rolls back; it hashes the state of the same frames
//   the fighters hash, so the peers' checkpoints compare.
//
// Replays (EMBER_LIBRARY.md section 26):
// * Every session reports each frame it plays (Config::onFrame) and each
//   checkpoint's hash (Config::onCheckpoint), a rollback's replay of a frame
//   again, so a recorder that keeps the last report of each confirmed frame
//   holds the match as both fighters played it.
// * A replay plays such frames back as a watcher plays P1's stream
//   (Config::replay): the same path, the same held frames and hashes, with
//   no GGPO session and no network at all.

#include "core/GameAdapter.hxx"
#include "core/ReplayFrames.hxx"

#include "RollbackHud.hxx"
#include "SpectatorPolicy.hxx"
#include "sf4e__GgpoAbortLatch.hxx"
#include "sf4e__GgpoGate.hxx"

#include <ggponet.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sfve {

class SyncTester;
class LaggedReplay;

class RollbackSession {
public:
	// Every kCheckpointInterval frames the session also keeps one hash per
	// region, so peers that disagree can find where. The latest five minutes
	// are kept. (20 from beta.9, 60 before: a checkpoint every third of a
	// second of play instead of every second, and a replay's recorded
	// checkpoints, which are multiples of the interval it was recorded at, stay
	// valid at any divisor of it: ReplayFile accepts every multiple of 20.)
	// Frame and region hashes here are the peers' (HashScope::Peers): two
	// processes playing the same match agree on them.
	static constexpr int kCheckpointInterval = 20;
	// How many checkpoints' whole states are kept (CheckpointState): 120
	// frames of them, a state's size each, for the desync report's state file.
	static constexpr int kCheckpointStatesKept = 6;

	struct Config {
		int localPlayer = 0;               // 0 = P1, 1 = P2
		uint16_t localPort = 0;
		std::string remoteIp = "127.0.0.1";
		uint16_t remotePort = 0;
		int inputDelay = 0;                // frames
		int disconnectTimeoutMs = 5000;
		int disconnectNotifyStartMs = 750;
		bool verifyLoads = false;          // re-hash every restored state (and hash every frame)
		// Re-hashes a sample of the restored states: every this many loads,
		// the restored state's hash is compared with its save's, and a
		// mismatch is logged and counted (Stats::loadMismatches). 0: none.
		// verifyLoads checks every load.
		int verifyLoadsEvery = 0;
		// Cheaper rollbacks (docs/design/beta8/perf.md). Neither changes a
		// hash peers compare, a checkpoint, or what any load restores; false
		// plays as before, for comparison.
		// * A save no load can reach keeps no copy of the state: one made once
		//   both players' inputs for the frame after it are confirmed.
		bool skipUnloadableSaves = true;
		// * A frame is hashed once its state is final (every input before it
		//   confirmed), from its save, instead of on every save of it.
		bool hashOnConfirm = true;
		// Diagnostics and tests: each save that kept no copy, by frame.
		// Called inside GGPO's calls, so it must not call the session.
		std::function<void(int frame)> onSaveSkipped;
		// Frames between the state hashes HashAt keeps. Hashing a state costs
		// several times what copying it does and GGPO never reads it, so by
		// default only checkpoints are hashed; the loopback tests hash every frame.
		int hashEveryFrames = kCheckpointInterval;
		// Diagnostics: HashAt keeps every hashed frame of the session (8 bytes
		// a frame) instead of the latest 256, so two copies' hashes can be
		// compared frame by frame after a match, back to the first that differs.
		bool keepAllHashes = false;

		// P1 of a room's match also streams to these watchers: each one's
		// match slot (2 and up) and the loopback port of its link. Another
		// player's list is ignored: only P1 streams.
		struct Watcher {
			size_t slot = 0;
			uint16_t port = 0;
		};
		std::vector<Watcher> watchers;
		// How long P1 waits, once a fighter has synchronized, for a watcher
		// to synchronize before it drops it (SpectatorPolicy::SyncDeadlineFor).
		uint64_t watcherSyncDeadlineMs = sf4e::SpectatorPolicy::SyncDeadlineMs;
		// This copy only watches: it plays the confirmed frames P1 streams
		// from remoteIp:remotePort and sends nothing of its own. localPlayer,
		// inputDelay, the disconnect settings and verifyLoads do not apply.
		bool watching = false;
		// This copy plays a recorded battle back: frame N plays replay[N], as
		// a watcher plays P1's frame N, with no GGPO session. It implies
		// watching; the ports and the watcher list do not apply. A frame
		// whose kind is not the recording's (held where it was advanced, or
		// reading inputs where it ignored them) has left the recording, and
		// the session fails; so does the end of the frames. The frames must
		// outlive the session.
		const ReplayFrames* replay = nullptr;
		// Diagnostics, a replay only: the playback also makes the rollbacks a
		// session could (SyncTester in learning mode, each rolled-back stretch
		// first played with wrong inputs), up to this many frames deep at
		// random spacing from replayCheckSeed, and logs every region that a
		// rollback leaves different ("replay check:" and "learn:" lines), on the
		// recording's own inputs. 0: off. Held frames end the checks.
		int replayCheckDistance = 0;
		uint32_t replayCheckSeed = 1;
		// Diagnostics, a replay only: the playback plays as a fighter's copy
		// does when player replayLagPlayer's inputs arrive this many frames
		// late (LaggedReplay: guesses, rendered, then rollbacks), and hashes
		// each checkpoint once its state is final. 0: off. Takes the place of
		// replayCheckDistance. A held frame fails the playback.
		int replayLag = 0;
		int replayLagPlayer = 0;
		uint32_t replayLagJitter = 0; // nonzero: the delay wanders up to replayLag (LaggedReplay), from this seed
		// Each frame as this session plays it, before the game sees it: its
		// number and what it gave the game (RecordedFrame). A rollback's replay
		// reports the frame again; the last report of a confirmed frame is
		// final. Called inside GGPO's calls, so it must not call the session.
		std::function<void(int frame, const RecordedFrame& played)> onFrame;
		// Each checkpoint's peers' hash (every kCheckpointInterval frames), as
		// the "checkpoint:" log line has it; again for a frame saved again
		// (with Config::hashOnConfirm, once the frame is final, so again only
		// for a final frame a dropped fighter's reload saves again, alike).
		std::function<void(int frame, uint64_t hash)> onCheckpoint;
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
		uint64_t loadMismatches = 0;       // verifyLoads, verifyLoadsEvery
		uint64_t loadsVerified = 0;        // restores re-hashed (verifyLoads, verifyLoadsEvery)
		uint64_t savesSkipped = 0;         // saves that kept no copy (skipUnloadableSaves)
		uint64_t hashesOnConfirm = 0;      // frames hashed once final, after their save (hashOnConfirm)
		uint64_t hashesDropped = 0;        // saves a rollback replaced before they were final: never hashed
		uint64_t skippedLoads = 0;         // loads of a save that kept no copy (never expected; fails the session)
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
	// at most one deterministic frame (plus any rollback GGPO asks for). A
	// frame that reads bits only confirmed (GameAdapter::ConfirmedBits) runs
	// once its inputs are confirmed, Waiting until then, as StepHeld's do. A
	// watcher's runs the next frame P1 streamed, Waiting until it has come;
	// `localInput` is unused.
	StepResult Step(PackedInput localInput);
	// One held frame: the game stands still (no Advance) while the session
	// goes on, so the players' inputs keep travelling. Steps only once the
	// frame's inputs are confirmed (Waiting until then), so `onInputs` sees
	// both players' final inputs, every bit of them, and runs before the next
	// frame is saved: a change it makes to the game is that frame's, on every
	// peer alike. Held frames take no input for a misprediction, and a
	// rollback that replays one does not advance the game either. The next
	// Step ends the hold. A watcher's held frame is the next frame P1
	// streamed, with the fighters' inputs; it sends nothing.
	StepResult StepHeld(PackedInput localInput, const std::function<void(const FrameInputs&)>& onInputs);
	// The frames StepHeld played since the hold began (0 when not holding).
	int HeldFrames() const { return holding_ ? frame_ - heldFrom_ : 0; }
	// Only pumps the network (a frame the host holds back still has to
	// receive the other side's inputs and keep the connection alive).
	void Poll();
	void Close();

	// A GGPO session, or a replay playing (Config::replay).
	bool Live() const { return session_ != nullptr || replaying_; }
	// This session plays a replay (Config::replay).
	bool Replaying() const { return replaying_; }
	bool Running() const { return gate_.phase == sf4e::gate::PHASE_RUNNING; }
	bool Failed() const { return failed_; }
	const std::string& FailureReason() const { return failure_; }
	int Frame() const { return frame_; }
	// The last frame whose inputs both fighters have given. A watcher plays
	// only such frames, so for it every state it has reached is final: its
	// current frame.
	int LastConfirmedFrame() const;

	// This copy watches (Config::watching).
	bool Watching() const { return config_.watching; }
	// The side this copy plays (Config::localPlayer: 0 P1, 1 P2).
	int LocalPlayer() const { return config_.localPlayer; }
	// A watcher's frames received from P1 and not played yet; -1 when the
	// session cannot tell (not a watcher's, or none).
	int WatchedBacklog() const;
	// P1's watcher streams still served: none dropped, disconnected or left.
	size_t WatcherStreams() const;
	// The match slots of the watchers P1 dropped or lost since the last call,
	// each once, for the room to end their links.
	std::vector<size_t> TakeDroppedWatchers() { return links_.Take(); }
	// The fight is over and the session is kept only so P1's watchers get
	// the frames still on their way: the other fighter leaving is expected
	// then, and no warning or failure of theirs is recorded.
	void KeepForWatchers();
	bool KeptForWatchers() const { return keptForWatchers_; }
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
	// Config::keepAllHashes). With Config::hashOnConfirm a frame is hashed
	// once its state is final (frame - 1 confirmed), so there is no hash of
	// it before; without, each save of it is hashed, and the hash is final
	// once the frame is. Peers compare final hashes.
	bool HashAt(int frame, uint64_t* out) const;
	// The saved bytes of `frame` while its save is still in the pool (the
	// latest frames), laid out by StateLayout::SaveTo; null otherwise, and
	// for a save that kept no copy (Config::skipUnloadableSaves).
	const uint8_t* SavedState(int frame) const;
	// The save of `frame` keeps its copy whether or not a load can reach it,
	// so SavedState answers it (a dump of that frame). Until changed.
	void KeepSave(int frame) { keepSave_ = frame; }
	// The peers' per-region hashes of checkpoint `frame`, in canonical order.
	bool RegionHashesAt(int frame, std::vector<RegionHash>* out) const;
	// The whole state at checkpoint `frame` as last hashed (the layout's
	// regions one after another), while it is one of the last
	// kCheckpointStatesKept checkpoints; null otherwise. A desync report keeps it, so two copies'
	// states can be compared byte by byte.
	const std::vector<uint8_t>* CheckpointState(int frame) const;
	int LatestCheckpoint(int atOrBefore) const;
	const Stats& GetStats() const { return stats_; }
	const StateLayout& Layout() const { return game_.Layout(); }

private:
	struct Slot {
		bool used = false;
		bool copied = false;     // bytes hold the frame's state; false: a save no load can reach
		bool pending = false;    // to be hashed once the frame is final (Config::hashOnConfirm)
		int frame = -1;
		long long copyTicks = 0; // the copy's time, for the checkpoint line
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
	// The hash of `frame`'s state, kept for HashAt, and at a checkpoint its
	// regions' hashes, its bytes (CheckpointState) and the checkpoint's log
	// line (with the copy's time, `copyTicks` of QueryPerformanceCounter).
	// `bytes` is its save, laid out by StateLayout::SaveTo, or null when the
	// live state is that frame's and its save kept no copy.
	void RecordHash(int frame, const uint8_t* bytes, long long copyTicks);
	// Whether frame `frame`'s state is hashed at all: every checkpoint, and
	// every Config::hashEveryFrames frame (every frame with verifyLoads).
	bool HashedFrame(int frame) const;
	// The state saved as `frame` is final: the inputs of every frame before
	// it are confirmed (as of the last reading, confirmed_), so no rollback
	// saves it again with other contents (the rule the desync check's
	// sf4e::statehash::IsConfirmedCheckpoint has).
	bool Final(int frame) const { return frame - 1 <= confirmed_; }
	// Reads the last confirmed frame, then hashes the pending saves it made
	// final, in frame order (Config::hashOnConfirm). Run after every GGPO
	// call that may confirm frames, and before each save.
	void HashConfirmed();
	void HashPending(Slot& slot);
	// Runs HashConfirmed as the session call it is made in returns, whatever
	// way it returns.
	class ConfirmedOnReturn;
	bool Load(unsigned char* buffer);
	void Free(void* buffer);
	bool AdvanceFromRollback();
	// The frame about to be advanced as the game gets it; reported to
	// onFrame as this frame's.
	FrameInputs InputsForThisFrame(const FrameInputs& inputs);
	// What the game gets of `inputs` on the frame about to be advanced
	// (GivenInputs), reported to onFrame; logged when a frame outside the
	// fight gives it bits it reads confirmed (an intro's skip), which every
	// copy then logs on the same frame.
	FrameInputs GivenAndReported(const FrameInputs& inputs);
	void Report(const FrameInputs& inputs, uint8_t flags) const
	{
		if (config_.onFrame) config_.onFrame(frame_, RecordedFrame{inputs, flags});
	}
	// A replay's next frame, checked against what the game does with it;
	// false (the session failed) at the end of the frames or where the
	// playback left the recording.
	bool ReplayInputs(bool held, FrameInputs* inputs);
	void Event(const GGPOEvent& info);

	// A watcher's frame: the next one P1 streamed, run (or, `onHeld`, held
	// with its inputs), and its state hashed as the fighters hash theirs.
	StepResult StepWatched(const std::function<void(const FrameInputs&)>* onHeld);
	// A watcher's state at `frame`, hashed where the fighters hash it.
	void HashWatched(int frame);
	// P1's watchers: drops the ones SpectatorPolicy gives up on. Outside
	// GGPO callbacks only (ggpo_disconnect_player).
	void PollWatchers();
	void DropWatcher(int handle, const char* why);
	// A handle that is neither fighter's. On P1 that is a watcher's; a
	// watching copy's one link is P1's, never a watcher's.
	bool WatcherHandle(GGPOPlayerHandle handle) const;

	// Aborts now, or latches until the GGPO call on the stack returns.
	void Fail(const char* reason);
	// Fails the session (Fail) when the game reports state it can no longer
	// keep faithful (GameAdapter::StateFailure); true when it did.
	bool FailedByGame();
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
	// The last frame whose inputs both players have given, as last read
	// (HashConfirmed: ggpo_get_confirmed_input_frame, the local input added
	// since GGPO's last poll included). Free cannot ask GGPO, which may be
	// closing.
	int confirmed_ = -1;
	// A rollback is replaying from rollbackFrom_ up to rollbackEnd_ (from its
	// Load to that frame's save, or the end of the GGPO call): the inputs
	// that started it are confirmed already, but the saves after the frame
	// it loaded still hold the guessed timeline until each is saved again, so
	// none of them is hashed meanwhile (Replaced).
	bool rollingBack_ = false;
	int rollbackFrom_ = -1, rollbackEnd_ = -1;
	bool Replaced(int frame) const { return rollingBack_ && frame > rollbackFrom_; }
	bool closing_ = false;     // GGPO is freeing its saves as it closes
	bool tearingDown_ = false; // the destructor's Close: the game may be gone
	int keepSave_ = -1;    // KeepSave
	uint64_t loads_ = 0;   // restores, for Config::verifyLoadsEvery
	std::vector<HashRecord> history_;
	std::vector<HashRecord> allHashes_; // Config::keepAllHashes, indexed by frame
	std::map<int, std::vector<RegionHash>> checkpoints_;
	struct KeptState {
		int frame = -1;
		std::vector<uint8_t> bytes;
	};
	KeptState checkpointStates_[kCheckpointStatesKept]; // the latest checkpoints' states (CheckpointState)
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

	// Watchers. P1: who is still served, when to drop one, and the slots of
	// those dropped (SF4's SpectatorPolicy and SpectatorLinks). A watcher:
	// whether P1's stream ever started (SpectatorStreamWatch) and whether
	// frame 0's state was hashed.
	sf4e::SpectatorPolicy policy_;
	sf4e::SpectatorLinks links_;
	std::set<int> watcherHandles_;
	sf4e::SpectatorStreamWatch streamWatch_;
	bool watchedStartHashed_ = false;
	bool keptForWatchers_ = false;
	// A replay plays (Config::replay): Live with no GGPO session.
	bool replaying_ = false;
	// The replay's rollback checks (Config::replayCheckDistance), and the
	// regions they have logged.
	std::unique_ptr<SyncTester> replayCheck_;
	std::unique_ptr<LaggedReplay> lagged_;
	int laggedHashed_ = 0;      // checkpoints hashed through this frame
	bool laggedFinished_ = false;
	void HashLagged();
	int replayCheckFailures_ = 0;
	std::set<std::string> replayCheckLogged_;
	void LogReplayCheck();
};

} // namespace sfve
