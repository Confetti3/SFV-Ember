#pragma once

// SFV's fighters in the frame meter's terms (EMBER_LIBRARY.md section 39):
// what mod::FighterReader read after a tick, as the library's
// training::FighterSample, which training::FrameMeter measures, and the feed
// that gives the meter one tick after another. Pure: no game memory, so the
// tests run it on ticks recorded in game.
//
// The meter's vocabulary is the library's (training/FrameMeter.hxx): a status
// from ActorStatus, an action and its frame, a posture, the frames until the
// fighter can act, hitstop and an attack's phase. SFV gives each directly:
//   - the status is the fighter's state kind (sfv::fighter::StateKind);
//   - the attack's phase comes from the script's own header (its first and
//     last attack frames), or, for a script with no attack of its own, from
//     the fighter's projectile coming out, and a script the move plays after
//     its attack is its recovery;
//   - the frames until the fighter can act are the game's own count, the one
//     its Training's Attack Data subtracts for advantage.

#include "mod/FighterReader.hxx"

#include "training/FrameMeter.hxx"

#include <stdint.h>

#include <array>
#include <optional>

namespace sfve::adapter {

// The meter's status (training::ActorStatus) for a state kind and posture,
// or none for a kind not seen in game (sfv::fighter::StateKind).
std::optional<unsigned> MeterStatus(mod::sfv::fighter::StateKind kind, int posture);

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

// Gives the meter both fighters after each update of the battle, on a count
// of the battle's ticks: an update that ran more than one tick leaves a gap in
// it, and the meter starts over after the gap (FrameMeter::Observe), since
// the ticks between were not read.
class MeterFeed {
public:
	void Feed(sf4e::training::FrameMeter& meter, const std::array<mod::FighterFrame, 2>& fighters, uint32_t ticks);

private:
	std::array<MeterMove, 2> moves_{};
	int tick_ = 0;
};

} // namespace sfve::adapter
