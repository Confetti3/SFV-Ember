// What this copy's player can ready on SFV's catalog, checked without the
// game: the spec's fitting every copy does alike, what is installed here,
// what the game lets them pick (Ember's ownership gate, the command line's
// marks), their stored pick and Training's dummy (SfvPick), and Random.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvPick.hxx"
#include "mod/GameContent.hxx"
#include "mod/ModConfig.hxx"

#include "common/GameCatalog.hxx"
#include "common/RandomFighter.hxx"
#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;

TEST_CASE("a pick is available when this copy has its fighter and costume installed")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const ember::game::GameState& state = ember::game::Game().state;
	const sf4e::selection::Availability availability = state.FighterAvailability(4);
	CHECK(availability.ready);
	// The runtime's Ready check (NetplayRuntime__Room) asks the catalog.
	CHECK_FALSE(catalog.fighterLocked(4, availability));
	CHECK(catalog.availablePick(PickOf(4, 0, 1), false, availability, nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(4, 1, 1), false, availability, nullptr)); // costume not installed
	CHECK(catalog.fighterLocked(3, state.FighterAvailability(3)));                       // fighter not installed
	CHECK_FALSE(catalog.availablePick(PickOf(3, 3, 5), false, state.FighterAvailability(3), nullptr));
	CHECK(catalog.fighterLocked(5, availability));
	Pick noAction = PickOf(0);
	noAction.personalAction = 255;
	CHECK(catalog.availablePick(noAction, false, state.FighterAvailability(0), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(5), false, availability, nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(4, 0, 2), false, availability, nullptr));
}

TEST_CASE("a pick is fitted to the spec: the fighter's first costume, then that costume's first colour")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	Pick pick = PickOf(5, 40, 99);
	pick.ultra = 7; // no loadout
	CHECK(catalog.normalizePick(pick, false, nullptr, nullptr));
	CHECK(pick.fighter == 0);
	CHECK(pick.costume == 0);
	CHECK(pick.color == 0);
	CHECK(picks::LoadoutIn(pick) == kOneOne);
	CHECK_FALSE(catalog.normalizePick(pick, false, nullptr, nullptr));
	// A fighter with few costumes at a high costume number, and a costume
	// with few colours at a high colour number.
	Pick few = PickOf(1, 40, 120);
	CHECK(catalog.normalizePick(few, false, nullptr, nullptr));
	CHECK(few.costume == 0);
	CHECK(few.color == 0);
	few = PickOf(1, 2, 120);
	CHECK(catalog.normalizePick(few, false, nullptr, nullptr));
	CHECK(few.costume == 2);
	CHECK(few.color == 0);
	// The first of each is the game's first, not number 0.
	Pick firsts = PickOf(3, 0, 0);
	CHECK(catalog.normalizePick(firsts, false, nullptr, nullptr));
	CHECK(firsts.costume == 3);
	CHECK(firsts.color == 5);
	// A colour of the costume stays, an EX one included.
	Pick ex = PickOf(0, 4, 101);
	CHECK_FALSE(catalog.normalizePick(ex, false, nullptr, nullptr));
	ex.color = 9; // costume 1's, not costume 4's
	CHECK(catalog.normalizePick(ex, false, nullptr, nullptr));
	CHECK(ex.color == 0);
	// What this copy has not installed is not the spec's business.
	Pick uninstalled = PickOf(4, 1, 1);
	CHECK_FALSE(catalog.normalizePick(uninstalled, false, nullptr, nullptr));
	CHECK(catalog.randomStage == picks::kRandomStage);
	const sf4e::selection::RandomPool pool; // the facade carries SF4's pool type; SFV reads none
	CHECK(catalog.resolveStage(2, 7, pool, nullptr, {}) == 2);
	CHECK(catalog.resolveStage(9, 7, pool, nullptr, {}) == 0); // not a stage of the universe
	// A chosen stage stays as chosen whatever is unavailable: the Ready
	// check, not the resolution, refuses it.
	CHECK(catalog.resolveStage(2, 7, pool, nullptr, {2}) == 2);
}

TEST_CASE("Random rolls only stages both seated players have, each as likely, and only the rolled stage travels")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const sf4e::selection::RandomPool pool;
	// This copy lacks S26 (id 3), and the opponent has every stage: the
	// runtime's unavailable set at the table is S26 alone. S02 (id 1) is
	// installed here, but the account's cache lacks it, so under the default
	// rule P1's roll never lands on it (section 41), though the room's set
	// says nothing of it.
	const std::vector<std::int64_t> unavailable = {kUninstalledStage};
	std::vector<int> landed(4, 0);
	for (std::uint32_t roll = 0; roll < 10000; roll++) {
		const int stage = catalog.resolveStage(picks::kRandomStage, roll * 2654435761u, pool, nullptr, unavailable);
		REQUIRE(catalog.validStage(stage)); // never Random itself on the wire
		CHECK(StageOffered(stage));
		CHECK(std::find(unavailable.begin(), unavailable.end(), stage) == unavailable.end());
		landed[(size_t)stage]++;
	}
	// Every playable stage is reached, about as often as the other.
	CHECK(landed[0] > 4500);
	CHECK(landed[2] > 4500);
	CHECK(landed[(size_t)kNotEntitledStage] == 0);
	CHECK(landed[(size_t)kUninstalledStage] == 0);
	// The opponent lacking S25 (id 2) as well leaves S01 alone.
	for (std::uint32_t roll = 0; roll < 100; roll++) {
		CHECK(catalog.resolveStage(picks::kRandomStage, roll * 2654435761u, pool, nullptr, {2, kUninstalledStage}) == 0);
	}
	// With no stage left, Random stays unresolved, which the runtime's Ready
	// check refuses (runtime.ready.stage_unavailable).
	CHECK(catalog.resolveStage(picks::kRandomStage, 5, pool, nullptr, {0, 1, 2, 3}) == picks::kRandomStage);
	CHECK_FALSE(catalog.validStage(catalog.resolveStage(picks::kRandomStage, 5, pool, nullptr, {0, 1, 2, 3})));
	// A launch that is not a room's (Training) rolls over the stages this
	// player is offered, so it never sees Random, a stage this copy lacks or
	// one the account's cache lacks.
	PlayerPick random;
	random.pick = PickOf(0);
	random.stage = picks::kRandomStage;
	for (int i = 0; i < 200; i++) {
		const std::string code = OfflineStageCode(random.stage);
		CHECK((code == "S01" || code == "S25"));
	}
	PlayerPick chosen = random;
	chosen.stage = 2;
	CHECK(OfflineStageCode(chosen.stage) == "S25");
}

TEST_CASE("this player's stored stage is Random or one they are offered")
{
	FillTestCatalog();
	PlayerPick pick = CurrentPick();
	pick.stage = picks::kRandomStage;
	CHECK(SetPick(pick).stage == picks::kRandomStage);
	CHECK(PickText(CurrentPick()).find("stage random") != std::string::npos);
	pick.stage = 2;
	CHECK(SetPick(pick).stage == 2);
	// A stage this copy lacks, one installed here that the account's cache
	// lacks (section 41), or none of the universe's, becomes the first stage
	// they are offered.
	pick.stage = (int)kUninstalledStage;
	CHECK(SetPick(pick).stage == 0);
	pick.stage = (int)kNotEntitledStage;
	CHECK(SetPick(pick).stage == 0);
	pick.stage = 9;
	CHECK(SetPick(pick).stage == 0);
}

TEST_CASE("with no stage offered the stored stage is Random, never a stage they may not choose")
{
	const int random = picks::kRandomStage;
	// The stage row's choices on a copy whose account grants none of its
	// installed stages: Random alone.
	CHECK(FittedStage(0, {random}) == random);
	CHECK(FittedStage(3, {random}) == random);
	CHECK(FittedStage(random, {random}) == random);
	// With stages offered, the first of them, never stage 0 unless offered.
	CHECK(FittedStage(0, {random, 2, 5}) == 2);
	CHECK(FittedStage(5, {random, 2, 5}) == 5);
	CHECK(FittedStage(random, {random, 2, 5}) == random);
	// What the store asks of it is the stage row's choices with nothing
	// unavailable.
	FillTestCatalog();
	CHECK(PlayableIds(StageOptions({})) == std::vector<int>{random, 0, 2});
}

TEST_CASE("the pipe's pick takes Random or a stage this player is offered, and refuses any other with why")
{
	FillTestCatalog();
	CHECK(NameStage(L"", 2) == NamedStage{2}); // no stage named: it stays
	CHECK(NameStage(L"RND", 2) == NamedStage{picks::kRandomStage});
	CHECK(NameStage(L"S01", 2) == NamedStage{0});
	// S02 is installed here but the account's cache lacks it (section 41).
	// Every refusal keeps the stage the pick had.
	CHECK(NameStage(L"S02", 0) == NamedStage{0, StageRefusal::NotOffered});
	CHECK(NameStage(L"S26", 0) == NamedStage{0, StageRefusal::NotInstalled});
	CHECK(NameStage(L"XYZ", 0) == NamedStage{0, StageRefusal::NotInGame});
}

TEST_CASE("the command line takes stages from this copy's install, never from the universe")
{
	mod::LoadConfigFromCommandLine(L"game.exe -sfve_mode=netplay -sfve_uninstalled=S01,s02,S25,TOOLONGCODE", sfve::BuildKind::Development);
	REQUIRE(mod::Config().uninstalled == std::vector<std::string>{"S01", "S25"});
	CHECK(mod::UninstalledProblem() == "S01,s02,S25,TOOLONGCODE");
	mod::ContentLists lists = TestContent();
	const std::string fingerprint = ContentFingerprint(lists);
	// S01 and S25 go; S02 stays, the last installed stage, whatever else is asked.
	CHECK(mod::MarkUninstalled(lists, {"S01", "S25", "S02", "ZZZ"}) == 2);
	CHECK_FALSE(lists.stages[0].installed);
	CHECK(lists.stages[1].installed);
	CHECK_FALSE(lists.stages[2].installed);
	CHECK_FALSE(lists.stages[3].installed);
	REQUIRE(lists.stages.size() == 4); // the universe stays
	CHECK(ContentFingerprint(lists) == fingerprint);
	mod::LoadConfigFromCommandLine(L"game.exe");
	CHECK(mod::Config().uninstalled.empty());
	CHECK(mod::UninstalledProblem().empty());
	mod::LoadConfigFromCommandLine(L"");
}

TEST_CASE("this player readies only what the game lets them pick; the opponent's pick plays as picked")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const ember::game::GameState& state = ember::game::Game().state;
	// The runtime's Ready check (NetplayRuntime__Room) refuses a fighter the
	// game does not let this player pick, and a pick of a costume or colour
	// they do not own.
	CHECK_FALSE(state.FighterAvailability(2).ready);
	CHECK(catalog.fighterLocked(2, state.FighterAvailability(2)));
	CHECK_FALSE(catalog.availablePick(PickOf(2), false, state.FighterAvailability(2), nullptr));
	CHECK(catalog.availablePick(PickOf(1, 0, 1), false, state.FighterAvailability(1), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(1, 2, 0), false, state.FighterAvailability(1), nullptr));
	CHECK(catalog.availablePick(PickOf(0, 4, 1), false, state.FighterAvailability(0), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(0, 4, 100), false, state.FighterAvailability(0), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(0, 4, 101), false, state.FighterAvailability(0), nullptr));
	// The pick screen says which (NOT OWNED); what is only not installed
	// here (CMY, BSN's costume 1) is not that.
	CHECK(FighterNotOwned(2));
	CHECK_FALSE(FighterNotOwned(3));
	CHECK_FALSE(FighterNotOwned(0));
	CHECK(CostumeNotOwned(1, 2));
	CHECK_FALSE(CostumeNotOwned(4, 1));
	CHECK(ColourNotOwned(0, 4, 101));
	CHECK_FALSE(ColourNotOwned(0, 4, 1));
	CHECK_FALSE(ColourNotOwned(0, 4, 2)); // not a colour of the costume at all
	// The opponent's pick is never judged by this copy: a room's match with
	// KEN against RYU's EX02 in costume 4 launches as picked.
	RoomMatch match = MatchOf(2, 0, 0);
	match.picks[1] = PickOf(0, 4, 101);
	const std::optional<mod::MatchPicks> picks = LaunchPicks(match).picks;
	REQUIRE(picks);
	CHECK(picks->players[0].fighter == L"KEN");
	CHECK(picks->players[1].fighter == L"RYU");
	CHECK(picks->players[1].costume == 4);
	CHECK(picks->players[1].colour == 101);
	// And the spec's fitting, which every copy does alike, never looks at
	// ownership.
	Pick theirs = PickOf(2);
	CHECK_FALSE(catalog.normalizePick(theirs, false, nullptr, nullptr));
	Pick theirsEx = PickOf(0, 4, 101);
	CHECK_FALSE(catalog.normalizePick(theirsEx, false, nullptr, nullptr));
}

// The catalog takes lists whose `owned` is already the rule's: a costume the
// account's cache lacks is not owned under Entitlements, which TestContent's
// CNL costume 3 is (the game's answer leaves it out).
TEST_CASE("a costume the account's cache lacks is not this player's to ready; as the opponent's pick it plays")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const sf4e::selection::Availability cnl = ember::game::Game().state.FighterAvailability(1);
	CHECK(CostumeIds(1, false) == std::vector<int>{0, 2, 3}); // the spec's, every one listed
	CHECK(CostumeIds(1, true) == std::vector<int>{0});
	CHECK(ColourIds(1, 3) == std::vector<int>{0}); // one colour, as SFV's EX character skins have
	CHECK(ColourIds(1, 3, true).empty());
	CHECK_FALSE(catalog.availablePick(PickOf(1, 3, 0), false, cnl, nullptr));
	CHECK(CostumeNotOwned(1, 3)); // the pick screen's NOT OWNED
	CHECK(ColourNotOwned(1, 3, 0));
	Pick own = PickOf(1, 3, 0);
	FitOwnPick(own);
	CHECK(own.costume == 0);
	// The opponent's pick in it launches as picked: LaunchPicks asks the spec
	// and this copy's install, never ownership.
	RoomMatch match = MatchOf(0, 1, 0);
	match.picks[1] = PickOf(1, 3, 0);
	const std::optional<mod::MatchPicks> picks = LaunchPicks(match).picks;
	REQUIRE(picks);
	CHECK(picks->players[1].fighter == L"CNL");
	CHECK(picks->players[1].costume == 3);
	CHECK(picks->players[1].colour == 0);
	// The spec's fitting, the same on every copy, keeps it.
	Pick theirs = PickOf(1, 3, 0);
	CHECK_FALSE(catalog.normalizePick(theirs, false, nullptr, nullptr));
	// A costume this copy has not installed is refused whoever picked it.
	match.picks[1] = PickOf(4, 1, 0);
	const MatchLaunch refused = LaunchPicks(match);
	CHECK_FALSE(refused.picks);
	CHECK(refused.refusal.log.find("not installed here") != std::string::npos);
}

TEST_CASE("the content ownership switch: Entitlements keeps the game's answer, Installed makes every installed look and stage theirs")
{
	const mod::ContentLists game = TestContent();
	// CNL's costume 3: installed here, lacking from the account's cache, and so
	// left out of the game's answer.
	const mod::CostumeContent& lacking = game.fighters[1].costumes[2];
	REQUIRE(lacking.id == 3);
	REQUIRE(lacking.installed);
	REQUIRE_FALSE(lacking.entitled);
	REQUIRE_FALSE(lacking.owned);
	const auto sameOwnership = [](const mod::ContentLists& a, const mod::ContentLists& b) {
		for (size_t f = 0; f < a.fighters.size(); f++) {
			if (a.fighters[f].owned != b.fighters[f].owned) return false;
			for (size_t c = 0; c < a.fighters[f].costumes.size(); c++) {
				const mod::CostumeContent& x = a.fighters[f].costumes[c];
				const mod::CostumeContent& y = b.fighters[f].costumes[c];
				if (x.owned != y.owned || OwnedColours(x) != OwnedColours(y)) return false;
			}
		}
		return true;
	};
	// The stages this copy's player is offered, by code, in order.
	const auto ownedStages = [](const mod::ContentLists& lists) {
		std::vector<std::string> codes;
		for (const mod::StageContent& stage : lists.stages) {
			if (stage.owned) codes.push_back(stage.code);
		}
		return codes;
	};
	// S02: installed here, its codes lacking from the account's cache, so the
	// game's own stage lists leave it out (section 41).
	REQUIRE(game.stages[(size_t)kNotEntitledStage].installed);
	REQUIRE_FALSE(game.stages[(size_t)kNotEntitledStage].entitled);

	mod::ContentLists entitlements = TestContent();
	for (mod::StageContent& stage : entitlements.stages) stage.owned = !stage.owned; // the rule alone decides stages
	mod::ApplyContentOwnership(entitlements, mod::ContentOwnership::Entitlements);
	CHECK(entitlements.ownership.looks == mod::ContentOwnership::Entitlements);
	CHECK(entitlements.ownership.stages == mod::ContentOwnership::Entitlements);
	CHECK(sameOwnership(entitlements, game));
	// Stages under the default: installed and entitled, what SFV's own stage
	// lists offered before the universe became the spec's rows.
	CHECK(ownedStages(entitlements) == std::vector<std::string>{"S01", "S25"});
	CHECK(ownedStages(entitlements) == ownedStages(game));

	mod::ContentLists installed = TestContent();
	mod::ApplyContentOwnership(installed, mod::ContentOwnership::Installed);
	CHECK(installed.ownership.looks == mod::ContentOwnership::Installed);
	CHECK(installed.ownership.stages == mod::ContentOwnership::Installed);
	// Every installed stage, S02 included; S26, not installed, under neither.
	CHECK(ownedStages(installed) == std::vector<std::string>{"S01", "S02", "S25"});
	CHECK(installed.fighters[1].costumes[2].owned);
	CHECK(OwnedColours(installed.fighters[1].costumes[2]) == std::vector<int>{0});
	// Colours too: RYU's EX colours of costume 4, and every colour of his
	// costume 1, are installed with their costumes.
	CHECK(OwnedColours(installed.fighters[0].costumes[2]) == std::vector<int>{0, 1, 100, 101});
	CHECK(OwnedColours(installed.fighters[0].costumes[1]) == mod::ColourNumbers(installed.fighters[0].costumes[1]));
	// What is not installed is not theirs under either rule: BSN's costume 1.
	CHECK_FALSE(installed.fighters[4].costumes[1].owned);
	CHECK(OwnedColours(installed.fighters[4].costumes[1]).empty());
	// Fighters keep the game's answer: KEN stays not owned.
	CHECK_FALSE(installed.fighters[2].owned);
	// The command line's marks still take a look away under it.
	mod::MarkUnowned(installed, {{"CNL", 3, -1}});
	CHECK_FALSE(installed.fighters[1].costumes[2].owned);

	// While the battle setup asset restricts the picks to its own list, the
	// game's answer stays under either rule.
	mod::ContentLists restricted = TestContent();
	restricted.ownership.restricted = true;
	mod::ApplyContentOwnership(restricted, mod::ContentOwnership::Installed);
	CHECK(sameOwnership(restricted, game));
	// ...and the log names the rule that decided, not the switch.
	CHECK(restricted.ownership.looks == mod::ContentOwnership::Entitlements);
	CHECK(std::string(mod::LooksRule(restricted.ownership)) == "the battle setup's list");
	CHECK(std::string(mod::LooksRule(installed.ownership)) == "installed");
	CHECK(std::string(mod::LooksRule(entitlements.ownership)) == "entitlements");
	// The asset's restriction is the looks' alone: stages follow the switch.
	CHECK(restricted.ownership.stages == mod::ContentOwnership::Installed);
	CHECK(ownedStages(restricted) == ownedStages(installed));

	// The rule is this copy's own: the build id never moves with it.
	CHECK(ContentFingerprint(installed) == ContentFingerprint(game));
	CHECK(ContentFingerprint(entitlements) == ContentFingerprint(game));
	CHECK(std::string(mod::ContentOwnershipName(mod::ContentOwnership::Entitlements)) == "entitlements");
	CHECK(std::string(mod::ContentOwnershipName(mod::ContentOwnership::Installed)) == "installed");
	// The switch's default is Entitlements, which the catalog's fixture (and
	// the stage offers SfvCatalogTests checks) is shaped by.
	CHECK(mod::kContentOwnership == mod::ContentOwnership::Entitlements);
}

TEST_CASE("each colour carries its own facts, and the log's counts are taken from them")
{
	const mod::ContentLists lists = TestContent();
	// RYU's costume 4: colours 0, 1, 100 and 101, the EX ones not his.
	const mod::CostumeContent& nostalgia = lists.fighters[0].costumes[2];
	REQUIRE(mod::ColourOf(nostalgia, 100));
	CHECK(mod::ColourOf(nostalgia, 100)->id == 100);
	CHECK_FALSE(mod::ColourOf(nostalgia, 100)->owned);
	CHECK(mod::ColourOf(nostalgia, 1)->owned);
	CHECK_FALSE(mod::ColourOf(nostalgia, 2));
	CHECK(mod::ColourNumbers(nostalgia) == std::vector<int>{0, 1, 100, 101});

	const mod::ContentCounts counts = mod::CountContent(lists);
	CHECK(counts.fighters == 5);
	CHECK(counts.fightersEntitled == 4);
	CHECK(counts.fightersNotOwned == 1); // KEN
	CHECK(counts.costumes == 10);
	CHECK(counts.costumesInstalled == 9); // all but BSN's costume 1
	CHECK(counts.costumesEntitled == 8);  // all but CNL's costumes 2 and 3
	CHECK(counts.costumesNotOwned == 2);
	CHECK(counts.colours == 28);
	// RYU's costume 1 in every colour and costume 4's EX colours, and CNL's
	// costume 3, which the account's cache lacks.
	CHECK(counts.coloursNotOwned == 13);
	// Those, and CNL's costume 2, whose colour the cache grants but whose
	// costume it does not.
	CHECK(counts.looksNotEntitled == 14);
	CHECK(counts.stages == 4);
	CHECK(counts.stagesInstalled == 3);
	CHECK(counts.stagesNotEntitled == 1); // S02; S26 is not installed
	CHECK(counts.stagesNotOwned == 2);    // S02 and S26
}

TEST_CASE("a costume is theirs to ready only in a colour of theirs, for the fitting and the pick screen alike")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const sf4e::selection::Availability ryu = ember::game::Game().state.FighterAvailability(0);
	// RYU's costume 1 is theirs, yet none of its colours is: no look of it is
	// theirs, so the pick screen neither offers it nor any colour of it, and
	// says it is not owned.
	CHECK(CostumeIds(0, true) == std::vector<int>{0, 4});
	CHECK(ColourIds(0, 1, true).empty());
	CHECK(ColourIds(0, 1) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}); // the spec's, all listed
	CHECK(CostumeNotOwned(0, 1));
	CHECK(ColourNotOwned(0, 1, 3));
	CHECK_FALSE(catalog.availablePick(PickOf(0, 1, 3), false, ryu, nullptr));
	// A pick of it is fitted to their first costume with a look of theirs,
	// not left on it with a colour they cannot ready.
	Pick pick = PickOf(0, 1, 3);
	FitOwnPick(pick);
	CHECK(pick.fighter == 0);
	CHECK(pick.costume == 0);
	CHECK(pick.color == 0);
	CHECK(catalog.availablePick(pick, false, ryu, nullptr));
	// The pick store keeps the fitted pick, as the pick screen writes it back.
	PlayerPick stored;
	stored.pick = PickOf(0, 1, 5);
	CHECK(SetPick(stored).pick.costume == 0);
	// Every pick of the spec, fitted, is one they can ready: the fitting and
	// the Ready check ask the same rule.
	for (int fighter = 0; fighter < catalog.fighterCount; fighter++) {
		for (int costume : CostumeIds(fighter, false)) {
			for (int colour : ColourIds(fighter, costume)) {
				Pick fitted = PickOf(fighter, costume, colour);
				FitOwnPick(fitted);
				INFO(FighterCode(fighter) << " costume " << costume << " colour " << colour);
				const sf4e::selection::Availability availability = OwnedAvailability(fitted.fighter);
				CHECK(catalog.availablePick(fitted, false, availability, nullptr));
				// What the pick screen offers for the fitted pick is what it is.
				const std::vector<int> costumes = CostumeIds(fitted.fighter, true);
				const std::vector<int> colours = ColourIds(fitted.fighter, fitted.costume, true);
				CHECK(std::find(costumes.begin(), costumes.end(), fitted.costume) != costumes.end());
				CHECK(std::find(colours.begin(), colours.end(), fitted.color) != colours.end());
			}
		}
	}
}

TEST_CASE("this player's stored pick falls back to one they can ready, and Random rolls only theirs")
{
	FillTestCatalog();
	// An unowned fighter becomes the first fighter of the roster they may
	// play, in that fighter's first costume and colour.
	Pick pick = PickOf(2);
	FitOwnPick(pick);
	CHECK(pick.fighter == 0);
	CHECK(pick.costume == 0);
	CHECK(pick.color == 0);
	// A fighter not installed here falls back the same way.
	pick = PickOf(3, 3, 5);
	FitOwnPick(pick);
	CHECK(pick.fighter == 0);
	// An unowned costume becomes their first costume of the fighter, an
	// unowned colour their first colour of the costume.
	pick = PickOf(1, 2, 0);
	FitOwnPick(pick);
	CHECK(pick.fighter == 1);
	CHECK(pick.costume == 0);
	CHECK(pick.color == 0);
	pick = PickOf(0, 4, 101);
	FitOwnPick(pick);
	CHECK(pick.costume == 4);
	CHECK(pick.color == 0);
	// What is theirs stays.
	pick = PickOf(1, 0, 1);
	FitOwnPick(pick);
	CHECK(pick.fighter == 1);
	CHECK(pick.color == 1);
	// The pick store (SfvPick) keeps only a pick they can ready.
	PlayerPick stored;
	stored.pick = PickOf(2);
	stored.stage = 2;
	stored = SetPick(stored);
	CHECK(stored.pick.fighter == 0);
	CHECK(stored.stage == 2);
	CHECK(CurrentPick().pick.fighter == 0);
	stored.pick = PickOf(1, 2, 0);
	CHECK(SetPick(stored).pick.costume == 0);
	// Random rolls only fighters they may play (not KEN, not CMY), in a look
	// they can ready.
	const auto availability = [](int id) { return ember::game::Game().state.FighterAvailability(id); };
	CHECK(sf4e::selection::RandomCandidates(availability, false) == std::vector<int>{0, 1, 4});
	const sf4e::selection::Availability cnl = availability(1);
	const Pick rolled = sf4e::selection::RolledPick(1, PickOf(1, 2, 0), false, &cnl);
	CHECK(rolled.costume == 0);
}

// The only test that picks a dummy: until one is picked, the dummy follows
// the player's pick, which no other test relies on.
TEST_CASE("Training's dummy is a pick of its own, fitted as the player's, and the player's until one is picked")
{
	FillTestCatalog();
	PlayerPick player;
	player.pick = PickOf(1, 0, 1);
	player.stage = 2;
	SetPick(player);
	// Until the player picks a dummy, it is their own fighter and look; the
	// dummy has no stage (Training loads on the player's).
	PlayerPick dummy = CurrentDummy();
	CHECK(dummy.pick.fighter == 1);
	CHECK(dummy.pick.color == 1);
	CHECK(dummy.stage == 0);
	// A dummy picked is kept apart from the player's pick, and fitted the
	// same way: an unowned fighter becomes their first one they can play.
	dummy.pick = PickOf(4);
	dummy.stage = 3;
	dummy = SetDummy(dummy);
	CHECK(dummy.pick.fighter == 4);
	CHECK(dummy.stage == 0);
	CHECK(CurrentPick().pick.fighter == 1);
	CHECK(CurrentPick().stage == 2);
	dummy.pick = PickOf(2);
	CHECK(SetDummy(dummy).pick.fighter == 0);
	CHECK(CurrentDummy().pick.fighter == 0);
	// The player's pick changing leaves the dummy as picked.
	player.pick = PickOf(4);
	SetPick(player);
	CHECK(CurrentDummy().pick.fighter == 0);
}

TEST_CASE("the command line marks content unowned for this copy, never owned")
{
	mod::LoadConfigFromCommandLine(L"game.exe -sfve_mode=netplay -sfve_unowned=KEN,RYU:4,CNL:0:1,bad:x,ZZZ", sfve::BuildKind::Development);
	const mod::ModConfig& config = mod::Config();
	CHECK(config.ownershipGate);
	CHECK_FALSE(config.gameOwnershipCheck);
	REQUIRE(config.unowned.size() == 4);
	CHECK(config.unowned[0].fighter == "KEN");
	CHECK(config.unowned[0].costume == -1);
	CHECK(config.unowned[1].fighter == "RYU");
	CHECK(config.unowned[1].costume == 4);
	CHECK(config.unowned[1].colour == -1);
	CHECK(config.unowned[2].colour == 1);
	CHECK(config.unowned[3].fighter == "ZZZ");
	CHECK(mod::UnownedProblem() == "KEN,RYU:4,CNL:0:1,bad:x,ZZZ");
	mod::ContentLists lists = TestContent();
	lists.fighters[2].owned = true; // as if the game let this player pick KEN
	mod::MarkUnowned(lists, config.unowned);
	CHECK_FALSE(lists.fighters[2].owned);
	CHECK_FALSE(lists.fighters[0].costumes[2].owned);
	CHECK(lists.fighters[0].costumes[0].owned);
	CHECK(OwnedColours(lists.fighters[1].costumes[0]) == std::vector<int>{0});
	CHECK(lists.ownership.markedUnowned == 3); // ZZZ is no fighter of the lists
	// Nothing the game does not allow becomes owned.
	CHECK_FALSE(lists.fighters[1].costumes[1].owned);
	mod::LoadConfigFromCommandLine(L"game.exe -sfve_ownership=0 -sfve_game_ownership=1", sfve::BuildKind::Development);
	CHECK_FALSE(mod::Config().ownershipGate);
	CHECK(mod::Config().gameOwnershipCheck);
	CHECK(mod::Config().unowned.empty());
	CHECK(mod::UnownedProblem().empty());
	mod::LoadConfigFromCommandLine(L"");
}
