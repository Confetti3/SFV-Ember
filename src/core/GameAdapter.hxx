#pragma once

// The seam between the game-agnostic rollback core and one game.
//
// SfvAdapter (src/mod) implements it over Street Fighter V's own rollback
// state; MockFighter (src/testbed) implements it over a small deterministic
// fighter so the session, the synctest harness and desync detection are
// tested without the game.

#include "core/StateRegions.hxx"

#include <stdint.h>

#include <string>

namespace sfve {

constexpr int kPlayers = 2;

// One player's input for one frame, in the form the game's battle reads it
// (after button configuration is applied), so both peers feed identical
// values into the simulation whatever their local layouts are.
using PackedInput = uint32_t;

struct FrameInputs {
	PackedInput p[kPlayers] = {};
	bool operator==(const FrameInputs&) const = default;
};

// `inputs` with every bit outside `bits` cleared.
inline FrameInputs Restricted(FrameInputs inputs, PackedInput bits)
{
	for (PackedInput& p : inputs.p) p &= bits;
	return inputs;
}

class GameAdapter {
public:
	virtual ~GameAdapter() = default;

	// Game build plus the adapter's state layout. Two peers may only play each
	// other when these match exactly.
	virtual std::string BuildId() const = 0;

	// Everything one frame of battle reads or writes. Stable for the length of
	// one match; the session sizes its save pool from it at start.
	virtual StateLayout& Layout() = 0;

	// Runs exactly one deterministic battle tick with these inputs.
	// `resimulating` is true for frames replayed after a rollback: the game
	// should not start sounds or one-shot effects for them.
	virtual void Advance(const FrameInputs& inputs, bool resimulating) = 0;

	// Whether the next frame reads the players' inputs, from the state at its
	// start (SFV: only the fight does; intros, KOs and round calls do not).
	// A frame whose inputs do not matter is advanced with neutral inputs and
	// its inputs are never taken for a misprediction, so no rollback starts
	// on it: frames the game cannot replay faithfully are then never
	// replayed from their own start.
	virtual bool InputsMatter() const { return true; }

	// The bits a frame's input can carry (those a pad can produce); the rest
	// are cleared before the game sees an input, so neither a peer nor a test
	// can hand the battle a value its own input code never makes.
	virtual PackedInput InputBits() const { return ~PackedInput{0}; }

	// Called right after the layout's memory was written back, for fix-ups a
	// plain copy cannot do (the game's own post-restore callbacks, state the
	// adapter keeps as a record). An adapter that keeps such a record keeps it
	// equal to the game after every Advance and AfterLoad, so saves and hashes
	// see it current.
	virtual void AfterLoad() {}
};

} // namespace sfve
