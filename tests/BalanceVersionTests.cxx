// A launch's balance versions (mod/BalanceVersionRules.hxx): the game's own
// resolve, mirrored, and the launch's rule that names an explicit version
// this copy has, or refuses.

#include <doctest/doctest.h>

#include "mod/BalanceVersionRules.hxx"

using namespace sfve::mod;

namespace {

// A list as an up-to-date install's, and one that ends a season early.
const std::vector<int32_t> kCurrent = {10, 21, 42, 43, 60, 61};
const std::vector<int32_t> kOlder = {10, 21, 42, 43};

} // namespace

TEST_CASE("the game keeps a version its list holds and makes anything else the list's last")
{
	CHECK(ResolveVersion(kCurrent, 43) == 43);
	CHECK(ResolveVersion(kCurrent, -1) == 61);
	CHECK(ResolveVersion(kCurrent, 59) == 61); // not in the list
	CHECK(ResolveVersion(kOlder, 61) == 43);   // the field's silent difference: the same request, another version
	CHECK(ResolveVersion(kOlder, -1) == 43);
	// The development range is kept whatever the list holds.
	CHECK(ResolveVersion(kOlder, 500) == 500);
	CHECK(ResolveVersion(kOlder, 599) == 599);
	CHECK(ResolveVersion(kOlder, 600) == 43);
	CHECK_FALSE(ResolveVersion({}, -1).has_value());
	CHECK_FALSE(ResolveVersion({10, -1}, -1).has_value());
}

TEST_CASE("a launch names this copy's newest, or exactly the version asked for")
{
	const VersionChoice newest = ChooseLaunchVersion("Z41", kCurrent, kNewestBalanceVersion);
	CHECK(newest.refusal.empty());
	CHECK(newest.version == 61);

	const VersionChoice recorded = ChooseLaunchVersion("Z41", kCurrent, 43);
	CHECK(recorded.refusal.empty());
	CHECK(recorded.version == 43);
}

TEST_CASE("a copy that lacks the version refuses the launch and names the fighter")
{
	const VersionChoice missing = ChooseLaunchVersion("GUL", kOlder, 60);
	CHECK(missing.refusal == "fighter GUL has no balance version 60 on this copy (it has 10, 21, 42, 43): verify the game's "
							 "files or remove mods");
	// Never the development range, which the game would accept without data.
	CHECK_FALSE(ChooseLaunchVersion("GUL", kOlder, 500).refusal.empty());
	CHECK(ChooseLaunchVersion("GUL", std::nullopt, kNewestBalanceVersion).refusal.find("cannot be read") != std::string::npos);
	CHECK_FALSE(ChooseLaunchVersion("GUL", std::vector<int32_t>{}, kNewestBalanceVersion).refusal.empty());
}

TEST_CASE("the list's text")
{
	CHECK(VersionListText(kOlder) == "10, 21, 42, 43");
	CHECK(VersionListText(std::nullopt) == "none");
}
