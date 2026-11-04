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

	// Called right after the layout's memory was written back, for fix-ups a
	// plain copy cannot do (the game's own post-restore callbacks, state the
	// adapter keeps as a record). An adapter that keeps such a record keeps it
	// equal to the game after every Advance and AfterLoad, so saves and hashes
	// see it current.
	virtual void AfterLoad() {}
};

} // namespace sfve
