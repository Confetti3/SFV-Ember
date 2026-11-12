// Eleven (docs/design/beta8/eleven.md): the rule that turns a rolled fighter
// and a seed into the pick the game plays (SfvEleven), and how the pick store
// and Random's follow take it: Random's rolled fighter in the look the game's
// Eleven wears, a loadout drawn for each roll, never a look this player cannot
// ready, and nothing remembered for the fighters it rolled. The fighters are
// CatalogFixture's: RYU (0) two of each part, CNL (1) two V-Skills and one
// V-Trigger, KEN (2) not this player's, CMY (3) not installed, BSN (4) two of
// each; the game's ELEVEN lists one costume (0) in one colour (2).

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRandomFighter.hxx"

#include "common/GameCatalog.hxx"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;

namespace {

constexpr int kRyu = 0, kCnl = 1, kKen = 2, kCmy = 3, kBsn = 4;

bool SameLook(const sf4e::selection::Pick& a, const sf4e::selection::Pick& b)
{
	return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && picks::LoadoutOf(a) == picks::LoadoutOf(b);
}

} // namespace

TEST_CASE("the game's ELEVEN is read as the spec lists it and is no fighter of the pick space")
{
	FillTestCatalog();
	const std::optional<ElevenSpec> spec = ElevenSpecOf();
	REQUIRE(spec.has_value());
	CHECK(*spec == ElevenSpec{0, 2});
	CHECK(ElevenName() == "ELEVEN");
	// No pick ever names it: it is neither a fighter of the catalog nor in its pick space.
	CHECK(ember::Catalog().fighterCount == 5);
	CHECK_FALSE(FighterId(L"ELV").has_value());
	for (int fighter = 0; fighter < ember::Catalog().fighterCount; fighter++) CHECK(FighterCode(fighter) != "ELV");
	// The fingerprint is the pick space's alone: ELEVEN's row never enters it.
	mod::ContentLists without = TestContent();
	without.eleven.reset();
	CHECK(ContentFingerprint(without) == ContentFingerprint(TestContent()));
}

TEST_CASE("Eleven's pick is the same for a fighter and a seed, in the Eleven look where the fighter has it")
{
	FillTestCatalog();
	const sf4e::selection::Pick first = ElevenPick(kRyu, 12345);
	const sf4e::selection::Pick again = ElevenPick(kRyu, 12345);
	CHECK(SameLook(first, again));
	CHECK(first.fighter == kRyu);
	// RYU has costume 0 in colour 2: the game's Eleven look as it is.
	CHECK(first.costume == 0);
	CHECK(first.color == 2);
	CHECK(first.size == picks::kSizeNormal);
	// CNL's costume 0 has no colour 2: the fighter's first colour of that costume.
	const sf4e::selection::Pick chun = ElevenPick(kCnl, 12345);
	CHECK(chun.fighter == kCnl);
	CHECK(chun.costume == 0);
	CHECK(chun.color == 0);
	// The loadout is the seed's alone: the same on every call, for every fighter.
	for (std::uint64_t seed = 1; seed < 40; seed++) {
		CAPTURE(seed);
		CHECK(SameLook(ElevenPick(kBsn, seed), ElevenPick(kBsn, seed)));
	}
	// A fighter the catalog lacks comes back as the id, untouched.
	const sf4e::selection::Pick none = ElevenPick(99, 7);
	CHECK(none.fighter == 99);
	CHECK(none.costume == 0);
	CHECK(none.color == 0);
}

TEST_CASE("Eleven draws each loadout part from the choices the fighter has, and every choice comes up")
{
	FillTestCatalog();
	std::set<int> ryuSkills, ryuTriggers, chunSkills, chunTriggers;
	for (std::uint64_t seed = 0; seed < 400; seed++) {
		const picks::Loadout ryu = picks::LoadoutOf(ElevenPick(kRyu, ElevenSeed(7, seed)));
		const picks::Loadout chun = picks::LoadoutOf(ElevenPick(kCnl, ElevenSeed(7, seed)));
		ryuSkills.insert(ryu.choice[picks::VSkill]);
		ryuTriggers.insert(ryu.choice[picks::VTrigger]);
		chunSkills.insert(chun.choice[picks::VSkill]);
		chunTriggers.insert(chun.choice[picks::VTrigger]);
	}
	CHECK(ryuSkills == std::set<int>{0, 1});
	CHECK(ryuTriggers == std::set<int>{0, 1});
	CHECK(chunSkills == std::set<int>{0, 1});
	// CNL has one V-Trigger, so it is always I.
	CHECK(chunTriggers == std::set<int>{0});
}

TEST_CASE("Eleven never gives a fighter this player may play a look they cannot ready, with the gate on")
{
	FillTestCatalog();
	int fighters = 0;
	for (int fighter = 0; fighter < ember::Catalog().fighterCount; fighter++) {
		if (ember::Catalog().fighterLocked(fighter, {})) continue;
		fighters++;
		for (std::uint64_t seed = 1; seed < 60; seed++) {
			CAPTURE(fighter);
			CAPTURE(seed);
			const sf4e::selection::Pick pick = ElevenPick(fighter, seed);
			REQUIRE(pick.fighter == fighter);
			CHECK_FALSE(FighterNotOwned(fighter));
			CHECK_FALSE(CostumeNotOwned(fighter, pick.costume));
			CHECK_FALSE(ColourNotOwned(fighter, pick.costume, pick.color));
			const std::vector<int> ready = ColourIds(fighter, pick.costume, true);
			CHECK(std::find(ready.begin(), ready.end(), pick.color) != ready.end());
			CHECK(ember::Catalog().validPick(pick, false));
			// It is a pick the player's own fitting leaves alone.
			sf4e::selection::Pick fitted = pick;
			FitOwnPick(fitted);
			CHECK(SameLook(fitted, pick));
		}
	}
	// RYU, CNL and BSN: KEN is not theirs and CMY is not installed.
	CHECK(fighters == 3);
	CHECK(ember::Catalog().fighterLocked(kKen, {}));
	CHECK(ember::Catalog().fighterLocked(kCmy, {}));
}

TEST_CASE("the seed is the roll mixed with this run's salt: the same for the same two, another for another roll or run")
{
	CHECK(ElevenSeed(5, 1) == ElevenSeed(5, 1));
	CHECK(ElevenSeed(5, 1) != ElevenSeed(5, 2));
	CHECK(ElevenSeed(5, 1) != ElevenSeed(6, 1));
	// Consecutive rolls do not walk in step.
	std::set<std::uint64_t> seeds;
	for (std::uint64_t roll = 1; roll <= 64; roll++) seeds.insert(ElevenSeed(5, roll));
	CHECK(seeds.size() == 64);
	// The run's salt is drawn once, never zero, and then stays.
	const std::uint64_t salt = ElevenSalt();
	CHECK(salt != 0);
	CHECK(ElevenSalt() == salt);
	SetElevenSaltForTest(42);
	CHECK(ElevenSalt() == 42);
	SetElevenSaltForTest(salt);
	CHECK(ElevenSalt() == salt);
}

TEST_CASE("Eleven takes each roll into the player's pick in the Eleven look, keeps the fighters' remembered looks, and leaves the dummy alone")
{
	FillTestCatalog();
	SetPickRandom(false); // and so Eleven off, as the other tests leave it
	SetElevenSaltForTest(0xE1E1E1E1ull);
	// RYU last stored in Nostalgia, colour 02; the pick is now CNL; the
	// dummy is BSN.
	PlayerPick ryu;
	ryu.pick = PickOf(kRyu, 4, 1, kTwoTwo);
	ryu = SetPick(ryu);
	PlayerPick chun;
	chun.pick = PickOf(kCnl, 0, 0, kOneOne);
	SetPick(chun);
	PlayerPick bison;
	bison.pick = PickOf(kBsn, 0, 0, kOneOne);
	const PlayerPick dummy = SetDummy(bison);
	sf4e::NetplayFacade::RuntimeSnapshot snapshot{};
	snapshot.randomFighter = kRyu;
	snapshot.randomRoll = 0xE1000001;
	// Not Eleven (nor Random): nothing taken.
	SetPickRandom(false);
	CHECK_FALSE(PickEleven());
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.fighter == kCnl);
	// Eleven is Random's pick, in the Eleven look: the roll, once.
	SetPickEleven(true);
	CHECK(PickEleven());
	CHECK(PickRandom());
	FollowRandomFighter(snapshot);
	const sf4e::selection::Pick drawn = ElevenPick(kRyu, ElevenSeed(ElevenSalt(), snapshot.randomRoll));
	CHECK(SameLook(CurrentPick().pick, drawn));
	CHECK(CurrentPick().pick.costume == 0);
	CHECK(CurrentPick().pick.color == 2);
	// RYU's remembered look is still the one the player chose.
	REQUIRE(RememberedLook(kRyu).has_value());
	CHECK(SameLook(*RememberedLook(kRyu), ryu.pick));
	// The same roll again keeps an edit made since.
	PlayerPick edited = CurrentPick();
	edited.pick.color = 0;
	SetPick(edited);
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.color == 0);
	CHECK(SameLook(*RememberedLook(kRyu), ryu.pick));
	// The next roll, RYU again: every game is drawn anew, the look back to Eleven's.
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	CHECK(SameLook(CurrentPick().pick, ElevenPick(kRyu, ElevenSeed(ElevenSalt(), snapshot.randomRoll))));
	CHECK(CurrentPick().pick.color == 2);
	// The next, BSN.
	snapshot.randomFighter = kBsn;
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	CHECK(SameLook(CurrentPick().pick, ElevenPick(kBsn, ElevenSeed(ElevenSalt(), snapshot.randomRoll))));
	CHECK(CurrentPick().pick.fighter == kBsn);
	// A roll to no fighter of the catalog is never taken.
	snapshot.randomFighter = 99;
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.fighter == kBsn);
	// The dummy is untouched throughout.
	CHECK(SameLook(CurrentDummy().pick, dummy.pick));
	// Random in Eleven's place takes the roll in hand again, in RYU's
	// remembered look, and the pick is no longer Eleven.
	snapshot.randomFighter = kRyu;
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	SetPickRandom(true);
	CHECK(PickRandom());
	CHECK_FALSE(PickEleven());
	FollowRandomFighter(snapshot);
	CHECK(SameLook(CurrentPick().pick, ryu.pick));
	// And back: Eleven takes that same roll again, in its look.
	SetPickEleven(true);
	FollowRandomFighter(snapshot);
	CHECK(SameLook(CurrentPick().pick, ElevenPick(kRyu, ElevenSeed(ElevenSalt(), snapshot.randomRoll))));
	// Turning it off turns Random off with it; choosing Random or a fighter turns Eleven off.
	SetPickEleven(false);
	CHECK_FALSE(PickEleven());
	CHECK_FALSE(PickRandom());
	SetPickEleven(true);
	SetPickRandom(false);
	CHECK_FALSE(PickEleven());
	CHECK_FALSE(PickRandom());
}
