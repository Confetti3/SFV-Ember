// The SFV catalog's life in one process: empty until the game's content lists
// fill it, filled once with one pick space, and final once Ember's runtime
// starts. Each step changes the process for good (nothing empties or unseals
// the catalog), so this is its own test program (sfve_catalog_lifecycle_tests)
// and one test case, run in order; sfve_ember_tests fills the catalog freely
// and never seals it.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"

#include <string>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;

TEST_CASE("the catalog is empty until the game's lists fill it, takes one pick space, and is final once the runtime starts")
{
	// Empty: no fighter, and every list that makes no pick space is refused.
	CHECK_FALSE(ember::Usable(ember::Catalog()));
	CHECK_FALSE(ember::ValidFighter(0));
	// The shell names fighter 0 before the lists are filled.
	CHECK(std::string(ember::Catalog().fighterName(0)) == "?");
	CHECK_FALSE(FillCatalog(CodesOnly({}, {"S01"})));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {})));
	// A pick space needs a costume for every fighter and a colour for every
	// costume, and numbers a room's byte carries.
	mod::ContentLists bare = CodesOnly({"RYU"}, {"S01"});
	bare.fighters[0].costumes.clear();
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(0, {})};
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(256, {0})};
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(0, {256})};
	CHECK_FALSE(FillCatalog(bare));
	CHECK_FALSE(ember::Usable(ember::Catalog()));

	// Filled: what it then holds is SfvCatalogTests'.
	FillTestCatalog();
	CHECK(ember::Usable(ember::Catalog()));
	CHECK(ember::Catalog().fighterCount == 5);

	// Filled once: the same lists again are fine, other lists are refused.
	CHECK(FillCatalog(TestContent()));
	mod::ContentLists renamed = TestContent(); // names and installs are this copy's
	renamed.fighters[0].name = "Ryu in another language";
	renamed.fighters[3].installed = true;
	renamed.stages[3].installed = true; // so is which stages the install has
	CHECK(FillCatalog(renamed));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {"S01"})));
	mod::ContentLists recoloured = TestContent();
	recoloured.fighters[2].costumes[0].colours.push_back(1);
	CHECK_FALSE(FillCatalog(recoloured));
	CHECK(ember::Catalog().fighterCount == 5);

	// Once Ember's runtime starts, the catalog refuses every fill, even of
	// the lists it holds.
	SealCatalog();
	CHECK_FALSE(FillCatalog(TestContent()));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {"S01"})));
	CHECK(ember::Catalog().fighterCount == 5);
}
