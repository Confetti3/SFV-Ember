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

// Seeded random play for automated tests: a direction and maybe an attack,
// held for six frames at a time. A function of (seed, frame) only, so a copy
// produces it without its window having the focus.
PackedInput Mash(uint32_t seed, int frame);

} // namespace sfve::mod::input
