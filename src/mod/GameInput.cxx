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

} // namespace

PackedInput Mash(uint32_t seed, int frame)
{
	const uint32_t h = Scramble(seed * 0x9E3779B9U ^ (uint32_t)(frame / kHoldFrames));
	PackedInput in = kDirections[h % std::size(kDirections)];
	if ((h >> 8) % 3 == 0) in |= kAttacks[(h >> 12) % std::size(kAttacks)];
	return in;
}

} // namespace sfve::mod::input
