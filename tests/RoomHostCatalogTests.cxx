// SFV's room host catalog (src/roomhost/SfvHostCatalog.cxx) with the pick
// space this build embeds (data/pick-space.json): it takes only the room of
// the build that pick space makes, for the game "sfv", and then judges picks
// by SFV's rules, refusing what an SF4 client would send.

#include <doctest/doctest.h>

#include "ember/SfvPickSpace.hxx"

#include "RoomHost.hxx"
#include "common/GameCatalog.hxx"
#include "common/SelectionPick.hxx"

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
	CHECK(build == sfve::picks::BuildId(space));
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
	CHECK(catalog.ultraChoices == 1);
	CHECK_FALSE(catalog.editions);
	CHECK(ember::PlayableRoundCount(3));
	CHECK_FALSE(ember::PlayableRoundCount(15));
	CHECK(ember::PlayableRoundTime(99));
	CHECK(ember::PlayableRoundTime(9999)); // the game's no time limit (EMBER_LIBRARY.md section 27)
	CHECK_FALSE(ember::PlayableRoundTime(546));

	// Every costume of every fighter in each of its colours is a pick.
	for (int fighter = 0; fighter < (int)space.fighters.size(); fighter++) {
		for (const sfve::picks::Costume& costume : space.fighters[(size_t)fighter].costumes) {
			for (int colour : costume.colours) {
				sf4e::selection::Pick pick;
				pick.fighter = fighter;
				pick.costume = costume.id;
				pick.color = colour;
				CHECK(catalog.validPick(pick, false));
				CHECK(catalog.availablePick(pick, false, {}, nullptr));
			}
		}
	}
	// What an SF4 client sends is not: an Ultra, a handicap, a fighter past the
	// roster, a costume or colour the fighter's spec does not have.
	sf4e::selection::Pick ultra;
	ultra.ultra = 1;
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
	CHECK(catalog.validStage(0));
	CHECK_FALSE(catalog.validStage((std::int64_t)space.stages.size()));
	CHECK(catalog.randomStage == -1);

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
