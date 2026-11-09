#pragma once

// SFV's fighters in the frame meter's terms (EMBER_LIBRARY.md section 39):
// what mod::FighterReader read after a tick, as the library's
// training::FighterSample, which training::FrameMeter measures. Pure: no game
// memory, so the tests run it on recorded ticks.
//
// The meter's vocabulary is the library's (training/FrameMeter.hxx): a status
// from ActorStatus, an action and its frame, a posture, the frames until the
// fighter can act, hitstop and an attack's phase. SFV gives each directly:
//   - the status is the fighter's state kind (sfv::fighter: the state
//     machine's current state, by its place in the game's state table);
//   - the attack's phase comes from the script's own header (its first and
//     last attack frames), or, for a script with no attack of its own, from
//     the fighter's projectile coming out, and a script the move plays after
//     its attack is its recovery;
//   - the frames until the fighter can act are the game's own count, the one
//     its Training's Attack Data subtracts for advantage.

#include "mod/FighterReader.hxx"

#include "training/FrameMeter.hxx"

#include <optional>

namespace sfve::adapter {

// SFV's state kinds (the kind of each state in the game's table), as section
// 39 lists what each was seen to be in game. A kind not listed was not seen.
enum class SfvState : int {
	Stand = 1, Crouch = 2, PreJump = 3, Jump = 4, Landing = 5, WalkForward = 6, WalkBack = 7, DashForward = 8,
	DashBack = 9, Guard = 12, StandToCrouch = 14, CrouchToStand = 15, TurnStand = 16, TurnCrouch = 17, TurnWalk = 18,
	Attack = 19, Hit = 22, GuardHit = 23, LaunchHit = 24, Down = 25, LyingDown = 26, Rise = 27, Held = 29
};

// The meter's status (training::ActorStatus) for a state kind and posture,
// or none for a kind section 39 does not name.
std::optional<unsigned> MeterStatus(int stateKind, int posture);

// An attack's phase from the script's header alone (training::FighterSample's
// hitPhase): 0 before its attack, 1 while its attack is out, 2 before a later
// attack of the same move, 3 after; -1 when the header names no attack.
int HeaderPhase(const mod::ScriptHeader& header, float frame, bool attackedBefore);

// The move a fighter's attack belongs to (the action its last state change
// started) and whether it has had an attack out yet.
struct MeterMove {
	int action = -1;
	bool attacked = false;
};

// A fighter's sample after a tick. move is its move so far, which the tick
// carries on or replaces: a script with no attack of its own needs it, as the
// move's startup before its attack and its recovery after.
sf4e::training::FighterSample MeterSample(const mod::FighterFrame& fighter, MeterMove& move);

} // namespace sfve::adapter
