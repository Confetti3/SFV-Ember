// A failed launch's way back to the main menu (mod/MenuReturn.hxx).

#include <doctest/doctest.h>

#include "mod/MenuReturn.hxx"

using sfve::mod::MenuReturn;
using Step = MenuReturn::Step;

TEST_CASE("a game stuck away from the main menu is asked for it, and is back the frame the menu comes")
{
	MenuReturn back;
	CHECK(back.Observe(false, false) == Step::Ask);
	CHECK(back.Observe(true, false) == Step::Back);
	CHECK(back.Frames() == 1);
}

TEST_CASE("a pending change is waited for, not asked over")
{
	MenuReturn back;
	CHECK(back.Observe(false, true) == Step::Wait);
	CHECK(back.Observe(false, true) == Step::Wait);
	CHECK(back.Observe(true, false) == Step::Back);
}

TEST_CASE("a battle that came instead of the main menu is asked to leave too")
{
	MenuReturn back;
	CHECK(back.Observe(false, false) == Step::Ask);
	// The jump's own change to its battle was made instead of the main menu.
	CHECK(back.Observe(false, false) == Step::Ask);
	CHECK(back.Observe(false, true) == Step::Wait);
	CHECK(back.Observe(true, false) == Step::Back);
}

TEST_CASE("the main menu with another change pending is back: the next change is not the way back's")
{
	MenuReturn back;
	CHECK(back.Observe(true, true) == Step::Back);
	CHECK(back.Frames() == 0);
}

TEST_CASE("a game that never changes scene is given up after the limit, once asked each frame")
{
	MenuReturn back;
	for (int frame = 1; frame <= MenuReturn::kGiveUpFrames; frame++) {
		REQUIRE(back.Observe(false, false) == Step::Ask);
	}
	CHECK(back.Observe(false, false) == Step::GaveUp);
	CHECK(back.Frames() == MenuReturn::kGiveUpFrames + 1);
}

TEST_CASE("a change that stays pending is given up after the limit as well")
{
	MenuReturn back;
	for (int frame = 1; frame <= MenuReturn::kGiveUpFrames; frame++) REQUIRE(back.Observe(false, true) == Step::Wait);
	CHECK(back.Observe(false, true) == Step::GaveUp);
}
