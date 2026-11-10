#include "mod/GameInput.hxx"

#include "mod/Log.hxx"

#include <iterator>
#include <span>

namespace sfve::mod::input {

namespace {

constexpr int kHoldFrames = 6;
// Grounded directions only: a scripted fighter that jumps half the time
// rarely stands where supers, V-Triggers and throws land, and those are
// what the desync sweeps need played out (Kate, 2026-11-10). Motions that
// pass through up (the 720) still do, for a frame or two.
constexpr PackedInput kDirections[] = {0, kDown, kLeft, kRight, kDown | kLeft, kDown | kRight};

uint32_t Scramble(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

PackedInput Buttons(uint32_t seed, int frame)
{
	const uint32_t h = Scramble(seed * 0x9E3779B9U ^ (uint32_t)(frame / kHoldFrames));
	PackedInput in = kDirections[h % std::size(kDirections)];
	if ((h >> 8) % 3 == 0) in |= kAttacks[(h >> 12) % std::size(kAttacks)];
	return in;
}

// Directions relative to the fighter's facing; the block's coin turns them
// into the input's left and right.
constexpr uint8_t U = 1, D = 2, B = 4, F = 8;

struct Step {
	uint8_t dirs;
	PackedInput buttons;
	uint8_t frames;
};

// A motion is its directions; the press comes with the last one. Every move
// starts from two neutral frames, so its buttons are a fresh press.
constexpr Step kNeutral{0, 0, 2};
constexpr Step kVSkill[] = {kNeutral, {0, kMP | kMK, 4}};
constexpr Step kVSkillHold[] = {kNeutral, {0, kMP | kMK, 45}};
constexpr Step kVTrigger[] = {kNeutral, {0, kHP | kHK, 4}};
constexpr Step kVTriggerTwice[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 30}, {0, kHP | kHK, 4}};
constexpr Step kVTriggerHeld[] = {kNeutral, {0, kHP | kHK, 40}};
constexpr Step kVReversalP[] = {{B, 0, 40}, {F, kLP | kMP | kHP, 4}};
constexpr Step kVReversalK[] = {{B, 0, 40}, {F, kLK | kMK | kHK, 4}};
constexpr Step kQcfQcfP[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}};
constexpr Step kQcfQcfK[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kHK, 4}};
constexpr Step kChargeP[] = {{D | B, 0, 50}, {F, 0, 2}, {B, 0, 2}, {F, kHP, 4}};
constexpr Step kChargeK[] = {{D | B, 0, 50}, {F, 0, 2}, {B, 0, 2}, {F, kHK, 4}};
constexpr Step k720P[] = {{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 2}, {U, 0, 1},
	{U | F, 0, 1}, {F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 1}, {U, kHP, 3}};
constexpr Step k720K[] = {{F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 2}, {U, 0, 1},
	{U | F, 0, 1}, {F, 0, 2}, {D | F, 0, 2}, {D, 0, 2}, {D | B, 0, 2}, {B, 0, 2}, {U | B, 0, 1}, {U, kHK, 3}};
constexpr Step kExQcf[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, kLP | kMP, 4}};
constexpr Step kExDp[] = {kNeutral, {F, 0, 2}, {D, 0, 2}, {D | F, kLP | kMP, 4}};
constexpr Step kExQcb[] = {kNeutral, {D, 0, 2}, {D | B, 0, 2}, {B, kLK | kMK, 4}};
constexpr Step kExCharge[] = {{B, 0, 45}, {F, kLP | kMP, 4}};
constexpr Step kThrow[] = {kNeutral, {F, kLP | kLK, 4}};
constexpr Step kVTriggerThenCa[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 20}, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2},
	{D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}};

struct Move {
	const char* name;
	std::span<const Step> steps;
};

// Facing is the block's coin, so a quarter circle forward covers back too.
constexpr Move kMoves[] = {
	{"v-skill", kVSkill},
	{"v-skill hold", kVSkillHold},
	{"v-trigger", kVTrigger},
	{"v-trigger twice", kVTriggerTwice},
	{"v-trigger held", kVTriggerHeld},
	{"v-reversal punch", kVReversalP},
	{"v-reversal kick", kVReversalK},
	{"critical art qcf qcf p", kQcfQcfP},
	{"critical art qcf qcf k", kQcfQcfK},
	{"critical art charge p", kChargeP},
	{"critical art charge k", kChargeK},
	{"critical art 720 p", k720P},
	{"critical art 720 k", k720K},
	{"ex qcf pp", kExQcf},
	{"ex dp pp", kExDp},
	{"ex qcb kk", kExQcb},
	{"ex charge pp", kExCharge},
	{"throw", kThrow},
	{"v-trigger then critical art", kVTriggerThenCa},
};

consteval bool EveryMoveFitsItsBlock()
{
	for (const Move& move : kMoves) {
		int frames = 0;
		for (const Step& step : move.steps) frames += step.frames;
		if (frames > kMoveBlockFrames) return false;
	}
	return true;
}
static_assert(EveryMoveFitsItsBlock(), "a move longer than its block would be cut off by the Buttons that follow it");

PackedInput Directions(uint8_t dirs, bool facingRight)
{
	PackedInput in = 0;
	if (dirs & U) in |= kUp;
	if (dirs & D) in |= kDown;
	if (dirs & F) in |= facingRight ? kRight : kLeft;
	if (dirs & B) in |= facingRight ? kLeft : kRight;
	return in;
}

// The move a block plays and the way it faces.
struct Planned {
	const Move& move;
	bool facingRight;
};

Planned PlanAt(uint32_t seed, int block)
{
	const uint32_t h = Scramble(seed * 0x85EBCA6BU ^ 0x6D6F7665U ^ (uint32_t)block);
	return {kMoves[h % std::size(kMoves)], ((h >> 16) & 1) != 0};
}

PackedInput Moves(uint32_t seed, int frame)
{
	const Planned plan = PlanAt(seed, frame / kMoveBlockFrames);
	int at = frame % kMoveBlockFrames;
	for (const Step& step : plan.move.steps) {
		if (at < step.frames) return Directions(step.dirs, plan.facingRight) | step.buttons;
		at -= step.frames;
	}
	return Buttons(seed, frame);
}

} // namespace

ScriptedInput::ScriptedInput(const ScriptConfig& config, int player)
	// A second seed for P2 that no P1 seed shares by accident of small numbers.
	: config_(config), seed_(player == 0 ? config.seed : config.seed * 2654435761u + 1), player_(player)
{
}

std::optional<PackedInput> ScriptedInput::At(int frame)
{
	if (!config_.seed) return std::nullopt;
	if (frame < config_.from) return config_.padBefore ? std::nullopt : std::optional<PackedInput>(0);
	const int played = frame - config_.from;
	if (config_.script == Script::Buttons) return Buttons(seed_, played);
	const int block = played / kMoveBlockFrames;
	if (block != loggedBlock_) {
		loggedBlock_ = block;
		const Planned plan = PlanAt(seed_, block);
		Log("moves: P%d frame %d: %s, facing %s", player_ + 1, frame, plan.move.name,
			plan.facingRight ? "right" : "left");
	}
	return Moves(seed_, played);
}

} // namespace sfve::mod::input
