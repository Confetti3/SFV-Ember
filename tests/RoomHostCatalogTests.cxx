// SFV's room host catalog (src/roomhost/SfvHostCatalog.cxx) with the pick
// space this build embeds (data/pick-space.json): it takes only the room of
// the build that pick space makes, for the game "sfv", and then judges picks
// by SFV's rules, refusing what an SF4 client would send.

#include <doctest/doctest.h>

#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRoomBuild.hxx"

#include "RoomHost.hxx"
#include "common/GameCatalog.hxx"
#include "common/SelectionPick.hxx"
#include "common/StageCatalog.hxx"
#include "session/RoomTypes.hxx"

#include <string>
#include <vector>

namespace sfve::roomhost {
extern const char* const kPickSpaceJson;
}

namespace {

sfve::picks::PickSpace Embedded(std::string& build)
{
	sfve::picks::PickSpace space;
	std::string error;
	REQUIRE(sfve::picks::FromJson(sfve::roomhost::kPickSpaceJson, space, build, error));
	return space;
}

// The fighter's id in the pick space, by its code; -1 for none.
int FighterIdOf(const sfve::picks::PickSpace& space, const std::string& code)
{
	for (size_t id = 0; id < space.fighters.size(); id++) {
		if (space.fighters[id].code == code) return (int)id;
	}
	return -1;
}

sf4e::selection::Pick PickOf(int fighter, int costume, int colour)
{
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	pick.costume = costume;
	pick.color = colour;
	return pick;
}

sf4e::roomhost::Config ConfigFor(const std::string& game, const std::string& build)
{
	sf4e::roomhost::Config config;
	config.game = game;
	config.buildId = build;
	return config;
}

} // namespace

TEST_CASE("SFV's room host takes only the room of the build its pick space makes")
{
	std::string build;
	const sfve::picks::PickSpace space = Embedded(build);
	CHECK(sfve::picks::Problem(space).empty());
	// The pick build, then the layout id the export carries (SfvRoomBuild).
	CHECK(!sfve::room::LayoutIdOf(build).empty());
	CHECK(build == sfve::room::RoomBuildId(sfve::picks::BuildId(space), sfve::room::LayoutIdOf(build)));
	CHECK(build.rfind(std::string(sfve::picks::kMatchBuild) + "/", 0) == 0);

	// Refused before anything is filled.
	std::string reason;
	CHECK_FALSE(sf4e::roomhost::PrepareCatalog(ConfigFor("usf4", build), reason));
	CHECK(reason == "other_game");
	std::string other = build;
	other.back() = other.back() == '0' ? '1' : '0';
	CHECK_FALSE(sf4e::roomhost::PrepareCatalog(ConfigFor("sfv", other), reason));
	CHECK(reason == "other_build");
	// An SF4 build id: the Sidecar.dll hash.
	CHECK_FALSE(sf4e::roomhost::PrepareCatalog(ConfigFor("sfv", std::string(64, 'a')), reason));
	CHECK(reason == "other_build");
	CHECK(ember::Catalog().fighterCount == 0);

	REQUIRE(sf4e::roomhost::PrepareCatalog(ConfigFor("sfv", build), reason));
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(ember::Usable(catalog));
	CHECK(catalog.fighterCount == (int)space.fighters.size());
	CHECK(catalog.costumeLimit == sfve::picks::CostumeLimit(space));
	CHECK(catalog.colorLimit == sfve::picks::ColourLimit(space));
	CHECK(catalog.ultraChoices == sfve::picks::kLoadoutChoices); // V-Skill and V-Trigger (section 40)
	CHECK_FALSE(catalog.editions);
	CHECK(ember::PlayableRoundCount(3));
	CHECK_FALSE(ember::PlayableRoundCount(15));
	CHECK(ember::PlayableRoundTime(99));
	CHECK(ember::PlayableRoundTime(9999)); // the game's no time limit (EMBER_LIBRARY.md section 27)
	CHECK_FALSE(ember::PlayableRoundTime(546));

	// Every costume of every fighter in each of its colours is a pick, but
	// the one the game ships nothing for, which the host refuses and fits
	// to the fighter's first costume.
	int unshipped = 0;
	for (int fighter = 0; fighter < (int)space.fighters.size(); fighter++) {
		for (const sfve::picks::Costume& costume : space.fighters[(size_t)fighter].costumes) {
			for (int colour : costume.colours) {
				sf4e::selection::Pick pick;
				pick.fighter = fighter;
				pick.costume = costume.id;
				pick.color = colour;
				CHECK(catalog.validPick(pick, false) == !costume.unshipped);
				CHECK(catalog.availablePick(pick, false, {}, nullptr) == !costume.unshipped);
				if (!costume.unshipped) continue;
				unshipped++;
				CHECK(catalog.normalizePick(pick, false, nullptr, nullptr));
				CHECK(pick.costume == space.fighters[(size_t)fighter].costumes.front().id);
			}
		}
	}
	CHECK(unshipped > 0);
	// A loadout is a pick exactly where the fighter has both its parts, as
	// the exported counts say, and the host fits one the fighter lacks.
	for (int fighter = 0; fighter < (int)space.fighters.size(); fighter++) {
		const sfve::picks::Fighter& content = space.fighters[(size_t)fighter];
		for (int index = 0; index < sfve::picks::kLoadoutChoices; index++) {
			sf4e::selection::Pick pick;
			pick.fighter = fighter;
			pick.costume = content.costumes.front().id;
			pick.color = content.costumes.front().colours.front();
			sfve::picks::SetLoadout(pick, *sfve::picks::LoadoutAt(index));
			const bool has = sfve::picks::HasLoadout(content, *sfve::picks::LoadoutAt(index));
			CHECK(catalog.validPick(pick, false) == has);
			CHECK(catalog.normalizePick(pick, false, nullptr, nullptr) == !has);
			CHECK(catalog.validPick(pick, false));
		}
	}
	// What an SF4 client sends is not: an Ultra past the four loadouts, a
	// handicap, a fighter past the roster, a costume or colour the fighter's
	// spec does not have.
	sf4e::selection::Pick ultra;
	ultra.ultra = sfve::picks::kLoadoutChoices;
	CHECK_FALSE(catalog.validPick(ultra, true));
	ultra.ultra = -1;
	CHECK_FALSE(catalog.validPick(ultra, true));
	sf4e::selection::Pick handicap;
	handicap.handicap = 1;
	CHECK_FALSE(catalog.validPick(handicap, false));
	sf4e::selection::Pick past;
	past.fighter = catalog.fighterCount;
	CHECK_FALSE(catalog.validPick(past, false));
	sf4e::selection::Pick costume;
	costume.costume = catalog.costumeLimit;
	CHECK_FALSE(catalog.validPick(costume, false));
	sf4e::selection::Pick colour;
	colour.color = catalog.colorLimit;
	CHECK_FALSE(catalog.validPick(colour, false));
	// Every stage of the universe is valid, whichever a member's copy lacks
	// (the room carries that, not the host's catalog); Random never travels.
	for (std::int64_t stage = 0; stage < (std::int64_t)space.stages.size(); stage++) CHECK(catalog.validStage(stage));
	CHECK_FALSE(catalog.validStage((std::int64_t)space.stages.size()));
	CHECK(catalog.randomStage == sfve::picks::kRandomStage);
	CHECK(sfve::picks::kRandomStage == sf4e::selection::RandomStageId);
	CHECK_FALSE(catalog.validStage(sfve::picks::kRandomStage));
	// SFV's copies can lack stages, so a member's list of them is kept.
	CHECK(catalog.stagesCanBeMissing);
	// A host never resolves Random: it only ever sees stages P1 resolved.
	CHECK(catalog.resolveStage(1, 7, sf4e::selection::RandomPool(), nullptr, {}) == 1);
	// A member's missing stages keep only the universe's.
	CHECK(sf4e::room::MissingStagesFrom(std::vector<std::int64_t>{sfve::picks::kRandomStage, 1, 1, 0, -1}) ==
		std::vector<std::int64_t>{0, 1});

	// The listing's faces (EMBER_LIBRARY.md section 28): SFV Ember's players
	// have no main, so each member is listed by the pick they shared in the
	// room, else none, never by a profile's main (fighter 0, RYU, unless set).
	CHECK(sf4e::roomhost::CatalogFaces() == sf4e::roomhost::ListedFaces::Picks);
	sf4e::room::Snapshot snapshot;
	snapshot.host = 1;
	for (sf4e::room::MemberId id = 1; id <= 4; id++) {
		sf4e::room::Member member;
		member.id = id;
		member.name = "player" + std::to_string(id);
		member.joinOrder = id;
		member.mainFighter = 0;
		snapshot.members.push_back(member);
	}
	snapshot.members[0].fighter = 3;                           // shared
	snapshot.members[2].fighter = catalog.fighterCount - 1;    // the last fighter
	snapshot.members[3].fighter = catalog.fighterCount;        // past the roster
	const auto details = sf4e::roomhost::DetailsOf(snapshot, sf4e::roomhost::CatalogFaces());
	CHECK(details.fighters == std::vector<int>{3, sf4e::roomhost::NoFighter, catalog.fighterCount - 1, sf4e::roomhost::NoFighter});
	// SF4's listing of the same room, by mains: RYU for everyone.
	CHECK(sf4e::roomhost::DetailsOf(snapshot).fighters == std::vector<int>(4, 0));
}

// The exported pick space is SFV's whole universe (EMBER_LIBRARY.md section
// 41): every fighter, costume and colour the spec's rows hold, and every
// stage, whatever the exporting copy's account or install. Needs
// data/pick-space.json exported from a copy with the universe readers
// (catalog-export); the counts are the exe's own CharaSpec and StageSpec
// rows, computed apart from the game, so an export from an older reader
// fails here rather than in a room's build check.
TEST_CASE("SFV's room host data holds every costume, colour and stage SFV has")
{
	std::string build;
	const sfve::picks::PickSpace space = Embedded(build);
	size_t costumes = 0, looks = 0;
	for (const sfve::picks::Fighter& fighter : space.fighters) {
		costumes += fighter.costumes.size();
		for (const sfve::picks::Costume& costume : fighter.costumes) looks += costume.colours.size();
	}
	CHECK(space.fighters.size() == 45);
	CHECK(costumes == 875);
	// The game's own lookups give 6,426 pairs (catalog-export, 2026-11-09);
	// the plan's offline count of the exe's rows was 6,424.
	CHECK(looks == 6426);
	// The spec's 46 stage rows less RND (kUnplayableStages); an export from
	// the account's own stage list held 37 (section 41).
	CHECK(space.stages.size() == 45);
	const int ryu = FighterIdOf(space, "RYU"), cnl = FighterIdOf(space, "CNL"), ken = FighterIdOf(space, "KEN");
	REQUIRE(ryu >= 0);
	REQUIRE(cnl >= 0);
	REQUIRE(ken >= 0);
	// RYU's costumes 0 to 24 and CHUN-LI's 0 to 34, with no gaps.
	CHECK(sfve::picks::CostumeCount(space, ryu) == 25);
	CHECK(space.fighters[(size_t)ryu].costumes.size() == 25);
	CHECK(sfve::picks::CostumeCount(space, cnl) == 35);
	CHECK(space.fighters[(size_t)cnl].costumes.size() == 35);
	CHECK(sfve::picks::CostumeLimit(space) == 35);
	CHECK(sfve::picks::ColourLimit(space) == 113);
	// Costumes no account's menus offered before: RYU's folder 30 set, KEN's
	// folder 11, CHUN-LI's EX character skin (one colour) and RYU's EX02 of
	// DEFAULT.
	CHECK(sfve::picks::ValidPick(space, PickOf(ryu, 24, 1)));
	CHECK(sfve::picks::ValidPick(space, PickOf(cnl, 23, 0)));
	CHECK(sfve::picks::ValidPick(space, PickOf(ken, 11, 9)));
	CHECK(sfve::picks::ValidPick(space, PickOf(ryu, 0, 101)));
	// RYU's folder 0C set, which loads with no textures: unshipped.
	CHECK_FALSE(sfve::picks::ValidPick(space, PickOf(ryu, 15, 1)));
	CHECK_FALSE(sfve::picks::ValidPick(space, PickOf(ryu, 24, 2)));
	CHECK_FALSE(sfve::picks::ValidPick(space, PickOf(cnl, 23, 1)));
	CHECK_FALSE(sfve::picks::ValidPick(space, PickOf(ryu, 25, 0)));
}
