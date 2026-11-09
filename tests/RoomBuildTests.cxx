// A room's build carries the rollback's layout id as its own part
// (ember/SfvRoomBuild, mod/SfvLayoutRules; EMBER_LIBRARY.md section 41).

#include <doctest/doctest.h>

#include "core/StateRegions.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRoomBuild.hxx"
#include "mod/SfvLayoutRules.hxx"

#include <fstream>
#include <iterator>
#include <string>

using sfve::room::LayoutIdOf;
using sfve::room::RoomBuildId;

TEST_CASE("the layout id is one build's own: 16 hex digits, the same wherever the state lies")
{
	const std::string& id = sfve::mod::LayoutRulesId();
	CHECK(id.size() == 16);
	CHECK(id.find_first_not_of("0123456789abcdef") == std::string::npos);
	CHECK(&sfve::mod::LayoutRulesId() == &id);
	// Where a battle keeps its state never enters the id: two battles at
	// different addresses lay out the same regions and masks.
	sfve::StateLayout one, other;
	sfve::mod::AddStateOutsideTheGameSnapshot(one, {0x1000, 0x2000, 0x3000, 0x4000, 0x5000});
	sfve::mod::AddStateOutsideTheGameSnapshot(other, {0x9000, 0x8000, 0x7000, 0x6000, 0x5000});
	CHECK(one.LayoutId() == other.LayoutId());
	CHECK(one.Regions().size() == other.Regions().size());
}

TEST_CASE("a room build is the pick build and the layout id, and gives the layout id back")
{
	const std::string pick = std::string(sfve::picks::kMatchBuild) + "/8dafe602f7615d90";
	const std::string build = RoomBuildId(pick, "0123456789abcdef");
	CHECK(build == pick + "/layout-0123456789abcdef");
	CHECK(LayoutIdOf(build) == "0123456789abcdef");
	// A build from before the layout id, whose fingerprint is 16 hex digits
	// too, or a layout part that is not one.
	CHECK(LayoutIdOf(pick).empty());
	CHECK(LayoutIdOf(pick + "/layout-").empty());
	CHECK(LayoutIdOf(std::string(sfve::picks::kMatchBuild)).empty());
	CHECK(LayoutIdOf(pick + "/layout-0123456789ABCDEF").empty());
	CHECK(LayoutIdOf(pick + "/layout-0123456789abcde").empty());
	CHECK(LayoutIdOf(build + "0").empty());
	CHECK(build.size() <= 128); // the room's limit for a build
}

TEST_CASE("the room host's pick space was exported by a build with these rollback rules")
{
	// data/pick-space.json is what SFV's room host hosts; exported by a copy
	// whose rules differ, the host would refuse every room of this build.
	std::ifstream file(SFVE_PICK_SPACE_PATH, std::ios::binary);
	REQUIRE(file);
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	sfve::picks::PickSpace space;
	std::string exported, error;
	REQUIRE(sfve::picks::FromJson(text, space, exported, error));
	CHECK(LayoutIdOf(exported) == sfve::mod::LayoutRulesId());
	CHECK(exported == RoomBuildId(sfve::picks::BuildId(space), sfve::mod::LayoutRulesId()));
}
