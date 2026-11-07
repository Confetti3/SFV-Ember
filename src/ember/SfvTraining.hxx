#pragma once

// SFV's Training under Ember's runtime (EMBER_LIBRARY.md section 27): the
// library's Training service (ember::game::Training, sf4e__GameAdapter.hxx)
// over SFV's own Training mode, as SF4 Ember's is over SF4's.
//
// What Ember adds is what SF4 Ember adds; Training itself is the game's:
//   - Training from Ember's Home: the game's main menu goes to Ember's
//     Training select (the player's pick, the dummy and the stage), whose
//     Start loads SFV's Training battle with them (VersusLaunch's jump,
//     target 2). From a room's "Wait in Training" the battle loads at once
//     with the player's pick and the last dummy: the room stays joined.
//   - While a room member waits there, the room shows them in Training (the
//     runtime's SayTraining reads ControlsAvailable), and an opponent who
//     sits down calls them back: the battle leaves for the main menu after
//     the call's countdown (training::LeaveCountdown, the library's model),
//     the way the game's own pause menu leaves it (EXIT_BATTLE: MatchEnd's
//     LeaveBattle), and the shell opens on their table there.
//   - Go now cuts the countdown short: the gameplay pad's View, or Enter,
//     taken where they arrive (SfvShell) through the library's GoNowGate.
// The dummy's settings, recording and playback, the frame data display and
// the position reset are SFV's own Training menu's, left to the game.
//
// The battle is followed from the battle hooks' record (mod/GameScene's
// LastBattleSeen): SFV's Training battle is not a room's, so the netplay
// mode plays it unchanged (RunOriginal).

#include "mod/VersusLaunch.hxx"

#include "sf4e/sf4e__GameAdapter.hxx"

#include <cstdint>
#include <string>

namespace sfve::adapter {

// The library's Training service for SFV, which SfvGame's adapter hands out.
ember::game::Training& SfvTrainingService();

// Starts following SFV's Training battles, once a frame on the game thread.
// Once, from the netplay mode's Install, with rooms on.
void InstallSfvTraining();

// What the overlay draws over a Training battle (SfvHud): whether one is
// running and followed, its generation (the battle's number), and the call's
// countdown: frames left before it leaves, 0 when no call counts, and the
// call it counts for. Any thread.
struct TrainingView {
	bool available = false;
	std::uint64_t generation = 0;
	int leavingIn = 0;
	std::uint64_t leavingCall = 0;
};
TrainingView ReadTrainingView();

// The game is paused over the Training battle (its own pause menu): no
// battle tick ran in the last update. Any thread.
bool TrainingPaused();

// Training select asked for by Home's Training row (Training::Request at the
// idle main menu): true once per request, for the shell, which then shows its
// Training select. Render thread.
bool TakeTrainingSelectRequest();

// Training select's Start: SFV's Training battle with `player` as P1 and
// `dummy` as P2 on `stage` (codes as VersusLaunch names them), from the main
// menu. `why` is for the log. Any thread; the launch runs on the game thread
// and a refusal or failure pushes `notice` (a catalog key) as the runtime's
// notice.
void LaunchTraining(const mod::FighterPick& player, const mod::FighterPick& dummy, const std::wstring& stage,
	const char* why, const char* notice);

} // namespace sfve::adapter
