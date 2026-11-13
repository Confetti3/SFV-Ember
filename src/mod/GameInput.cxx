#include "mod/GameInput.hxx"

#include "mod/Log.hxx"

#include <initializer_list>
#include <iterator>
#include <span>

namespace sfve::mod::input {

namespace {

constexpr int kHoldFrames = 6;
// Grounded directions only: a scripted fighter that jumps half the time
// rarely stands where supers, V-Triggers and throws land, and those are
// what the desync sweeps need played out (the maintainer, 2026-11-10). Motions that
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

// Directions relative to the fighter's facing; the block's facing (the
// fighter's, or the block's coin) turns them into the input's left and right.
constexpr uint8_t U = 1, D = 2, B = 4, F = 8;

struct Step {
	uint8_t dirs;
	PackedInput buttons;
	uint16_t frames;
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

// Critical Arts that play out (MoveSet::Supers). A V-Trigger's activation
// takes its time before the next motion is read: 30 neutral frames.
constexpr Step kCaQcfQcfLp[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2}, {D, 0, 2}, {D | F, 0, 2}, {F, kLP, 4}};
constexpr Step kVtCaQcfQcfP[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 30}, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2},
	{D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}};
constexpr Step kVtCaQcfQcfK[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 30}, {D, 0, 2}, {D | F, 0, 2}, {F, 0, 2},
	{D, 0, 2}, {D | F, 0, 2}, {F, kHK, 4}};
constexpr Step kVtCaChargeP[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 30}, {D | B, 0, 50}, {F, 0, 2}, {B, 0, 2},
	{F, kHP, 4}};

// Items and thrown things (MoveSet::Cody, and the V-Trigger's end for
// MoveSet::Projectiles). A V-Trigger left to run out ends in a crouching
// block held for 600 frames: the gauge drains, and what the trigger handed
// the fighter is dropped (Z29's part, rva 0x3C2C20 and 0x3C2C90; Z33's,
// rva 0x3BD900).
constexpr Step kVSkillPunches[] = {kNeutral, {0, kMP | kMK, 4}, {0, 0, 20}, {0, kLP, 4}, {0, 0, 20}, {0, kMP, 4},
	{0, 0, 20}, {0, kHP, 4}};
constexpr Step kVSkillTwice[] = {kNeutral, {0, kMP | kMK, 4}, {0, 0, 30}, {0, kMP | kMK, 4}};
constexpr Step kVTriggerThrice[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 40}, {0, kHP | kHK, 4}, {0, 0, 40},
	{0, kHP | kHK, 4}};
constexpr Step kVTriggerSpecials[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 40}, {D, 0, 2}, {D | F, 0, 2},
	{F, kMP, 4}, {0, 0, 40}, {D, 0, 2}, {D | F, 0, 2}, {F, kMK, 4}, {0, 0, 40}, {D, 0, 2}, {D | B, 0, 2},
	{B, kMP, 4}, {0, 0, 40}, {D, 0, 2}, {D | B, 0, 2}, {B, kMK, 4}};
constexpr Step kVTriggerHeavies[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 40}, {0, kHP, 4}, {0, 0, 30}, {0, kHK, 4},
	{0, 0, 30}, {D, kHP, 4}, {0, 0, 30}, {F, kMP, 4}, {0, 0, 30}, {B, kHP, 4}};
constexpr Step kVTriggerThrowWalk[] = {kNeutral, {0, kHP | kHK, 4}, {0, 0, 40}, {0, kHP | kHK, 4}, {0, 0, 30},
	{F, 0, 60}, {D, 0, 10}, {D, kLP, 4}, {0, 0, 10}, {D, kLK, 4}};
constexpr Step kVTriggerRunOut[] = {kNeutral, {0, kHP | kHK, 4}, {D | B, 0, 600}};

// Projectile specials (MoveSet::Projectiles).
constexpr Step kQcfLp[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, kLP, 4}};
constexpr Step kQcfMp[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, kMP, 4}};
constexpr Step kQcfHp[] = {kNeutral, {D, 0, 2}, {D | F, 0, 2}, {F, kHP, 4}};
constexpr Step kQcbP[] = {kNeutral, {D, 0, 2}, {D | B, 0, 2}, {B, kHP, 4}};
constexpr Step kQcbK[] = {kNeutral, {D, 0, 2}, {D | B, 0, 2}, {B, kHK, 4}};
constexpr Step kChargeBfP[] = {{B, 0, 45}, {F, kHP, 4}};
constexpr Step kDpP[] = {kNeutral, {F, 0, 2}, {D, 0, 2}, {D | F, kHP, 4}};

// Target combos and chained buttons (MoveSet::Luke, Akira, Targets). A step's
// frames are counted from the press's first frame, so "MP at +10" is a 4-frame
// press, six neutral frames, and the next press. Where a chain's gaps are
// named, they are press to press: kLukeSlow is LP at frame 0, MP at +10, HP at
// +31 (a player's, from the field), kLukeTight LP, MP at +8, HP at +18.
constexpr Step kLukeSlow[] = {kNeutral, {0, kLP, 4}, {0, 0, 6}, {0, kMP, 4}, {0, 0, 17}, {0, kHP, 4}, {0, 0, 40}};
constexpr Step kLukeTight[] = {kNeutral, {0, kLP, 4}, {0, 0, 4}, {0, kMP, 4}, {0, 0, 6}, {0, kHP, 4}, {0, 0, 40}};
// ...then QCB+LP, the field replay's follow-up (the motion's last direction
// carries the press, as every motion here does).
constexpr Step kLukeSlowQcb[] = {kNeutral, {0, kLP, 4}, {0, 0, 6}, {0, kMP, 4}, {0, 0, 17}, {0, kHP, 4}, {0, 0, 30},
	{D, 0, 2}, {D | B, 0, 2}, {B, kLP, 4}};
constexpr Step kLukeTightQcb[] = {kNeutral, {0, kLP, 4}, {0, 0, 4}, {0, kMP, 4}, {0, 0, 6}, {0, kHP, 4}, {0, 0, 30},
	{D, 0, 2}, {D | B, 0, 2}, {B, kLP, 4}};
constexpr Step kLukeQcb[] = {kNeutral, {D, 0, 2}, {D | B, 0, 2}, {B, kLP, 4}};

// Akira's V-Skill (MP+MK): taps at varying gaps and the 13-frame hold the field
// showed, then one attack after a hold or a tap, which is what the launcher
// of V-Skill I reads (which attack it takes is for the run to show).
constexpr Step kAkiraTapsShort[] = {kNeutral, {0, kMP | kMK, 4}, {0, 0, 6}, {0, kMP | kMK, 4}, {0, 0, 9}, {0, kMP | kMK, 4}};
constexpr Step kAkiraTapsLong[] = {kNeutral, {0, kMP | kMK, 4}, {0, 0, 21}, {0, kMP | kMK, 4}, {0, 0, 38}, {0, kMP | kMK, 4}};
constexpr Step kAkiraHold13[] = {kNeutral, {0, kMP | kMK, 13}};
constexpr Step kAkiraHold13Tap[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 12}, {0, kMP | kMK, 4}};
constexpr Step kAkiraHold13Lp[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kLP, 4}};
constexpr Step kAkiraHold13Mp[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kMP, 4}};
constexpr Step kAkiraHold13Hp[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kHP, 4}};
constexpr Step kAkiraHold13Lk[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kLK, 4}};
constexpr Step kAkiraHold13Mk[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kMK, 4}};
constexpr Step kAkiraHold13Hk[] = {kNeutral, {0, kMP | kMK, 13}, {0, 0, 8}, {0, kHK, 4}};
constexpr Step kAkiraTapLpLate[] = {kNeutral, {0, kMP | kMK, 4}, {0, 0, 24}, {0, kLP, 4}, {0, 0, 10}, {0, kHP, 4}};

// Chained buttons at 6-12 frame gaps, press to press neutral frames between
// (MoveSet::Targets). Each press is four frames.
constexpr Step kLpMp6[] = {kNeutral, {0, kLP, 4}, {0, 0, 6}, {0, kMP, 4}};
constexpr Step kLpMp12[] = {kNeutral, {0, kLP, 4}, {0, 0, 12}, {0, kMP, 4}};
constexpr Step kMpHp8[] = {kNeutral, {0, kMP, 4}, {0, 0, 8}, {0, kHP, 4}};
constexpr Step kMpHp11[] = {kNeutral, {0, kMP, 4}, {0, 0, 11}, {0, kHP, 4}};
constexpr Step kLkMk9[] = {kNeutral, {0, kLK, 4}, {0, 0, 9}, {0, kMK, 4}};
constexpr Step kLkMk6[] = {kNeutral, {0, kLK, 4}, {0, 0, 6}, {0, kMK, 4}};
constexpr Step kLpMpHp6_10[] = {kNeutral, {0, kLP, 4}, {0, 0, 6}, {0, kMP, 4}, {0, 0, 10}, {0, kHP, 4}};
constexpr Step kLpMpHp12_8[] = {kNeutral, {0, kLP, 4}, {0, 0, 12}, {0, kMP, 4}, {0, 0, 8}, {0, kHP, 4}};
constexpr Step kMkHk7[] = {kNeutral, {0, kMK, 4}, {0, 0, 7}, {0, kHK, 4}};
constexpr Step kMkHk12[] = {kNeutral, {0, kMK, 4}, {0, 0, 12}, {0, kHK, 4}};
// The same chains from a crouch (a direction held down is still grounded).
constexpr Step kCrLpMp8[] = {kNeutral, {D, kLP, 4}, {D, 0, 8}, {D, kMP, 4}};
constexpr Step kCrLkMk8[] = {kNeutral, {D, kLK, 4}, {D, 0, 8}, {D, kMK, 4}};

struct Move {
	const char* name;
	std::span<const Step> steps;
	int block = kMoveBlockFrames; // the block's length, from the move's first frame
};

// Facing is the fighter's when the caller can read it, otherwise the block's
// coin, so a quarter circle forward covers back too.
constexpr Move kSystem[] = {
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

constexpr Move kSupers[] = {
	{"critical art qcf qcf p", kQcfQcfP, kSuperBlockFrames},
	{"critical art qcf qcf lp", kCaQcfQcfLp, kSuperBlockFrames},
	{"critical art qcf qcf k", kQcfQcfK, kSuperBlockFrames},
	{"critical art charge p", kChargeP, kSuperBlockFrames},
	{"critical art charge k", kChargeK, kSuperBlockFrames},
	{"critical art 720 p", k720P, kSuperBlockFrames},
	{"critical art 720 k", k720K, kSuperBlockFrames},
	{"v-trigger then critical art qcf qcf p", kVtCaQcfQcfP, kSuperBlockFrames},
	{"v-trigger then critical art qcf qcf k", kVtCaQcfQcfK, kSuperBlockFrames},
	{"v-trigger then critical art charge p", kVtCaChargeP, kSuperBlockFrames},
};

constexpr Move kCody[] = {
	{"v-skill then each punch", kVSkillPunches, 240},
	{"v-skill twice", kVSkillTwice, 160},
	{"v-skill hold", kVSkillHold, 160},
	{"v-trigger then the trigger twice more", kVTriggerThrice, 240},
	{"v-trigger then specials", kVTriggerSpecials, 300},
	{"v-trigger then heavy normals", kVTriggerHeavies, 300},
	{"v-trigger, the trigger again, walk over it", kVTriggerThrowWalk, 240},
	{"v-trigger left to run out", kVTriggerRunOut, 660},
	{"critical art qcf qcf lp", kCaQcfQcfLp, kSuperBlockFrames},
	{"critical art qcf qcf p", kQcfQcfP, kSuperBlockFrames},
	{"v-trigger then critical art qcf qcf p", kVtCaQcfQcfP, kSuperBlockFrames},
};

constexpr Move kProjectiles[] = {
	{"qcf lp", kQcfLp, 160},
	{"qcf mp", kQcfMp, 160},
	{"qcf hp", kQcfHp, 160},
	{"ex qcf pp", kExQcf, 160},
	{"qcb p", kQcbP, 160},
	{"qcb k", kQcbK, 160},
	{"charge back forward p", kChargeBfP, 160},
	{"ex charge pp", kExCharge, 160},
	{"dp p", kDpP, 160},
	{"v-skill", kVSkill, 160},
	{"v-skill hold", kVSkillHold, 160},
	{"v-trigger then the trigger twice more", kVTriggerThrice, 240},
	{"v-trigger left to run out", kVTriggerRunOut, 660},
	{"critical art qcf qcf p", kQcfQcfP, kSuperBlockFrames},
};

// Block lengths: a chain with its recovery, long enough for the hits to
// land and the fighter to stand again before the next block.
constexpr int kTargetBlock = 120;
constexpr Move kLuke[] = {
	{"luke target lp mp hp (field gaps)", kLukeSlow, kTargetBlock},
	{"luke target lp mp hp (tight)", kLukeTight, kTargetBlock},
	{"luke target lp mp hp (field gaps) then qcb lp", kLukeSlowQcb, 200},
	{"luke target lp mp hp (tight) then qcb lp", kLukeTightQcb, 200},
	{"luke qcb lp", kLukeQcb, 120},
};

constexpr Move kAkira[] = {
	{"akira v-skill taps (short gaps)", kAkiraTapsShort, 160},
	{"akira v-skill taps (long gaps)", kAkiraTapsLong, 160},
	{"akira v-skill hold 13", kAkiraHold13, 120},
	{"akira v-skill hold 13 then tap", kAkiraHold13Tap, 160},
	{"akira v-skill hold 13 then lp", kAkiraHold13Lp, 160},
	{"akira v-skill hold 13 then mp", kAkiraHold13Mp, 160},
	{"akira v-skill hold 13 then hp", kAkiraHold13Hp, 160},
	{"akira v-skill hold 13 then lk", kAkiraHold13Lk, 160},
	{"akira v-skill hold 13 then mk", kAkiraHold13Mk, 160},
	{"akira v-skill hold 13 then hk", kAkiraHold13Hk, 160},
	{"akira v-skill tap then lp, hp", kAkiraTapLpLate, 160},
};

constexpr Move kTargets[] = {
	{"lp>mp (gap 6)", kLpMp6, kTargetBlock},
	{"lp>mp (gap 12)", kLpMp12, kTargetBlock},
	{"mp>hp (gap 8)", kMpHp8, kTargetBlock},
	{"mp>hp (gap 11)", kMpHp11, kTargetBlock},
	{"lk>mk (gap 9)", kLkMk9, kTargetBlock},
	{"lk>mk (gap 6)", kLkMk6, kTargetBlock},
	{"lp>mp>hp (gaps 6, 10)", kLpMpHp6_10, kTargetBlock},
	{"lp>mp>hp (gaps 12, 8)", kLpMpHp12_8, kTargetBlock},
	{"mk>hk (gap 7)", kMkHk7, kTargetBlock},
	{"mk>hk (gap 12)", kMkHk12, kTargetBlock},
	{"crouching lp>mp (gap 8)", kCrLpMp8, kTargetBlock},
	{"crouching lk>mk (gap 8)", kCrLkMk8, kTargetBlock},
};

constexpr std::span<const Move> MovesOf(MoveSet set)
{
	switch (set) {
	case MoveSet::Luke: return kLuke;
	case MoveSet::Akira: return kAkira;
	case MoveSet::Targets: return kTargets;
	case MoveSet::Supers: return kSupers;
	case MoveSet::Cody: return kCody;
	case MoveSet::Projectiles: return kProjectiles;
	case MoveSet::System: break;
	}
	return kSystem;
}

consteval bool EveryMoveFitsItsBlock()
{
	for (const MoveSet set : {MoveSet::System, MoveSet::Supers, MoveSet::Cody, MoveSet::Projectiles, MoveSet::Luke,
		     MoveSet::Akira, MoveSet::Targets}) {
		for (const Move& move : MovesOf(set)) {
			int frames = 0;
			for (const Step& step : move.steps) frames += step.frames;
			if (move.block <= 0 || frames > move.block) return false;
		}
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

// A block's move and coin come from one hash of the seed and the block's
// number (the same as the system moves' before move sets: their sweeps play
// what they played).
uint32_t PlanHash(uint32_t seed, int block)
{
	return Scramble(seed * 0x85EBCA6BU ^ 0x6D6F7665U ^ (uint32_t)block);
}

const Move& MoveAt(MoveSet set, uint32_t seed, int block)
{
	const std::span<const Move> moves = MovesOf(set);
	return moves[PlanHash(seed, block) % moves.size()];
}

bool CoinAt(uint32_t seed, int block)
{
	return ((PlanHash(seed, block) >> 16) & 1) != 0;
}

} // namespace

ScriptedInput::ScriptedInput(const ScriptConfig& config, int player)
	// A second seed for P2 that no P1 seed shares by accident of small numbers.
	: config_(config), seed_(player == 0 ? config.seed : config.seed * 2654435761u + 1), player_(player)
{
}

// Moves block_ to the block that holds `played` (frames from the script's
// first): forward from the current block, or from the first when `played`
// lies before it.
void ScriptedInput::Seek(int played)
{
	const bool approach = config_.idle >= 0 && config_.idle != player_;
	const auto describe = [&](int index, int start) {
		const Move& move = MoveAt(config_.moveSet, seed_, index);
		blockIndex_ = index;
		blockFaced_ = false;
		block_ = Planned{move.name, start, move.block + (approach ? kApproachFrames : 0), CoinAt(seed_, index), false};
	};
	if (blockIndex_ < 0 || played < block_.start) describe(0, 0);
	while (played >= block_.start + block_.frames) describe(blockIndex_ + 1, block_.start + block_.frames);
}

std::optional<PackedInput> ScriptedInput::At(int frame, std::optional<bool> facingRight)
{
	if (!config_.seed) return std::nullopt;
	if (frame < config_.from) return config_.padBefore ? std::nullopt : std::optional<PackedInput>(0);
	if (config_.idle == player_) return PackedInput(0);
	const int played = frame - config_.from;
	if (config_.script == Script::Buttons) return Buttons(seed_, played);
	Seek(played);
	if (!blockFaced_) {
		blockFaced_ = true;
		if (facingRight) {
			block_.facingRight = *facingRight;
			block_.fighterFacing = true;
		}
	}
	if (blockIndex_ != loggedBlock_) {
		loggedBlock_ = blockIndex_;
		Log("moves: P%d frame %d: %s, facing %s (%s)", player_ + 1, frame, block_.move, block_.facingRight ? "right" : "left",
			block_.fighterFacing ? "the fighter's" : "the block's coin");
	}
	int at = played - block_.start;
	if (config_.idle >= 0) {
		if (at < kApproachFrames) return Directions(F, block_.facingRight);
		at -= kApproachFrames;
	}
	for (const Step& step : MoveAt(config_.moveSet, seed_, blockIndex_).steps) {
		if (at < step.frames) return Directions(step.dirs, block_.facingRight) | step.buttons;
		at -= step.frames;
	}
	return Buttons(seed_, played);
}

} // namespace sfve::mod::input
