#pragma once

// The battle's per-tick input value, as the game's input unit records it for
// a pad (observed in game with virtual pads, 2026-11-04): directions in bits
// 0-3, attacks in bits 4-11. Start is not part of it.

#include "core/GameAdapter.hxx"

#include <stdint.h>

namespace sfve::mod::input {

constexpr PackedInput kUp = 0x001;
constexpr PackedInput kDown = 0x002;
constexpr PackedInput kLeft = 0x004;
constexpr PackedInput kRight = 0x008;
// Attack bits seen from single pad buttons: X, Y, A, B, RB, RT on a default layout.
constexpr PackedInput kAttacks[] = {0x010, 0x020, 0x040, 0x080, 0x200, 0x800};
// Every bit a battle input can carry.
constexpr PackedInput kInputBits = 0xFFF;
// The attacks by name, as players' chords in recorded matches show them
// (2026-11-09: V-Trigger HP+HK 0xA00, V-Skill MP+MK 0xA0, throw LP+LK 0x50).
constexpr PackedInput kLP = 0x010, kMP = 0x020, kLK = 0x040, kMK = 0x080, kHP = 0x200, kHK = 0x800;

// Seeded random play for automated tests: a direction and maybe an attack,
// held for six frames at a time. A function of (seed, frame) only, so a copy
// produces it without its window having the focus.
PackedInput Mash(uint32_t seed, int frame);

// Seeded play of the game's system moves, for desync sweeps: blocks of
// kMoveBlockFrames, each one move (V-Skill tap or hold, V-Trigger and a
// second press while it is active, V-Reversal from a held block, Critical
// Art motions for punch and kick: two quarter circles either way, charge,
// two full circles; EX specials, throw), facing either way, then Mash for
// the rest of the block. A function of (seed, frame) only, like Mash.
constexpr int kMoveBlockFrames = 96;
PackedInput Moves(uint32_t seed, int frame);
// The move a block of Moves plays, for the log (`frame` anywhere in it).
const char* MoveName(uint32_t seed, int frame);

} // namespace sfve::mod::input
