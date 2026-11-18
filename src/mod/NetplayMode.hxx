#pragma once

// netplay: the battle runs through a GGPO RollbackSession. One rendered frame
// is one session Step: GGPO advances the game (and replays rolled-back frames
// inside that Step), or holds it while waiting for the peer.
//
// Where the match comes from:
// - With -sfve_room=1 the mode hosts Ember's runtime and shell (Install), and
//   a battle the room's launch loaded is the room's (adapter::RoomBattle):
//   the room's match session gives the ports, seed and delay, the session
//   starts once every member's battle has loaded, and the battle reports to
//   the runtime (EMBER_LIBRARY.md sections 10, 12 and 14). Any other battle
//   plays offline.
// - Otherwise, with -sfve_peer_port: the ports on the command line, for two
//   copies on one PC.
// - A room member who watches the table plays the same battle from P1's
//   stream of the fighters' confirmed frames and sends nothing (section 25).
// - A replay's launch (Ember's Replays screen) loads a battle that plays a
//   recorded room battle back as a watcher plays one, from the recording's
//   frames (section 26).
//
// Before a session's first frame every copy of the match (P2 and each
// watcher against P1) checks that it starts from P1's state, and takes P1's
// bytes where it does not (core/StartAgreement.hxx), over the session's own
// ports before GGPO binds them; the battle stands still meanwhile. A replay
// compares its start with the one its header records.
//
// Every room battle this copy fights or watches is recorded: the frames its
// session played, kept as they become final, and saved as a replay with the
// battle's launch and seed when the battle closes (section 26).
//
// The local player's input is what the game computed from this copy's pad 0
// (whichever side it plays), so button layouts apply before the input is
// shared; or, for automated tests, seeded random play (-sfve_mash=N), which
// needs no window focus.
//
// Rollback starts only in the fight. Outside it (intros, round calls, the KO
// and results) the battle flow waits on presentation the snapshot does not
// hold, so a frame there must never be replayed from its own start. The
// game ignores the players' inputs there (SfvAdapter::InputsMatter): such a
// frame gets neutral inputs and GGPO takes no input on it for a
// misprediction (but for the intro's skip request on its skip frames, which
// are stepped confirmed, IntroSkip.hxx), so every rollback starts on a fight
// frame (replaying across a fight's end, which the flow record handles). The
// flow itself is part of the rollback state, so a guessed KO that a rollback
// removes puts the fight back. A room match's end is decided on a confirmed
// frame. Time sync stretches frames by a few milliseconds while this side
// runs ahead (FramePacer).
//
// The pipe's test and desync-check commands ("hash", "checkpoint", "dump",
// "freeze", "perturb", "skipafter", "vital", "riftgain") are
// NetplayDevCommands'.
//
// Where it lives: NetplayMode.cxx steps the frames; NetplayModeStart.cxx
// starts a battle's session (settings, start state agreement, recording's
// wiring). Composed: the battle's recording (MatchRecording), its scenes'
// log (SceneNotes) and the pipe's commands (NetplayDevCommands).

#include "core/ReplayRecorder.hxx"
#include "core/ReplaySeeker.hxx"
#include "core/RollbackSession.hxx"
#include "core/StartAgreement.hxx"
#include "ember/RoomBattle.hxx"
#include "mod/BattleMode.hxx"
#include "mod/FramePacer.hxx"
#include "mod/IntroSkip.hxx"
#include "mod/MatchRecording.hxx"
#include "mod/ModConfig.hxx"
#include "mod/NetplayDevCommands.hxx"
#include "mod/NetplayPhase.hxx"
#include "mod/SceneNotes.hxx"

#include "sf4e/sf4e__RuntimeTick.hxx"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace sfve::mod {

class NetplayMode : public BattleMode, public ember::GameTick {
public:
	explicit NetplayMode(const ModConfig& config) : config_(config) {}

	// The pipe's commands; with -sfve_room=1 also Ember's runtime (EmberHost,
	// ticking this mode), its room test commands and its shell.
	void Install() override;
	void NewBattle(SfvAdapter& adapter) override;
	void Frame(SfvAdapter& adapter) override;
	std::string Describe() const override;
	// Never in a battle a session plays: a room's (fighting or watching), a
	// replay's, a linked test's. Training and offline battles keep the
	// game's pause. Game thread, inside Frame's update (mutex_ is held).
	bool GamePauseAllowed() const override;

	// Ember's outer tick (EmberHost), on the game thread between battle
	// frames: a room's battle whose scene has gone, with the main menu back,
	// is closed and reported to the runtime.
	void PollBattle() override;
	// SFV's own update runs after the tick (the scene manager's), not in it.
	void UpdateGame() override {}

private:
	void RegisterCommands();
	// The battle's phase moved by `event` (netplay::Next); an event the phase
	// never sees is logged and changes nothing.
	void Enter(netplay::Event event);
	// A frame of a battle going back to the main menu: the leave asked again
	// until the scene manager takes it, the session still answering.
	void HoldWhileLeaving();
	// A frame of a Playing battle: offline, watched, replayed, the held WIN,
	// or the fight (and the match's end) stepped.
	void PlayFrame(SfvAdapter& adapter);
	// Over the WIN: a held frame carrying both fighters' answers.
	void PlayHeldWin(SfvAdapter& adapter);
	// A fighter's frame: held for a decision on a confirmed frame (or a
	// "freeze"), else the local input sent and one frame stepped.
	void StepFight(SfvAdapter& adapter, bool frozen);
	// Back to the main menu; the battle stands still meanwhile. A leave the
	// scene manager cannot take yet is asked again on the next frame.
	void Leave();
	// Starts the session once the match is known and the copies agree on
	// their start state (Preparing, then Agreeing); false while the battle
	// waits for either.
	bool StartSession(SfvAdapter& adapter);
	// The session's settings once the match is known, the start state made
	// (reseeded, a Practice room's gauges) and its agreement begun: Waiting
	// while the match is not known, NoSession when this battle has none.
	enum class Prepared { Waiting, NoSession, Ready };
	Prepared PrepareStart(SfvAdapter& adapter);
	// The start state's agreement (core/StartAgreement.hxx) over the
	// session's own ports, before GGPO takes them: P1 is the reference, P2
	// and every watcher follow it. A replay has none; it compares its start
	// with its header's (adapter::NotePlaybackStart).
	void BeginStartAgreement(SfvAdapter& adapter);
	// True once it has finished (a repair is reported then) and its port is
	// free for GGPO.
	bool AgreeStart(SfvAdapter& adapter);
	void EndStartAgreement();
	// A watcher's frame (EMBER_LIBRARY.md section 25): the next frame P1
	// streamed and, while this copy is behind, a few more (SF4's
	// SpectatorCatchUp), each through the decisions a fighter's frame takes.
	void WatchFrames(SfvAdapter& adapter);
	// One of them; false when no frame was played.
	bool WatchFrame(SfvAdapter& adapter);
	// A replay's engine frame: what its playback asks of this one (ember/
	// SfvPlayback.cxx): a frame by the transport, or a seek on its way, which
	// puts back a state first when it goes back and plays on to its frame, up to
	// 30 updates an engine frame, each through the decisions a watcher's frame takes.
	void PlayReplay(SfvAdapter& adapter);
	// P1's session kept for its watchers after the battle: closed if it is
	// not already, and freed.
	void EndDrain();
	// The battle's session becomes the drain kept for the watchers (the one
	// place a drain is assigned): a drain still kept from before closes first,
	// and with no session there is nothing to keep.
	void BeginDrain();
	// The first frame of each round's fight, on a fighter's copy and a watcher's
	// alike: logs the battle frame's state word (SceneNotes) and gives the
	// fighters the "vital" command's shares (NetplayDevCommands).
	// Game thread, inside Frame's step with mutex_ held (the shares are read under it).
	void NoteFightStart(SfvAdapter& adapter, int frame);
	// The room battle's recording: its frames made final so far (after any
	// step), by the battle's session or the one kept for its watchers.
	void ConfirmRecording();
	// This copy's player's input for `frame`: the pad's (or a test's script)
	// in the fight, and in the decided match's scenes the press that may skip
	// them (IntroSkip.hxx).
	PackedInput LocalInput(SfvAdapter& adapter, int frame);

	// Without a room: the same in every copy.
	static constexpr uint32_t kSessionSeed = 0x5F0E5EED;

	const ModConfig& config_;
	input::ScriptedInput script_{config_.script, config_.player}; // the local player's, for tests
	intro_skip::Request introSkip_; // the local player's ask to skip the intros and the decided match's scenes
	SceneNotes scenes_;             // the scenes' and fights' log lines
	NetplayDevCommands dev_;        // the pipe's test commands and what they ask of the battle
	mutable std::mutex mutex_; // the game thread steps, the pipe thread reads
	std::unique_ptr<RollbackSession> session_;
	// P1's last session, kept after its battle for the watchers still
	// streaming (RoomBattle::Close) and polled between battles, until the
	// runtime's drain ends (adapter::SetWatcherDrain).
	std::unique_ptr<RollbackSession> drain_;
	adapter::RoomBattle room_; // what the room hears of its battle
	// Where the battle is in its life (NetplayPhase.hxx); Unbound exactly
	// while session_ is null.
	netplay::Phase phase_ = netplay::Phase::Unbound;
	// While Agreeing: the session's settings, and the start state's agreement
	// with its socket on the session's local port.
	struct PendingStart {
		RollbackSession::Config config;
		uint32_t seed = 0;
		bool practice = false;
	};
	PendingStart pending_;
	std::unique_ptr<UdpStartChannel> startChannel_;
	std::unique_ptr<StartAgreement> startAgreement_;
	// P1's peers' hash of the start state as agreed (this copy's own when P1's
	// is not known), and the address ranges the copies agreed on, for the
	// replay's header.
	uint64_t startHash_ = 0;
	std::vector<AddressRange> startAddresses_;
	uint32_t seed_ = kSessionSeed; // the session's: a rematch in place reseeds from it
	uint32_t startTime_ = 0;       // the match's start time, kept the session's (Frame)
	// The runtime asked the battle to end (Retire, Abort, Fail); the next
	// battle frame ends it.
	std::atomic<bool> exitAsked_{false};
	FramePacer pacer_;
	RollbackSession::StepResult last_ = RollbackSession::StepResult::NoSession;
	// The room battle being recorded (EMBER_LIBRARY.md section 26).
	MatchRecording recording_;
	// The replay this battle plays back, from its bind to its close, and why
	// its playback ended.
	std::shared_ptr<const replay::Replay> replay_;
	std::string replayEnd_;
	// The states its playback takes as it plays, for the seeks that go back.
	ReplaySeeker seeker_;
	// The last checkpoint whose regions a playback logged (-sfve_replay_regions).
};

} // namespace sfve::mod
