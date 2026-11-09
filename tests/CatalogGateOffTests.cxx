// The SFV catalog filled with Ember's ownership gate off (FillCatalog's
// ownershipGate false, as -sfve_ownership=0 starts a development copy): this
// player may pick whatever this copy has installed, stages included
// (EMBER_LIBRARY.md section 41). A filled catalog keeps its gate, so this is
// its own test program (sfve_catalog_gate_off_tests); sfve_ember_tests fills
// the catalog with the gate on.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"

#include "common/GameCatalog.hxx"

#include <optional>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;

TEST_CASE("with the gate off this player is offered every stage installed here, and what the account grants does not count")
{
	REQUIRE(FillCatalog(TestContent(), false));
	// A filled catalog keeps its gate: the same lists with the gate on are refused.
	CHECK_FALSE(FillCatalog(TestContent()));
	// S02 is installed here and the account's cache lacks it: offered now.
	// S26 is not installed: never offered, gate or not.
	CHECK(StageOffered(0));
	CHECK(StageOffered(kNotEntitledStage));
	CHECK(StageOffered(2));
	CHECK_FALSE(StageOffered(kUninstalledStage));
	CHECK(StageOptions({}) == std::vector<StageOption>{{picks::kRandomStage, StageOffer::Playable}, {0, StageOffer::Playable},
		{1, StageOffer::Playable}, {2, StageOffer::Playable}});
	// The opponent lacks S02: listed, as theirs to lack; Random rolls over
	// the rest.
	const std::vector<StageOption> atTable = StageOptions({kNotEntitledStage, kUninstalledStage});
	CHECK(atTable[2] == StageOption{1, StageOffer::OpponentLacks});
	CHECK(PlayableIds(atTable) == std::vector<int>{picks::kRandomStage, 0, 2});
	// The stored stage keeps S02, and the pipe's pick takes it.
	PlayerPick pick = CurrentPick();
	pick.stage = (int)kNotEntitledStage;
	CHECK(SetPick(pick).stage == (int)kNotEntitledStage);
	CHECK(NameStage(L"S02", 0) == NamedStage{(int)kNotEntitledStage});
	CHECK(NameStage(L"S26", 0) == NamedStage{0, StageRefusal::NotInstalled});
	// What the room hears this copy lacks is the install's, as with the gate on.
	CHECK(MissingStages() == std::vector<std::int64_t>{kUninstalledStage});
}

TEST_CASE("with the gate off nothing the game withholds is NOT OWNED, and only what this copy lacks is locked")
{
	REQUIRE(FillCatalog(TestContent(), false));
	// KEN, which the game does not let this copy's player pick, is theirs.
	CHECK_FALSE(FighterNotOwned(2));
	CHECK_FALSE(ember::Catalog().fighterLocked(2, {}));
	// CNL's costume the account's cache lacks, and RYU's EX colours of
	// Nostalgia.
	CHECK_FALSE(CostumeNotOwned(1, 3));
	CHECK(CostumeIds(1, true) == std::vector<int>{0, 2, 3});
	CHECK(ColourIds(0, 4, true) == std::vector<int>{0, 1, 100, 101});
	// CMY is not installed here: locked whatever the gate says.
	CHECK(ember::Catalog().fighterLocked(3, {}));
}
