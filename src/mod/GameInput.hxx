#pragma once

// The battle's per-tick input value, as the game's input unit records it for
// a pad (observed in game with virtual pads, 2026-11-04): directions in bits
// 0-3, attacks in bits 4-11. Start is not part of it.

#include "core/GameAdapter.hxx"

#include <stdint.h>

#include <optional>

namespace sfve::mod::input {

constexpr PackedInput kUp = 0x001;
constexpr PackedInput kDown = 0x002;
constexpr PackedInput kLeft = 0x004;
constexpr PackedInput kRight = 0x008;
// The attacks by name. Single pad buttons on a default layout: X, Y, A, B,
// RB, RT; players' chords in recorded matches (2026-11-09): V-Trigger
// HP+HK 0xA00, V-Skill MP+MK 0xA0, throw LP+LK 0x50.
constexpr PackedInput kLP = 0x010, kMP = 0x020, kLK = 0x040, kMK = 0x080, kHP = 0x200, kHK = 0x800;
constexpr PackedInput kAttacks[] = {kLP, kMP, kLK, kMK, kHP, kHK};
// Every bit a battle input can carry.
constexpr PackedInput kInputBits = 0xFFF;

// What a scripted player plays (automated tests only; player builds never
// read the options that choose it).
enum class Script {
	// A grounded direction (never up: fighters stay where moves land) and
	// maybe an attack, held for six frames at a time.
	Buttons,
	// The game's system moves, for desync sweeps: blocks of kMoveBlockFrames,
	// each one move (V-Skill tap or hold, V-Trigger and a second press while
	// it is active, V-Reversal from a held block, Critical Art motions for
	// punch and kick: two quarter circles, charge, two full circles; EX
	// specials, throw), then Buttons for the rest of the block.
	Moves,
};
constexpr int kMoveBlockFrames = 96;

struct ScriptConfig {
	uint32_t seed = 0; // 0: the pad plays
	int from = 0;      // the script's first session frame
	bool padBefore = false; // before `from` the pad plays, otherwise nothing
	Script script = Script::Buttons;
};

// One player's scripted input. A function of (seed, player, frame) only, so
// a copy produces it without its window having the focus, and both copies
// of a synctest agree on it; frames count from `from`, so a block of moves
// starts there. Facing is the block's coin, not the fighter's: the log line
// says which way the motion went.
class ScriptedInput {
public:
	ScriptedInput(const ScriptConfig& config, int player);

	// The input for session frame `frame`, or none when the pad plays it.
	// Logs each block of moves once, as it starts.
	std::optional<PackedInput> At(int frame);

private:
	ScriptConfig config_;
	uint32_t seed_;
	int player_;
	int loggedBlock_ = -1;
};

} // namespace sfve::mod::input
