#pragma once

// Inputs that bring the mock fighters together and make them trade hits, so
// every code path (movement, jumps, hits, hitstun, RNG) runs in a few hundred
// frames. A function of (player, frame, seed) only, so both peers of a
// loopback session can produce the other's inputs for comparison.

#include "testbed/MockFighter.hxx"

namespace sfve::testbed {

inline PackedInput ScriptedInput(int player, int frame, uint32_t seed)
{
	uint32_t h = seed ^ (uint32_t)(frame * 2654435761u) ^ (uint32_t)(player * 40503u);
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	PackedInput in = 0;
	const bool approach = (h % 10) < 7;
	if (approach) in |= kForward;
	else if ((h % 10) == 7) in |= kBack;
	if ((h >> 4) % 17 == 0) in |= kUp;
	if ((h >> 9) % 4 == 0) in |= kPunch;
	return in;
}

inline FrameInputs ScriptedFrame(int frame, uint32_t seed)
{
	FrameInputs f;
	for (int p = 0; p < kPlayers; p++) f.p[p] = ScriptedInput(p, frame, seed);
	return f;
}

} // namespace sfve::testbed
