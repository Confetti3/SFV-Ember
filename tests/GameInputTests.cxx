// Scripted test input (mod/GameInput.hxx): who plays a frame, and that the
// script is the same for the same seed, player and frame on any copy.

#include <doctest/doctest.h>

#include "mod/GameInput.hxx"

#include <optional>
#include <string>
#include <vector>

using sfve::PackedInput;
using namespace sfve::mod::input;

TEST_CASE("no seed: the pad plays every frame")
{
	ScriptedInput script({}, 0);
	CHECK_FALSE(script.At(0).has_value());
	CHECK_FALSE(script.At(5000).has_value());
}

TEST_CASE("before its first frame a script plays nothing, or the pad when asked")
{
	CHECK(ScriptedInput({7, 100}, 0).At(99) == PackedInput(0));
	CHECK_FALSE(ScriptedInput({7, 100, true}, 0).At(99).has_value());
	CHECK(ScriptedInput({7, 100, true}, 0).At(100).has_value());
}

TEST_CASE("a script is a function of seed, player and frame")
{
	for (const Script kind : {Script::Buttons, Script::Moves}) {
		const ScriptConfig config{31337, 60, false, kind};
		ScriptedInput a(config, 0), b(config, 0), other(config, 1);
		bool differs = false;
		for (int frame = 60; frame < 60 + 20 * kMoveBlockFrames; frame++) {
			const PackedInput mine = *a.At(frame);
			CHECK(mine == *b.At(frame));
			CHECK((mine & ~kInputBits) == 0);
			differs |= mine != *other.At(frame);
		}
		CHECK(differs); // P2 is not P1's copy
	}
}

TEST_CASE("a block of moves starts on the script's first frame")
{
	// The same script started later plays the same moves, shifted.
	ScriptedInput early({5, 0, false, Script::Moves}, 0), late({5, 37, false, Script::Moves}, 0);
	for (int frame = 0; frame < 3 * kMoveBlockFrames; frame++) CHECK(*early.At(frame) == *late.At(frame + 37));
}

namespace {

constexpr MoveSet kSets[] = {MoveSet::System, MoveSet::Supers, MoveSet::Cody, MoveSet::Projectiles, MoveSet::Luke,
	MoveSet::Akira, MoveSet::Targets};

// A script's inputs for `frames` frames from `from`, played in order.
std::vector<PackedInput> Play(ScriptedInput& script, int from, int frames, std::optional<bool> facing = std::nullopt)
{
	std::vector<PackedInput> out;
	for (int frame = from; frame < from + frames; frame++) out.push_back(*script.At(frame, facing));
	return out;
}

bool IsFullCircle(const char* move)
{
	return std::string(move).find("720") != std::string::npos;
}

} // namespace

TEST_CASE("every move set's blocks follow each other, and its fighters stay on the ground")
{
	for (const MoveSet set : kSets) {
		for (const uint32_t seed : {1u, 4242u, 31337u}) {
			ScriptedInput script({seed, 0, false, Script::Moves, set}, 0);
			int start = 0, end = 0, blocks = 0;
			for (int frame = 0; frame < 20000; frame++) {
				const PackedInput in = *script.At(frame);
				const Planned& block = script.Block();
				REQUIRE(block.move != nullptr);
				CHECK(block.frames > 0);
				CHECK(frame >= block.start);
				CHECK(frame < block.start + block.frames);
				if (block.start != start || blocks == 0) {
					CHECK(block.start == end); // no gap and no overlap
					start = block.start;
					end = block.start + block.frames;
					blocks++;
				}
				CHECK((in & ~kInputBits) == 0);
				// Up only in the two full circles, never a press of left and right at once.
				if (!IsFullCircle(block.move)) CHECK((in & kUp) == 0);
				CHECK((in & (kLeft | kRight)) != (kLeft | kRight));
			}
			CHECK(blocks > 5);
		}
	}
}

TEST_CASE("the system moves play what they played before move sets: 96-frame blocks, the block's coin")
{
	ScriptedInput script({31337, 0, false, Script::Moves}, 0);
	for (int frame = 0; frame < 12 * kMoveBlockFrames; frame++) {
		script.At(frame);
		CHECK(script.Block().start == frame / kMoveBlockFrames * kMoveBlockFrames);
		CHECK(script.Block().frames == kMoveBlockFrames);
		CHECK_FALSE(script.Block().fighterFacing);
	}
}

TEST_CASE("a block's forward is the fighter's facing at the block's first frame")
{
	for (const MoveSet set : kSets) {
		ScriptConfig config{77, 0, false, Script::Moves, set};
		ScriptedInput right(config, 0), left(config, 0), coin(config, 0);
		const std::vector<PackedInput> r = Play(right, 0, 3000, true);
		const std::vector<PackedInput> l = Play(left, 0, 3000, false);
		Play(coin, 0, 3000);
		CHECK(right.Block().fighterFacing);
		CHECK(right.Block().facingRight);
		CHECK_FALSE(left.Block().facingRight);
		CHECK_FALSE(coin.Block().fighterFacing);
		// Facing left is facing right in a mirror: a move's left and right
		// swap; the mash after it faces nothing and is the same.
		bool turned = false;
		for (size_t i = 0; i < r.size(); i++) {
			const PackedInput mirrored = (r[i] & ~(kLeft | kRight)) | (r[i] & kLeft ? kRight : 0) | (r[i] & kRight ? kLeft : 0);
			CHECK((l[i] == r[i] || l[i] == mirrored));
			turned |= l[i] == mirrored && r[i] != l[i];
		}
		// Akira's and the target chains hold no forward or back: nothing to turn.
		if (set != MoveSet::Akira && set != MoveSet::Targets) CHECK(turned);
	}
}

TEST_CASE("a block keeps the facing it started with")
{
	ScriptedInput script({9, 0, false, Script::Moves, MoveSet::Supers}, 0);
	script.At(0, true);
	const int start = script.Block().start;
	for (int frame = 1; frame < kSuperBlockFrames; frame++) {
		script.At(frame, false);
		REQUIRE(script.Block().start == start);
		CHECK(script.Block().facingRight);
	}
	script.At(kSuperBlockFrames, false); // the next block takes the facing it starts with
	CHECK_FALSE(script.Block().facingRight);
}

TEST_CASE("an idle player presses nothing, and the other walks forward first")
{
	const ScriptConfig config{7, 100, false, Script::Moves, MoveSet::Cody, 1};
	ScriptedInput idle(config, 1), mover(config, 0);
	CHECK(idle.At(99) == PackedInput(0));
	for (int frame = 100; frame < 5000; frame++) CHECK(idle.At(frame) == PackedInput(0));
	// With the pad before its first frame, the idle player's pad still plays then.
	CHECK_FALSE(ScriptedInput({7, 100, true, Script::Moves, MoveSet::Cody, 1}, 1).At(99).has_value());
	// Each of the mover's blocks opens with a walk forward toward the idle one.
	for (int frame = 100; frame < 100 + kApproachFrames; frame++) CHECK(mover.At(frame, true) == kRight);
	ScriptedInput facingLeft(config, 0);
	CHECK(facingLeft.At(100, false) == kLeft);
	const int next = 100 + mover.Block().frames;
	CHECK(mover.Block().frames > kApproachFrames);
	CHECK(mover.At(next, false) == kLeft);
	// Buttons scripts stand still the same way.
	ScriptedInput buttons({7, 0, false, Script::Buttons, MoveSet::System, 0}, 0);
	for (int frame = 0; frame < 600; frame++) CHECK(buttons.At(frame) == PackedInput(0));
}

TEST_CASE("a script read out of order plays the same frames as one read in order")
{
	const ScriptConfig config{123, 10, false, Script::Moves, MoveSet::Projectiles};
	ScriptedInput ordered(config, 1);
	const std::vector<PackedInput> frames = Play(ordered, 10, 4000);
	ScriptedInput jumping(config, 1);
	for (const int frame : {3000, 20, 2500, 10, 3999}) CHECK(*jumping.At(frame + 10) == frames[frame]);
}

namespace {

// The inputs of the first block of `set` named `name` (some seed in a few
// plays it), from the block's first frame, facing right.
std::vector<PackedInput> BlockOf(MoveSet set, const std::string& name)
{
	for (uint32_t seed = 1; seed <= 8; seed++) {
		ScriptedInput script({seed, 0, false, Script::Moves, set}, 0);
		std::vector<PackedInput> inputs;
		bool inside = false;
		for (int frame = 0; frame < 40000; frame++) {
			const PackedInput in = *script.At(frame, true);
			const Planned& block = script.Block();
			if (name == block.move && frame >= block.start) {
				inside = true;
				inputs.push_back(in);
			} else if (inside) {
				return inputs;
			}
		}
	}
	return {};
}

// The frames at which an attack bit starts (it was not down the frame before).
std::vector<std::pair<int, PackedInput>> Presses(const std::vector<PackedInput>& block, int upTo)
{
	std::vector<std::pair<int, PackedInput>> presses;
	for (int i = 0; i < upTo && i < (int)block.size(); i++) {
		const PackedInput now = block[i] & kInputBits & ~(kUp | kDown | kLeft | kRight);
		const PackedInput before = i ? block[i - 1] & kInputBits & ~(kUp | kDown | kLeft | kRight) : 0;
		if (now & ~before) presses.push_back({i, now & ~before});
	}
	return presses;
}

} // namespace

TEST_CASE("Luke's target combo comes with the field's gaps: LP, MP at +10, HP at +31, then QCB+LP")
{
	const std::vector<PackedInput> slow = BlockOf(MoveSet::Luke, "luke target lp mp hp (field gaps)");
	REQUIRE(slow.size() >= 40);
	const auto presses = Presses(slow, 40);
	REQUIRE(presses.size() == 3);
	CHECK(presses[0].second == kLP);
	CHECK(presses[1].second == kMP);
	CHECK(presses[2].second == kHP);
	CHECK(presses[1].first - presses[0].first == 10);
	CHECK(presses[2].first - presses[0].first == 31);
	// Each press is four frames, nothing else is down and nothing moves while the chain plays.
	for (const auto& [at, bit] : presses) {
		for (int i = 0; i < 4; i++) CHECK(slow[at + i] == bit);
	}
	CHECK(slow[0] == PackedInput(0));

	const std::vector<PackedInput> tight = BlockOf(MoveSet::Luke, "luke target lp mp hp (tight)");
	REQUIRE(tight.size() >= 40);
	const auto tightPresses = Presses(tight, 40);
	REQUIRE(tightPresses.size() == 3);
	CHECK(tightPresses[1].first - tightPresses[0].first == 8);
	CHECK(tightPresses[2].first - tightPresses[1].first == 10);

	// Then a quarter circle back facing right: down, down-left, left with the punch.
	const std::vector<PackedInput> qcb = BlockOf(MoveSet::Luke, "luke target lp mp hp (field gaps) then qcb lp");
	REQUIRE(qcb.size() >= 80);
	CHECK(qcb[presses[2].first + 4 + 30] == kDown);
	CHECK(qcb[presses[2].first + 4 + 32] == (kDown | kLeft));
	CHECK(qcb[presses[2].first + 4 + 34] == (kLeft | kLP));
}

TEST_CASE("Akira's V-Skill is MP+MK: taps at varying gaps and a 13-frame hold, loadout apart")
{
	const std::vector<PackedInput> hold = BlockOf(MoveSet::Akira, "akira v-skill hold 13");
	REQUIRE(hold.size() >= 20);
	int held = 0;
	for (const PackedInput in : hold) held += in == (kMP | kMK);
	CHECK(held >= 13);
	for (int i = 2; i < 15; i++) CHECK(hold[i] == (kMP | kMK));
	CHECK(hold[15] != (kMP | kMK)); // the mash after the steps is no hold
	const std::vector<PackedInput> taps = BlockOf(MoveSet::Akira, "akira v-skill taps (long gaps)");
	REQUIRE(taps.size() >= 80);
	const auto presses = Presses(taps, 73);
	REQUIRE(presses.size() == 3);
	CHECK(presses[1].first - presses[0].first == 25);
	CHECK(presses[2].first - presses[1].first == 42);
	// The follow-up after a hold is one attack, a fixed gap later.
	const std::vector<PackedInput> then = BlockOf(MoveSet::Akira, "akira v-skill hold 13 then hp");
	REQUIRE(then.size() >= 40);
	CHECK(then[2 + 13 + 8] == kHP);
}

TEST_CASE("the target chains press their buttons at 6-12 frame gaps, grounded, for any fighter")
{
	const std::vector<PackedInput> triple = BlockOf(MoveSet::Targets, "lp>mp>hp (gaps 6, 10)");
	REQUIRE(triple.size() >= 40);
	const auto presses = Presses(triple, 30);
	REQUIRE(presses.size() == 3);
	CHECK(presses[0].second == kLP);
	CHECK(presses[1].second == kMP);
	CHECK(presses[2].second == kHP);
	CHECK(presses[1].first - presses[0].first == 10); // four-frame press plus the gap of 6
	CHECK(presses[2].first - presses[1].first == 14);
	const std::vector<PackedInput> kicks = BlockOf(MoveSet::Targets, "mk>hk (gap 12)");
	const auto kickPresses = Presses(kicks, 22);
	REQUIRE(kickPresses.size() == 2);
	CHECK(kickPresses[0].second == kMK);
	CHECK(kickPresses[1].second == kHK);
	CHECK(kickPresses[1].first - kickPresses[0].first == 16);
	// Facing left only mirrors a direction; these chains hold none but down.
	const std::vector<PackedInput> crouch = BlockOf(MoveSet::Targets, "crouching lp>mp (gap 8)");
	REQUIRE(crouch.size() >= 30);
	CHECK(crouch[2] == (kDown | kLP));
}
