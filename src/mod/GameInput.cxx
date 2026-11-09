#include "mod/GameInput.hxx"

#include <iterator>

namespace sfve::mod::input {

namespace {

constexpr int kHoldFrames = 6;
constexpr PackedInput kDirections[] = {0, kUp, kDown, kLeft, kRight, kUp | kLeft, kUp | kRight, kDown | kLeft,
	kDown | kRight};

uint32_t Scramble(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

// Directions relative to the fighter's facing; Moves turns them into the
// input's left and right for the block's side.
constexpr uint8_t U = 1, D = 2, B = 4, F = 8;
constexpr PackedInput kPunches = kLP | kMP | kHP, kKicks = kLK | kMK | kHK;

struct Step {
	uint8_t dirs;
	PackedInput buttons;
	uint8_t frames;
};

struct Move {
	const char* name;
	Step steps[16];
};

// Motions at two frames a step; the button comes with the last direction.
constexpr Move kMoves[] = {
	{"v-skill", {{0, 0, 2}, {0, kMP | kMK, 4}}},
	{"v-skill hold", {{0, 0, 2}, {0, kMP | kMK, 45}}},
	{"v-trigger", {{0, 0, 2}, {0, kHP | kHK, 4}}},
	{"v-trigger twice", {{0, 0, 2}, {0, kHP | kHK, 4}, {0, 0, 30}, {0, kHP | kHK, 4}}},
	{"v-trigger held", {{0, 0, 2}, {0, kHP | kHK, 40}}},
	{"v-reversal punch", {{B, 0, 40}, {F, kPunches, 4}}},
	{"v-reversal kick", {{B, 0, 40}, {F, kKicks, 4}}},
	{"critical art qcf qcf p",
		{{D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}}},
	{"critical art qcf qcf k",
		{{D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kHK, 4}}},
	{"critical art qcb qcb p",
		{{D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, kHP, 4}}},
	{"critical art qcb qcb k",
		{{D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, kHK, 4}}},
	{"critical art charge p", {{D | B, 0, 50}, {F, 0, 2}, {B, 0, 2}, {F, kHP, 4}}},
	{"critical art charge k", {{D | B, 0, 50}, {F, 0, 2}, {B, 0, 2}, {F, kHK, 4}}},
	{"critical art 720 p",
		{{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 2}, {U, 0, 1}, {U | F, 0, 1},
			{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 1}, {U, kHP, 3}}},
	{"critical art 720 k",
		{{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 2}, {U, 0, 1}, {U | F, 0, 1},
			{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 1}, {U, kHK, 3}}},
	{"ex qcf pp", {{D, 0, 2}, {D | F, 0, 2}, {F, kLP | kMP, 4}}},
	{"ex dp pp", {{F, 0, 2}, {D, 0, 2}, {D | F, kLP | kMP, 4}}},
	{"ex qcb kk", {{D, 0, 2}, {D | B, 0, 2}, {B, kLK | kMK, 4}}},
	{"ex charge pp", {{B, 0, 45}, {F, kLP | kMP, 4}}},
	{"throw", {{0, 0, 2}, {F, kLP | kLK, 4}}},
	{"v-trigger then critical art",
		{{0, kHP | kHK, 4}, {0, 0, 20}, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}}},
};

PackedInput Directions(uint8_t dirs, bool facingRight)
{
	PackedInput in = 0;
	if (dirs & U) in |= kUp;
	if (dirs & D) in |= kDown;
	if (dirs & F) in |= facingRight ? kRight : kLeft;
	if (dirs & B) in |= facingRight ? kLeft : kRight;
	return in;
}

uint32_t BlockHash(uint32_t seed, int frame)
{
	return Scramble(seed * 0x85EBCA6BU ^ 0x6D6F7665U ^ (uint32_t)(frame / kMoveBlockFrames));
}

} // namespace

PackedInput Moves(uint32_t seed, int frame)
{
	const uint32_t h = BlockHash(seed, frame);
	const Move& move = kMoves[h % std::size(kMoves)];
	const bool facingRight = (h >> 16) & 1;
	int at = frame % kMoveBlockFrames;
	for (const Step& step : move.steps) {
		if (!step.frames) break;
		if (at < step.frames) return Directions(step.dirs, facingRight) | step.buttons;
		at -= step.frames;
	}
	return Mash(seed, frame);
}

const char* MoveName(uint32_t seed, int frame)
{
	return kMoves[BlockHash(seed, frame) % std::size(kMoves)].name;
}

PackedInput Mash(uint32_t seed, int frame)
{
	const uint32_t h = Scramble(seed * 0x9E3779B9U ^ (uint32_t)(frame / kHoldFrames));
	PackedInput in = kDirections[h % std::size(kDirections)];
	if ((h >> 8) % 3 == 0) in |= kAttacks[(h >> 12) % std::size(kAttacks)];
	return in;
}

} // namespace sfve::mod::input
