// Which build's replays the list keeps and Watch now plays (SfvReplayBuild.hxx):
// the pure rule, for a replay of this build, of another build, of an
// unreadable header, and with the development option that plays any build.

#include <doctest/doctest.h>

#include "ember/SfvReplayBuild.hxx"

#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sfve::adapter;

const std::string kCurrent = "sfv-ember/13173604/0.7/aaaa/layout-1";
const std::string kOlder = "sfv-ember/13173604/0.6/bbbb/layout-1";

}

TEST_CASE("a replay plays on the build that recorded it and no other")
{
	CHECK(PlaysOnBuild(kCurrent, kCurrent, false));
	CHECK_FALSE(PlaysOnBuild(kOlder, kCurrent, false));
	CHECK_FALSE(PlaysOnBuild("", kCurrent, false));
	// The development option plays any build.
	CHECK(PlaysOnBuild(kOlder, kCurrent, true));
}

TEST_CASE("the list leaves out the replays of another build and keeps the rest")
{
	CHECK_FALSE(LeftOutForBuild(kCurrent, kCurrent, false));
	CHECK(LeftOutForBuild(kOlder, kCurrent, false));
	CHECK_FALSE(LeftOutForBuild(kOlder, kCurrent, true));
	// A header that could not be read has no build: not left out for it.
	CHECK_FALSE(LeftOutForBuild(std::nullopt, kCurrent, false));
	CHECK_FALSE(LeftOutForBuild(std::nullopt, kCurrent, true));

	const std::vector<std::optional<std::string>> files = {kCurrent, kOlder, std::nullopt, kOlder, kCurrent};
	int left = 0;
	for (const auto& build : files) left += LeftOutForBuild(build, kCurrent, false) ? 1 : 0;
	CHECK(left == 2);
}

TEST_CASE("Watch now and the list agree on every pair of builds")
{
	for (const std::string& recorded : {kCurrent, kOlder, std::string()}) {
		for (const bool any : {false, true}) {
			CHECK(LeftOutForBuild(recorded, kCurrent, any) == !PlaysOnBuild(recorded, kCurrent, any));
		}
	}
}
