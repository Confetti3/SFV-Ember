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
	// A frame whose inputs do not matter is advanced with neutral inputs (but
	// for its ConfirmedBits) and its inputs are never taken for a
	// misprediction, so no rollback starts on it: frames the game cannot
	// replay faithfully are then never replayed from their own start.
	virtual bool InputsMatter() const { return true; }

	// The bits a frame's input can carry (those a pad can produce); the rest
	// are cleared before the game sees an input, so neither a peer nor a test
	// can hand the battle a value its own input code never makes.
	virtual PackedInput InputBits() const { return ~PackedInput{0}; }

	// The bits session frame `frame` still reads when it ignores the players'
	// inputs (InputsMatter false), from the state at its start; 0 when it
	// reads none (SFV: an intro's skip request, on its skip frames). Such a
	// frame reads them confirmed: a session steps it only once both players'
	// inputs for it are final, so no guess of them ever reaches the game and
	// no rollback reaches back past it. That wait needs an input delay of at
	// least one frame, as a held frame's does (RollbackSession::StepHeld).
	virtual PackedInput ConfirmedBits(int frame) const
	{
		(void)frame;
		return 0;
	}

	// Called right after the layout's memory was written back, for fix-ups a
	// plain copy cannot do (the game's own post-restore callbacks, state the
	// adapter keeps as a record). An adapter that keeps such a record keeps it
	// equal to the game after every Advance and AfterLoad, so saves and hashes
	// see it current.
	virtual void AfterLoad() {}

	// Why the adapter can no longer keep its state faithful to the game (a
	// record it can no longer mirror or write back), or null. Latched for the
	// rest of the battle: a session over it fails with this reason, since a
	// rollback would restore bytes without the game they describe.
	virtual const char* StateFailure() const { return nullptr; }
};

// The bits session frame `frame` reads of the players' inputs, from the state
// at its start: the pad's in the fight, the confirmed bits outside it (none
// on most such frames).
inline PackedInput BitsRead(const GameAdapter& game, int frame)
{
	return game.InputsMatter() ? game.InputBits() : game.ConfirmedBits(frame);
}

// What frame `frame` gives the game of these inputs. Every session path (a
// fighter's step and rollback, a watcher's and a replay's frame) and the
// synctest give the game this, so each plays the same frame alike. It is its
// own fixed point, so a replay that recorded it plays it back unchanged.
inline FrameInputs GivenInputs(const GameAdapter& game, const FrameInputs& inputs, int frame)
{
	return Restricted(inputs, BitsRead(game, frame));
}

// Frame `frame` reads bits that must be confirmed (GameAdapter::ConfirmedBits).
inline bool ReadsConfirmed(const GameAdapter& game, int frame)
{
	return !game.InputsMatter() && game.ConfirmedBits(frame) != 0;
}

} // namespace sfve
