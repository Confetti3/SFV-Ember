#pragma once

// netplay: the battle runs through a GGPO RollbackSession with the peer on
// -sfve_peer_port (loopback for two copies on one PC). One rendered frame is
// one session Step: GGPO advances the game (and replays rolled-back frames
// inside that Step), or holds it while waiting for the peer.
//
// The local player's input is what the game computed from this copy's pad 0
// (whichever side it plays), so button layouts apply before the input is
// shared; or, for automated tests, seeded random play (-sfve_mash=N), which
// needs no window focus.
//
// Rollback only while the players fight. Outside the fight (intros, round
// calls, the KO and results) the battle flow waits on presentation the
// snapshot does not hold, so there frames run in lockstep: only once both
// inputs are confirmed. The flow itself is part of the rollback state, so a
// guessed KO that a rollback removes puts the fight back. Time sync stretches
// frames by a few milliseconds while this side runs ahead (FramePacer).
//
// Pipe commands (cross-copy desync checks): "hash <frame>", "checkpoint
// [frame]" (latest confirmed per-region checkpoint), "dump <frame>" (that
// frame's saved state to a file) and "freeze <frame>" (hold the battle at that
// frame so both screens can be compared).

#include "core/RollbackSession.hxx"
#include "mod/BattleMode.hxx"
#include "mod/FramePacer.hxx"
#include "mod/HelperLink.hxx"
#include "mod/ModConfig.hxx"

#include <memory>
#include <mutex>

namespace sfve::mod {

class NetplayMode : public BattleMode {
public:
	// `link`: the match comes from Ember's helper (its ports, seed and delay)
	// instead of the command line; the battle waits at its first frame until
	// the link is ready.
	NetplayMode(const ModConfig& config, std::unique_ptr<HelperLink> link)
		: config_(config), link_(std::move(link))
	{
	}

	void NewBattle(SfvAdapter& adapter) override;
	void Frame(SfvAdapter& adapter) override;
	std::string Describe() const override;
	void RegisterCommands();

private:
	bool HoldForLockstep(const SfvAdapter& adapter);
	// Starts the session once the match is known; false while it is not.
	bool StartSession(SfvAdapter& adapter);
	void DumpIfRequested();

	// Without a helper link: the same in every copy (the link hands out one per match).
	static constexpr uint32_t kSessionSeed = 0x5F0E5EED;

	const ModConfig& config_;
	std::unique_ptr<HelperLink> link_; // null: ports from the command line
	mutable std::mutex mutex_; // the game thread steps, the pipe thread reads
	std::unique_ptr<RollbackSession> session_;
	bool started_ = false;
	uint64_t frames_ = 0;
	uint64_t lockstepHolds_ = 0;
	FramePacer pacer_;
	int dumpFrame_ = -1;   // "dump <frame>": write that frame's saved state to the log folder
	int freezeFrame_ = -1; // "freeze <frame>": stop advancing there (to compare screens)
	RollbackSession::StepResult last_ = RollbackSession::StepResult::NoSession;
};

} // namespace sfve::mod
