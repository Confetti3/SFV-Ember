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

// Where the value a battle tick records for a player's pad comes from
// (SfvAdapter::OnInputRecord): the PC's own pad, the session's input while
// it advances the battle, or nothing pressed, for a room's or a replay's
// battle before its session.
enum class PadSource { Pads, Session, Neutral };
// What a tick records for a pad: the session's input, the pad itself, or,
// while a room's or a replay's battle loads (Neutral), `held`: nothing, or
// the costume code that launch gave the player (EMBER_LIBRARY.md sections
// 53 and 55), the same on every copy.
constexpr PackedInput RecordedInput(PadSource source, PackedInput pad, PackedInput session, PackedInput held = 0)
{
	return source == PadSource::Session ? session : source == PadSource::Neutral ? held : pad;
}

// What a scripted player plays (automated tests only; player builds never
// read the options that choose it).
enum class Script {
	// A grounded direction (never up: fighters stay where moves land) and
	// maybe an attack, held for six frames at a time.
	Buttons,
	// The game's moves, for desync sweeps: blocks, each one move of the
	// MoveSet, then Buttons for the rest of the block.
	Moves,
};

// The moves a Moves script plays (-sfve_moveset). Every one stays on the
// ground but the two full circles (which pass through up for a frame or two).
enum class MoveSet {
	// The system moves, in blocks of kMoveBlockFrames: V-Skill tap or hold,
	// V-Trigger once, twice and held, V-Reversal from a held block for punch
	// and kick, Critical Art motions for punch and kick (two quarter circles,
	// charge, two full circles), EX specials, throw, V-Trigger then Critical
	// Art.
	System,
	// Critical Arts that play out: each motion alone and after a V-Trigger,
	// in blocks long enough for the whole cinematic (kSuperBlockFrames).
	Supers,
	// Z29's (Cody's) knife, pipe and rock (docs/design/beta8/desync.md): the
	// V-Skill and its follow-ups, the V-Trigger with its second press, its
	// specials, its heavy normals, a walk over what it dropped, the
	// V-Trigger left to run out, and his Critical Art (two quarter circles
	// and a punch: the motion of the desyncs of 2026-11-11).
	Cody,
	// Projectile specials and V-Skills, and V-Triggers left to run out (Z33's
	// drop at the end of its V-Trigger reads the model's pose): for Z33 and
	// Z37, and anyone.
	Projectiles,
};
constexpr int kMoveBlockFrames = 96;
// A Critical Art's script runs to some 390 frames (Z29's script 1200: 388).
constexpr int kSuperBlockFrames = 480;
// With an idle opponent each block opens with a walk forward of this many
// frames, so the mover stays in range (ScriptConfig::idle).
constexpr int kApproachFrames = 40;

struct ScriptConfig {
	uint32_t seed = 0; // 0: the pad plays
	int from = 0;      // the script's first session frame
	bool padBefore = false; // before `from` the pad plays, otherwise nothing
	Script script = Script::Buttons;
	MoveSet moveSet = MoveSet::System;
	// This player (0 or 1) stands still from `from` on, pressing nothing, as a
	// Training dummy stands; -1 none. A mover against it opens each block of
	// moves with a walk forward (kApproachFrames).
	int idle = -1;
};

// The block of moves a script plays at a frame (ScriptedInput::Block).
struct Planned {
	const char* move = nullptr; // the move's name, as the log line says it
	int start = 0;              // its first frame, counted from ScriptConfig::from
	int frames = 0;             // the block's length
	bool facingRight = true;
	bool fighterFacing = false; // the facing is the fighter's, not the block's coin
};

// One player's scripted input. A function of (seed, player, frame) and, when
// the caller gives it, the fighter's facing at the first frame of each block
// of moves, so a copy produces it without its window having the focus and
// both copies of a synctest agree on it. Frames count from `from`, so a block
// of moves starts there. With no facing given (a copy that cannot read it)
// the block's coin decides, and the log line says which.
class ScriptedInput {
public:
	ScriptedInput(const ScriptConfig& config, int player);

	// The input for session frame `frame`, or none when the pad plays it.
	// `facingRight`: the fighter faces right now (SfvAdapter::FacingRight);
	// read at a block's first frame, it turns that block's forward and back
	// into right and left. Logs each block of moves once, as it starts.
	std::optional<PackedInput> At(int frame, std::optional<bool> facingRight = std::nullopt);

	// The block of moves At last played (its move null before any).
	const Planned& Block() const { return block_; }

private:
	void Seek(int played);

	ScriptConfig config_;
	uint32_t seed_;
	int player_;
	int blockIndex_ = -1;     // the block block_ describes
	bool blockFaced_ = false; // block_'s facing is decided
	int loggedBlock_ = -1;
	Planned block_;
};

} // namespace sfve::mod::input
