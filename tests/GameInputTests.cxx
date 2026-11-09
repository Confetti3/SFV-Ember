// Scripted test input (mod/GameInput.hxx): who plays a frame, and that the
// script is the same for the same seed, player and frame on any copy.

#include <doctest/doctest.h>

#include "mod/GameInput.hxx"

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
