// Tiny fighters (EMBER_LIBRARY.md section 59), checked without the game: the
// pick's size through SFV's fitting, validity and launch; what Ready sends;
// the pick screen's Size row; the scale a tiny side's mesh is given and the
// whole-mesh transform's, on memory laid out as the game's; the game's own
// scale the fight's update sees; the socket read's site and its log's
// every-tick readers; and the state that says which sides are drawn small.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "core/ReplayFile.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelectionScreen.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SocketProbe.hxx"
#include "mod/TinyFighters.hxx"

#include "common/GameCatalog.hxx"
#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "ui/GameMenu.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;
namespace tiny = sfve::mod::tiny;
namespace visual = sfve::mod::sfv::visual;

namespace {

Pick WithSize(Pick pick, int size)
{
	pick.size = size;
	return pick;
}

// A fighter's visual part as the game lays it out, with the objects it
// points at: the owning fighter (its side at CharaUnit +0x758), the control
// block's use count (+8), the actor and the actor's mesh component.
struct FakePart {
	alignas(16) unsigned char part[0xB00] = {};
	alignas(16) unsigned char unit[0x800] = {};
	alignas(16) int control[4] = {0, 0, 1, 0};
	alignas(16) unsigned char actor[0x400] = {};
	alignas(16) unsigned char mesh[0x40] = {};

	FakePart(int side, float x = 1.0f, float y = 1.0f, float z = 1.0f)
	{
		Put(part, visual::kOwnerObject, static_cast<unsigned char*>(unit));
		Put(part, visual::kOwnerControl, reinterpret_cast<unsigned char*>(control));
		Put(part, visual::kActor, static_cast<unsigned char*>(actor));
		Put(actor, visual::kActorMesh, static_cast<unsigned char*>(mesh));
		Put(unit, mod::sfv::fighter::kSide, side);
		Put(part, visual::kWantedScale, x);
		Put(part, visual::kWantedScale + 4, y);
		Put(part, visual::kWantedScale + 8, z);
	}
	template <class T>
	static void Put(unsigned char* base, unsigned offset, T value)
	{
		std::memcpy(base + offset, &value, sizeof(T));
	}
};

struct Call {
	void* component = nullptr;
	float xyz[3] = {};
	int count = 0;
};
Call s_call;

void RecordScale(void* component, const float* xyz)
{
	s_call.component = component;
	std::memcpy(s_call.xyz, xyz, sizeof(s_call.xyz));
	s_call.count++;
}

bool Near(float a, float b)
{
	return std::fabs(a - b) < 1e-6f;
}

mod::TinySides Sides(bool p1, bool p2)
{
	mod::TinySides sides;
	sides.side[0] = p1;
	sides.side[1] = p2;
	return sides;
}

} // namespace

TEST_CASE("a pick's size is 0 or 1 on every copy: kept when it is one, else normal")
{
	FillTestCatalog();
	CHECK(picks::kSizeNormal == 0);
	CHECK(picks::kSizeTiny == 1);
	const ember::GameCatalog& catalog = ember::Catalog();
	for (const int size : {0, 1}) {
		Pick pick = WithSize(PickOf(1), size);
		CHECK(catalog.validPick(pick, false));
		CHECK_FALSE(catalog.normalizePick(pick, false, nullptr, nullptr));
		CHECK(pick.size == size);
	}
	// Any other number is no size: fitted to normal, and not valid as it came.
	for (const int size : {2, 7, 255, -1}) {
		Pick pick = WithSize(PickOf(1), size);
		CHECK_FALSE(catalog.validPick(pick, false));
		CHECK(catalog.normalizePick(pick, false, nullptr, nullptr));
		CHECK(pick.size == 0);
		CHECK(catalog.validPick(pick, false));
	}
	// A size is part of what fitting can change.
	CHECK_FALSE(picks::SameFitting(WithSize(PickOf(1), 1), PickOf(1)));
	CHECK(picks::SameFitting(WithSize(PickOf(1), 1), WithSize(PickOf(1), 1)));
	// The fitting of this copy's own player's pick keeps it too.
	Pick own = WithSize(PickOf(0), 1);
	const sf4e::selection::Availability availability;
	catalog.normalizePick(own, false, &availability, nullptr);
	CHECK(own.size == 1);
}

TEST_CASE("a size rides in the wire pick's spare byte and the stored pick keeps it")
{
	FillTestCatalog();
	PlayerPick stored;
	stored.pick = WithSize(PickOf(1), 1);
	const PlayerPick kept = SetPick(stored);
	CHECK(kept.pick.size == 1);
	CHECK(CurrentPick().pick.size == 1);
	CHECK(PickText(kept).find("size tiny") != std::string::npos);
	// Ready sends it only where the room allows tiny fighters: this test has
	// no room, so the wire's byte is 0 whatever is stored.
	sf4e::NetplayFacade::RuntimeCommand command;
	AddPickTo(command);
	CHECK(command.character._unused == 0);
	CHECK(SizeToSend(1, true) == 1);
	CHECK(SizeToSend(1, false) == 0);
	CHECK(SizeToSend(0, true) == 0);
	CHECK(SizeToSend(7, true) == 0);
	// Normal again.
	stored.pick.size = 0;
	CHECK(SetPick(stored).pick.size == 0);
	CHECK(PickText(CurrentPick()).find("size") == std::string::npos);
}

TEST_CASE("a room's match launches a tiny side drawn small, and no other")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(0, 1, 2);
	std::optional<mod::MatchPicks> launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK_FALSE(launched->players[0].tiny);
	CHECK_FALSE(launched->players[1].tiny);
	match.picks[1].size = 1;
	// A room that does not allow tiny fighters (a public room's, or a private
	// room without the rule) draws a Tiny pick normal size.
	launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK_FALSE(launched->players[1].tiny);
	match.tinyAllowed = true;
	launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK_FALSE(launched->players[0].tiny);
	CHECK(launched->players[1].tiny);
	// Nothing written to the game's launch depends on it: the same fighters,
	// looks and loadouts launch either way.
	match.picks[1].size = 0;
	const std::optional<mod::MatchPicks> normal = LaunchPicks(match).picks;
	REQUIRE(normal);
	CHECK(normal->players[1].fighter == launched->players[1].fighter);
	CHECK(normal->players[1].costume == launched->players[1].costume);
	CHECK(normal->players[1].colour == launched->players[1].colour);
	CHECK(normal->players[1].vSkill == launched->players[1].vSkill);
	CHECK(normal->players[1].vTrigger == launched->players[1].vTrigger);
}

TEST_CASE("a replay's header keeps a side's size, written only for Tiny")
{
	replay::Header header;
	header.players[1].size = 1;
	replay::Replay replay;
	replay.header = header;
	const std::vector<std::uint8_t> bytes = replay::Encode(replay);
	replay::Replay decoded;
	std::string why;
	REQUIRE(replay::Decode(bytes.data(), bytes.size(), &decoded, &why));
	CHECK(decoded.header.players[0].size == 0);
	CHECK(decoded.header.players[1].size == 1);
	// A header with no tiny side holds no size at all, so every other file is what it was.
	replay::Replay plain;
	const std::vector<std::uint8_t> plainBytes = replay::Encode(plain);
	CHECK(std::search(plainBytes.begin(), plainBytes.end(), "\"size\"", "\"size\"" + 6) == plainBytes.end());
}

TEST_CASE("the pick screen shows the Size row only in a room that allows tiny fighters")
{
	FillTestCatalog();
	PlayerPick pick;
	pick.pick = PickOf(0);
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	const auto has = [](const std::vector<sf4e::ui::MenuEntry>& rows) {
		return std::any_of(rows.begin(), rows.end(), [](const sf4e::ui::MenuEntry& row) { return row.id == "size"; });
	};
	CHECK_FALSE(has(SfvSelectionScreen::Rows(pick, true, true, true, stages)));
	CHECK_FALSE(has(SfvSelectionScreen::Rows(pick, true, true, true, stages, false)));
	const std::vector<sf4e::ui::MenuEntry> rows = SfvSelectionScreen::Rows(pick, true, true, true, stages, true);
	REQUIRE(has(rows));
	const auto row = std::find_if(rows.begin(), rows.end(), [](const sf4e::ui::MenuEntry& entry) { return entry.id == "size"; });
	CHECK(row->choices.size() == 2);
	CHECK(row->enabled);
	// Locked while the pick may not change.
	const std::vector<sf4e::ui::MenuEntry> locked = SfvSelectionScreen::Rows(pick, false, true, true, stages, true);
	CHECK_FALSE(std::find_if(locked.begin(), locked.end(), [](const sf4e::ui::MenuEntry& entry) { return entry.id == "size"; })->enabled);

	// Right makes it Tiny, Left normal again; a chosen option does the same; a number that is no size does nothing.
	sf4e::ui::MenuAction action;
	action.kind = sf4e::ui::MenuAction::Adjust;
	action.id = "size";
	action.delta = 1;
	CHECK(SfvSelectionScreen::Apply(action, pick, stages));
	CHECK(pick.pick.size == 1);
	action.delta = -1;
	CHECK(SfvSelectionScreen::Apply(action, pick, stages));
	CHECK(pick.pick.size == 0);
	action.kind = sf4e::ui::MenuAction::Chosen;
	action.text = "#1";
	CHECK(SfvSelectionScreen::Apply(action, pick, stages));
	CHECK(pick.pick.size == 1);
	action.text = "#5";
	CHECK_FALSE(SfvSelectionScreen::Apply(action, pick, stages));
	CHECK(pick.pick.size == 1);
	// The pane previews the focused option.
	const PlayerPick shown = SfvSelectionScreen::Previewed(pick, "size", "#0");
	CHECK(shown.pick.size == 0);
}

TEST_CASE("the visual part's sites are the ones the exe was read for")
{
	CHECK(mod::sfv::kCharaVisualUpdate.rva == 0x3A06C0);
	CHECK(mod::sfv::kSetWorldScale3D.rva == 0x1BE8B40);
	CHECK(mod::sfv::kActorSetMeshTransform.rva == 0xE5BD90);
	CHECK(mod::sfv::kCharaVisualUpdate.bytes[0] == 0x40);
	CHECK(mod::sfv::kSetWorldScale3D.bytes[15] == 0xDD);
	CHECK(mod::sfv::kActorSetMeshTransform.bytes[15] == 0x89);
	// Each is in the build table, so another build hooks nothing.
	const mod::GameBuild* build = mod::sfv::VerifiedBuild();
	for (const mod::CodeSite* site : {&mod::sfv::kCharaVisualUpdate, &mod::sfv::kSetWorldScale3D, &mod::sfv::kActorSetMeshTransform}) {
		CHECK(std::any_of(build->sites, build->sites + build->siteCount, [site](const mod::CodeSite& listed) { return listed.rva == site->rva; }));
	}
}

TEST_CASE("a tiny mesh's scale is the game's wanted scale times the tiny scale, with 1 for a scale that is no scale")
{
	CHECK(mod::kTinyScale == doctest::Approx(0.4f));
	float out[3];
	const float normal[3] = {1.0f, 1.0f, 1.0f};
	tiny::ScaledScale(normal, out);
	CHECK(Near(out[0], 0.4f));
	CHECK(Near(out[1], 0.4f));
	CHECK(Near(out[2], 0.4f));
	const float big[3] = {2.0f, 1.0f, 0.5f};
	tiny::ScaledScale(big, out);
	CHECK(Near(out[0], 0.8f));
	CHECK(Near(out[1], 0.4f));
	CHECK(Near(out[2], 0.2f));
	const float inf = std::numeric_limits<float>::infinity();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float bad[3] = {0.0f, -1.0f, nan};
	tiny::ScaledScale(bad, out);
	for (const float value : out) CHECK(Near(value, 0.4f));
	const float infinite[3] = {inf, 1.0f, -inf};
	tiny::ScaledScale(infinite, out);
	CHECK(Near(out[0], 0.4f));
	CHECK(Near(out[2], 0.4f));
}

TEST_CASE("a tiny side's mesh is scaled after the game's update, and a normal side's is left alone")
{
	// Side 1 tiny: its part's mesh is given the game's scale times 0.4.
	FakePart p2(1, 1.0f, 1.0f, 1.0f);
	s_call = {};
	tiny::PartResult result = tiny::ApplyToPart(p2.part, Sides(false, true), &RecordScale);
	CHECK(result.side == 1);
	CHECK(result.scaled);
	CHECK(result.actor == p2.actor);
	CHECK(s_call.count == 1);
	CHECK(s_call.component == p2.mesh);
	CHECK(Near(s_call.xyz[0], 0.4f));
	CHECK(Near(s_call.xyz[1], 0.4f));
	CHECK(Near(s_call.xyz[2], 0.4f));
	// Where the game wants another scale, the tiny one follows it.
	FakePart wide(1, 1.5f, 1.0f, 1.0f);
	s_call = {};
	CHECK(tiny::ApplyToPart(wide.part, Sides(false, true), &RecordScale).scaled);
	CHECK(Near(s_call.xyz[0], 0.6f));
	// Side 0 is normal when only side 1 is tiny: nothing is called.
	FakePart p1(0);
	s_call = {};
	result = tiny::ApplyToPart(p1.part, Sides(false, true), &RecordScale);
	CHECK(result.side == 0);
	CHECK_FALSE(result.scaled);
	CHECK(result.actor == nullptr);
	CHECK(s_call.count == 0);
	// No tiny side at all: nothing is called for either.
	s_call = {};
	CHECK_FALSE(tiny::ApplyToPart(p2.part, Sides(false, false), &RecordScale).scaled);
	CHECK(s_call.count == 0);
	// Both tiny.
	s_call = {};
	CHECK(tiny::ApplyToPart(p1.part, Sides(true, true), &RecordScale).scaled);
	CHECK(s_call.count == 1);
}

TEST_CASE("the fight sees a tiny side's mesh at the game's own scale")
{
	// What a battle update starts with (BeginFightUpdate): the wanted scale,
	// not times 0.4.
	FakePart p2(1, 1.5f, 1.0f, 1.0f);
	s_call = {};
	tiny::PartResult result = tiny::RestoreToPart(p2.part, Sides(false, true), &RecordScale);
	CHECK(result.side == 1);
	CHECK(result.scaled);
	CHECK(s_call.count == 1);
	CHECK(s_call.component == p2.mesh);
	CHECK(Near(s_call.xyz[0], 1.5f));
	CHECK(Near(s_call.xyz[1], 1.0f));
	CHECK(Near(s_call.xyz[2], 1.0f));
	// A normal side is never touched, as ApplyToPart never touches it.
	FakePart p1(0);
	s_call = {};
	CHECK_FALSE(tiny::RestoreToPart(p1.part, Sides(false, true), &RecordScale).scaled);
	CHECK(s_call.count == 0);
	// A scale that is no scale counts as 1, as for the tiny scale.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float bad[3] = {0.0f, -2.0f, nan};
	float out[3];
	tiny::NormalScale(bad, out);
	for (const float value : out) CHECK(Near(value, 1.0f));
	// Restored and then scaled again, the mesh ends as the per-frame hook leaves it.
	s_call = {};
	tiny::RestoreToPart(p2.part, Sides(false, true), &RecordScale);
	tiny::ApplyToPart(p2.part, Sides(false, true), &RecordScale);
	CHECK(s_call.count == 2);
	CHECK(Near(s_call.xyz[0], 0.6f));
}

TEST_CASE("a battle update without the picture's hooks touches nothing")
{
	// The hooks are not installed in the tests, as in a copy that could not
	// hook them: nothing is drawn small, so nothing is restored or scaled,
	// whatever the sides say.
	mod::SetTinyFighters(true, true, false);
	FakePart p1(0), p2(1);
	uint8_t* parts[2] = {p1.part, p2.part};
	mod::BeginFightUpdate(parts);
	mod::EndFightUpdate(parts);
	uint8_t* none[2] = {nullptr, nullptr};
	mod::BeginFightUpdate(none);
	mod::EndFightUpdate(none);
	mod::ClearTinyFighters();
	CHECK_FALSE(mod::TinyFightersNow().Any());
}

TEST_CASE("the socket read's site, and the readers left out of its log")
{
	CHECK(mod::sfv::kSocketWorldPosition.rva == 0x3919D0);
	CHECK(mod::sfv::kSocketWorldPosition.bytes[0] == 0x48);
	CHECK(mod::sfv::kSocketWorldPosition.bytes[15] == 0x63);
	const mod::GameBuild* build = mod::sfv::VerifiedBuild();
	CHECK(std::any_of(build->sites, build->sites + build->siteCount,
		[](const mod::CodeSite& listed) { return listed.rva == mod::sfv::kSocketWorldPosition.rva; }));
	// Every tick: bone 28 in Chara PreInput, the camera's handlers, CharaManager's.
	for (const uint32_t every : {0x37D48Eu, 0x31B10Eu, 0x31B1DEu, 0x32FA5Fu, 0x33C41Fu, 0x3E3EF0u, 0x3E3F57u}) {
		CHECK(mod::socket_probe::EveryTick(every));
	}
	// Spawns at a bone: Z33's part as its V-Trigger ends, and any script's
	// spawn with a bone (through rva 0x32BAA0's jump, so from rva 0x4CFB70).
	for (const uint32_t spawn : {0x3BDB02u, 0x4CFC99u, 0x3BC294u, 0x3BDF3Bu}) CHECK_FALSE(mod::socket_probe::EveryTick(spawn));
}

TEST_CASE("a part that is no live fighter's, or has no actor or mesh, is left as the game made it")
{
	const mod::TinySides both = Sides(true, true);
	{
		FakePart part(1);
		part.control[2] = 0; // no uses left: the fighter is gone
		s_call = {};
		const tiny::PartResult result = tiny::ApplyToPart(part.part, both, &RecordScale);
		CHECK(result.side == -1);
		CHECK_FALSE(result.scaled);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(1);
		FakePart::Put(part.part, visual::kOwnerObject, static_cast<unsigned char*>(nullptr));
		s_call = {};
		CHECK(tiny::ApplyToPart(part.part, both, &RecordScale).side == -1);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(1);
		FakePart::Put(part.part, visual::kOwnerControl, static_cast<unsigned char*>(nullptr));
		s_call = {};
		CHECK(tiny::ApplyToPart(part.part, both, &RecordScale).side == -1);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(2); // a side that is neither
		s_call = {};
		CHECK(tiny::ApplyToPart(part.part, both, &RecordScale).side == -1);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(1);
		FakePart::Put(part.part, visual::kActor, static_cast<unsigned char*>(nullptr));
		s_call = {};
		const tiny::PartResult result = tiny::ApplyToPart(part.part, both, &RecordScale);
		CHECK(result.side == 1);
		CHECK_FALSE(result.scaled);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(1);
		FakePart::Put(part.actor, visual::kActorMesh, static_cast<unsigned char*>(nullptr));
		s_call = {};
		CHECK_FALSE(tiny::ApplyToPart(part.part, both, &RecordScale).scaled);
		CHECK(s_call.count == 0);
	}
	{
		FakePart part(1);
		s_call = {};
		CHECK_FALSE(tiny::ApplyToPart(nullptr, both, &RecordScale).scaled);
		CHECK_FALSE(tiny::ApplyToPart(part.part, both, nullptr).scaled);
		CHECK(s_call.count == 0);
	}
}

TEST_CASE("a whole-mesh transform keeps its rotation and translation and scales its scale")
{
	alignas(16) float in[12] = {0.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f, 30.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.0f};
	alignas(16) __m128 source[3];
	for (int i = 0; i < 3; i++) source[i] = _mm_load_ps(in + 4 * i);
	alignas(16) __m128 scaled[3];
	tiny::ScaledTransform(source, scaled);
	alignas(16) float out[12];
	for (int i = 0; i < 3; i++) _mm_store_ps(out + 4 * i, scaled[i]);
	for (int i = 0; i < 8; i++) CHECK(out[i] == in[i]);
	CHECK(Near(out[8], 0.4f));
	CHECK(Near(out[9], 0.4f));
	CHECK(Near(out[10], 0.8f));
	CHECK(out[11] == in[11]);
	// The source is not written.
	alignas(16) float after[4];
	_mm_store_ps(after, source[2]);
	CHECK(after[0] == 1.0f);
	CHECK(after[2] == 2.0f);
}

TEST_CASE("the sides drawn small are set by a launch and cleared by the end of the battle")
{
	mod::ClearTinyFighters();
	CHECK_FALSE(mod::TinyFightersNow().Any());
	mod::SetTinyFighters(false, true);
	CHECK_FALSE(mod::TinyFightersNow().side[0]);
	CHECK(mod::TinyFightersNow().side[1]);
	mod::SetTinyFighters(true, true);
	CHECK(mod::TinyFightersNow().side[0]);
	CHECK(mod::TinyFightersNow().side[1]);
	mod::SetTinyFighters(false, false);
	CHECK_FALSE(mod::TinyFightersNow().Any());
	mod::SetTinyFighters(true, false);
	mod::ClearTinyFighters();
	CHECK_FALSE(mod::TinyFightersNow().Any());
}

TEST_CASE("the booth's factor scales a mesh as Tiny's does, by its own size")
{
	// The default stays the Tiny size.
	const float normal[3] = {1.0f, 2.0f, 1.0f};
	float out[3];
	tiny::ScaledScale(normal, out);
	CHECK(Near(out[1], 0.8f));
	tiny::ScaledScale(normal, out, 0.001f);
	CHECK(Near(out[0], 0.001f));
	CHECK(Near(out[1], 0.002f));
	tiny::ScaledScale(normal, out, 1.0f);
	CHECK(Near(out[1], 2.0f));
	CHECK(tiny::UsableFactor(0.001f));
	CHECK(tiny::UsableFactor(1.0f));
	for (const float bad : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		CHECK_FALSE(tiny::UsableFactor(bad));
	}
	// A side's factor in the sides: each side scaled by its own.
	mod::TinySides sides = Sides(true, true);
	CHECK(sides.factor[0] == mod::kTinyScale);
	sides.factor[1] = 0.001f;
	FakePart p1(0), p2(1, 1.5f, 1.0f, 1.0f);
	s_call = {};
	REQUIRE(tiny::ApplyToPart(p2.part, sides, &RecordScale).scaled);
	CHECK(Near(s_call.xyz[0], 0.0015f));
	REQUIRE(tiny::ApplyToPart(p1.part, sides, &RecordScale).scaled);
	CHECK(Near(s_call.xyz[0], 0.4f));
	// The fight still sees the game's own scale (BeginFightUpdate).
	REQUIRE(tiny::RestoreToPart(p2.part, sides, &RecordScale).scaled);
	CHECK(Near(s_call.xyz[0], 1.5f));
	// A whole-mesh transform by the side's factor.
	alignas(16) float in[12] = {0.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f, 30.0f, 0.0f, 1.0f, 1.0f, 2.0f, 0.0f};
	alignas(16) __m128 source[3];
	for (int i = 0; i < 3; i++) source[i] = _mm_load_ps(in + 4 * i);
	alignas(16) __m128 scaled[3];
	tiny::ScaledTransform(source, scaled, 0.5f);
	alignas(16) float scale[4];
	_mm_store_ps(scale, scaled[2]);
	CHECK(Near(scale[0], 0.5f));
	CHECK(Near(scale[2], 1.0f));
}

TEST_CASE("the booth scales both sides' parts at once, each on its own side")
{
	FakePart p1(0), p2(1, 1.0f, 2.0f, 1.0f);
	const uint8_t* parts[2] = {p1.part, p2.part};
	const float factors[2] = {0.001f, 1.0f};
	tiny::PartResult results[2];
	s_call = {};
	CHECK(tiny::ScaleBothSides(parts, factors, &RecordScale, results));
	CHECK(s_call.count == 2);
	CHECK(results[0].side == 0);
	CHECK(results[0].actor == p1.actor);
	CHECK(results[1].side == 1);
	CHECK(s_call.component == p2.mesh);
	CHECK(Near(s_call.xyz[1], 2.0f)); // factor 1: the game's own size
	// A part remembered for a side that reads as the other's, or none: not both.
	const uint8_t* swapped[2] = {p2.part, p1.part};
	CHECK_FALSE(tiny::ScaleBothSides(swapped, factors, &RecordScale, results));
	const uint8_t* missing[2] = {p1.part, nullptr};
	CHECK_FALSE(tiny::ScaleBothSides(missing, factors, &RecordScale, results));
	CHECK(results[0].scaled);
	CHECK(results[1].side == -1);
	// A factor that is no size: nothing is called.
	const float bad[2] = {0.0f, 1.0f};
	s_call = {};
	CHECK_FALSE(tiny::ScaleBothSides(parts, bad, &RecordScale, results));
	CHECK(s_call.count == 0);
	CHECK_FALSE(results[0].scaled);
}

TEST_CASE("the booth's scale now is refused outside the booth, unbound, or off the game thread")
{
	// Not the booth (no BoothMode installed it): refused, and no side scaled.
	CHECK_FALSE(mod::ApplyFighterScaleNow(0.001f, 0.001f));
	CHECK_FALSE(mod::TinyFightersNow().Any());
	// The booth, but the picture's hooks are not in (the tests install none)
	// and no part has been seen: still refused, and nothing changes.
	mod::SetFighterScaleAllowed(true);
	CHECK_FALSE(mod::ApplyFighterScaleNow(0.001f, 0.001f));
	CHECK_FALSE(mod::ApplyFighterScaleNow(0.0f, 1.0f));
	CHECK_FALSE(mod::TinyFightersNow().Any());
	mod::ForgetFighterScale();
	mod::SetFighterScaleAllowed(false);
	CHECK_FALSE(mod::TinyFightersNow().Any());
	// A Tiny pick's sides still read as before.
	mod::SetTinyFighters(false, true, false);
	const mod::TinySides sides = mod::TinyFightersNow();
	CHECK_FALSE(sides.side[0]);
	CHECK(sides.side[1]);
	CHECK(sides.factor[1] == mod::kTinyScale);
	mod::ClearTinyFighters();
	CHECK_FALSE(mod::TinyFightersNow().Any());
}
