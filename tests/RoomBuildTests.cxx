// A room's build carries the rollback's layout id as its own part
// (ember/SfvRoomBuild, mod/SfvLayoutRules; EMBER_LIBRARY.md section 42).

#include <doctest/doctest.h>

#include "core/StateRegions.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRoomBuild.hxx"
#include "mod/ModConfig.hxx"
#include "mod/SfvLayoutRules.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SfvRegionNames.hxx"
#include "mod/SocketReads.hxx"

#include <xxhash.h>

#include <stdint.h>
#include <stdio.h>

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

TEST_CASE("the layout id hashes the rules' text, which carries each owning module's tables whole")
{
	const std::string& text = sfve::mod::LayoutRulesText();
	char hex[17];
	snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)XXH3_64bits(text.data(), text.size()));
	CHECK(sfve::mod::LayoutRulesId() == hex);
	// A row changed in RegionNames' tables or in the socket reads' sites or
	// offsets changes its module's text, so the text the id hashes.
	CHECK(text.find("; region owners " + sfve::mod::RegionOwnersText() + "; ") != std::string::npos);
	const std::string anchors =
		sfve::mod::ChooseSocketAnchor(sfve::mod::Config()) ? sfve::mod::socket_reads::AnchorRulesText() : std::string("off");
	CHECK(text.find("; socket anchors " + anchors) != std::string::npos);
}

TEST_CASE("the region owners' text names every owner's type, runs and sub-objects in the tables' order, and the lookups' bounds")
{
	const std::string owners = sfve::mod::RegionOwnersText();
	CHECK(owners.rfind("units, CharaUnit 0x3A647C0 +248/40 +376/68 +1892/300 +2196/208 ", 0) == 0);
	CHECK(owners.find(" +3604/16 ->3160+16/32, ProjectileUnit 0x3A64C20 +248/40 +376/68 +464/208 +688/104 ->672+16/20, ") !=
		std::string::npos);
	CHECK(owners.find("; parts, CharaActionPart 0x3A64A98 +216/15808 +16048/4 +16104/4 +16112/8 ->16072+48/224 ->16088+48/224, ") !=
		std::string::npos);
	CHECK(owners.find(", ProjectileVisualPart 0x3A66D00 +192/44 +240/136; root ModeUnit 0x0 +6448/228, the manager at +566272; ") !=
		std::string::npos);
	CHECK(owners.find("; unique parts derive from 0x3A64B40; a unit's parts at +192..+200, at most 256; at most 256 units of a type") !=
		std::string::npos);
	// Every owner kRunRules names is one of the tables' (a rule for an owner
	// they lost would apply nowhere).
	for (const sfve::mod::sfv::manager::RunRule& rule : sfve::mod::sfv::manager::kRunRules) {
		CAPTURE(rule.owner);
		CHECK(owners.find(", " + std::string(rule.owner) + " 0x") != std::string::npos);
	}
}

TEST_CASE("every fighter unit's parts keep their state outside the snapshot, a partner's too")
{
	// Akira's partners are CharaUnit[2] and [3] (sfv::unit::kMostCharaUnits);
	// a unit without the part (0) gets no run.
	sfve::mod::OutsideState where{0x1000, 0x2000, 0x3000, 0x4000, 0x5000};
	where.fighterParts[0][0] = 0x10000;
	where.fighterParts[2][0] = 0x20000;
	where.fighterParts[sfve::mod::kMaxFighterUnits - 1][1] = 0x30000;
	sfve::StateLayout layout;
	sfve::mod::AddStateOutsideTheGameSnapshot(layout, where);
	const auto at = [&](const std::string& name) -> uintptr_t {
		for (const sfve::StateRegion& r : layout.Regions()) {
			if (r.name == name) return reinterpret_cast<uintptr_t>(r.ptr);
		}
		return 0;
	};
	CHECK(at("CharaUnit[0].CharaStatusPart.holds") == 0x10000 + 0x338);
	CHECK(at("CharaUnit[2].CharaStatusPart.holds") == 0x20000 + 0x338);
	CHECK(at("CharaUnit[7].CharaUniquePart@0x3A682D0.delay") == 0x30000 + 0x61C);
	CHECK(at("CharaUnit[1].CharaStatusPart.holds") == 0);
	CHECK(at("CharaUnit[3].CharaStatusPart.holds") == 0);
}

TEST_CASE("the visual part's animation hand-off block is saved and restored for every fighter unit, and never hashed")
{
	// CharaVisualPart +268..+360 (sfv::kPartRunsOutsideSnapshot's third row), for each of the eight units, partners included.
	sfve::mod::OutsideState where{0x1000, 0x2000, 0x3000, 0x4000, 0x5000};
	for (size_t unit = 0; unit < sfve::mod::kMaxFighterUnits; unit++) where.fighterParts[unit][2] = 0x100000 + unit * 0x10000;
	sfve::StateLayout layout;
	sfve::mod::AddStateOutsideTheGameSnapshot(layout, where);
	for (size_t unit = 0; unit < sfve::mod::kMaxFighterUnits; unit++) {
		const sfve::StateRegion* r = layout.Find("CharaUnit[" + std::to_string(unit) + "].CharaVisualPart+0x10C.motionhandoff");
		REQUIRE(r != nullptr);
		CHECK(reinterpret_cast<uintptr_t>(r->ptr) == 0x100000 + unit * 0x10000 + 268);
		CHECK(r->size == 92);
		CHECK_FALSE(r->checked); // restored, not hashed and not compared with the peer
	}
	// A unit without the part gets none.
	where.fighterParts[1][2] = 0;
	sfve::StateLayout fewer;
	sfve::mod::AddStateOutsideTheGameSnapshot(fewer, where);
	CHECK(fewer.Find("CharaUnit[1].CharaVisualPart+0x10C.motionhandoff") == nullptr);
	CHECK(fewer.Regions().size() + 1 == layout.Regions().size());
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

TEST_CASE("the status part's four effect switches keep their kind, level, shown type and override, hashed")
{
	// sfv::kPartRunsOutsideSnapshot's rows 3 to 10: records of 0x48 bytes
	// from +0x340, +0x28 (4 bytes) and +0x34 (16 bytes) of each.
	sfve::mod::OutsideState where{0x1000, 0x2000, 0x3000, 0x4000, 0x5000};
	for (size_t row = 3; row < 11; row++) where.fighterParts[0][row] = 0x40000;
	sfve::StateLayout layout;
	sfve::mod::AddStateOutsideTheGameSnapshot(layout, where);
	for (uint32_t record = 0; record < 4; record++) {
		const std::string name = "CharaUnit[0].CharaStatusPart.effectswitch" + std::to_string(record);
		const sfve::StateRegion* kind = layout.Find(name + ".kind");
		const sfve::StateRegion* shown = layout.Find(name + ".shown");
		REQUIRE(kind != nullptr);
		REQUIRE(shown != nullptr);
		CHECK(reinterpret_cast<uintptr_t>(kind->ptr) == 0x40000 + 0x340 + record * 0x48 + 0x28);
		CHECK(kind->size == 4);
		CHECK(reinterpret_cast<uintptr_t>(shown->ptr) == 0x40000 + 0x340 + record * 0x48 + 0x34);
		CHECK(shown->size == 16);
		CHECK(kind->checked);
		CHECK(shown->checked);
	}
	// None of them reaches the game's own runs of a record (+0x2C..+0x33 and +0x44).
	for (const sfve::mod::sfv::PartRun& run : sfve::mod::sfv::kPartRunsOutsideSnapshot) {
		if (run.partType != sfve::mod::sfv::fighter::kStatusPartType || run.offset < 0x340) continue;
		const uint32_t inRecord = (run.offset - 0x340) % 0x48;
		CHECK((inRecord + run.size <= 0x2C || inRecord >= 0x34));
		CHECK(inRecord + run.size <= 0x44);
	}
}

TEST_CASE("the round's end wait unit keeps its state and the fighter's position, never its reference")
{
	// A made-up address clear of the other regions' (battledata's later rounds reach 0x70000).
	constexpr uintptr_t kUnit = 0x40000000;
	sfve::mod::OutsideState where{0x1000, 0x2000, 0x3000, 0x4000, 0x5000, kUnit};
	sfve::StateLayout layout;
	sfve::mod::AddStateOutsideTheGameSnapshot(layout, where);
	const sfve::StateRegion* state = layout.Find("kowait.state");
	const sfve::StateRegion* position = layout.Find("kowait.position");
	REQUIRE(state != nullptr);
	REQUIRE(position != nullptr);
	CHECK(reinterpret_cast<uintptr_t>(state->ptr) == kUnit + 0xF8);
	CHECK(state->size == 0x18); // +0xF8..+0x10F: the latch (+0x100) and the mode (+0x104) among them
	CHECK(reinterpret_cast<uintptr_t>(position->ptr) == kUnit + 0x120);
	CHECK(position->size == 0x10);
	CHECK(state->checked);
	// +0x110 and the counted reference at +0x118 are in no region.
	for (const sfve::StateRegion& r : layout.Regions()) {
		const uintptr_t from = reinterpret_cast<uintptr_t>(r.ptr), to = from + r.size;
		CAPTURE(r.name);
		CHECK_FALSE((from < kUnit + 0x120 && to > kUnit + 0x110));
	}
	// A battle without the unit adds neither.
	where.koWait = 0;
	sfve::StateLayout without;
	sfve::mod::AddStateOutsideTheGameSnapshot(without, where);
	CHECK(without.Find("kowait.state") == nullptr);
	CHECK(without.Regions().size() + 2 == layout.Regions().size());
}
