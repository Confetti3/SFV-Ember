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
// misprediction, so every rollback starts on a fight frame (replaying across
// a fight's end, which the flow record handles). The flow itself is part of
// the rollback state, so a guessed KO that a rollback removes puts the fight
// back. A room match's end is decided on a confirmed frame. Time sync
// stretches frames by a few milliseconds while this side runs ahead
// (FramePacer).
//
// Pipe commands (cross-copy desync checks): "hash <frame>", "checkpoint
// [frame]" (latest confirmed per-region checkpoint), "dump <frame>" (that
// frame's saved state to a file, once it is confirmed) and "freeze <frame>"
// (hold the battle at that frame so both screens can be compared). Test aid:
// "perturb <frame> <region name or size> <offset> <add>" adds to a 32-bit
// value of this copy's state once the session has played that frame: a real
// desync, for checking that the peers' hashes then differ.

#include "core/RollbackSession.hxx"
#include "ember/RoomBattle.hxx"
#include "mod/BattleMode.hxx"
#include "mod/FramePacer.hxx"
#include "mod/ModConfig.hxx"

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

	// Ember's outer tick (EmberHost), on the game thread between battle
	// frames: a room's battle whose scene has gone, with the main menu back,
	// is closed and reported to the runtime.
	void PollBattle() override;
	// SFV's own update runs after the tick (the scene manager's), not in it.
	void UpdateGame() override {}

private:
	void RegisterCommands();
	// Back to the main menu; the battle stands still meanwhile. A leave the
	// scene manager cannot take yet is asked again on the next frame.
	void Leave();
	// Starts the session once the match is known; false while it is not.
	bool StartSession(SfvAdapter& adapter);
	void DumpIfRequested();
	void PerturbIfDue();

	// Without a room: the same in every copy.
	static constexpr uint32_t kSessionSeed = 0x5F0E5EED;

	const ModConfig& config_;
	mutable std::mutex mutex_; // the game thread steps, the pipe thread reads
	std::unique_ptr<RollbackSession> session_;
	adapter::RoomBattle room_; // what the room hears of its battle
	bool started_ = false;
	uint32_t seed_ = kSessionSeed; // the session's: a rematch in place reseeds from it
	uint32_t startTime_ = 0;       // the match's start time, kept the session's (Frame)
	bool leaveWanted_ = false; // the battle goes back to the main menu
	bool leaving_ = false;     // ...and the scene manager took the leave
	// The runtime asked the battle to end (Retire, Abort, Fail); the next
	// battle frame ends it.
	std::atomic<bool> exitAsked_{false};
	FramePacer pacer_;
	int dumpFrame_ = -1;   // "dump <frame>": write that frame's saved state to the log folder
	std::vector<uint8_t> dumpBytes_; // its latest save, until it is confirmed
	int freezeFrame_ = -1; // "freeze <frame>": stop advancing there (to compare screens)
	struct Perturbation {
		int frame = -1; // none pending
		std::string region;
		size_t offset = 0;
		int32_t add = 0;
	};
	Perturbation perturb_;
	RollbackSession::StepResult last_ = RollbackSession::StepResult::NoSession;
};

} // namespace sfve::mod
