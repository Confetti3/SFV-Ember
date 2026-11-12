// The photo booth's pure parts (mod/BoothMode, overlay/FrameGrab), checked
// without the game. Step 0 holds the frozen types; stream A-mod adds the
// step and command parsing, the BMP encoding and the depth choice.

#include <doctest/doctest.h>

#include "mod/BoothMode.hxx"
#include "overlay/FrameGrab.hxx"

TEST_CASE("a booth command starts with nothing to do")
{
	const sfve::mod::booth::Command command;
	CHECK(command.verb == sfve::mod::booth::Verb::Hold);
	CHECK(command.steps.empty());
	CHECK(command.scale[0] == 1.f);
	CHECK(command.scale[1] == 1.f);
	CHECK(command.grab.depth.mode == sfve::overlay::grab::DepthMode::Auto);
}
