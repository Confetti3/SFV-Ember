// SFV's catalog filled from a content list, checked without the game: what it
// lists and how the shell names it, its pick and stage rules, the battle a
// room's match launches, the build a room checks, and the room host's copy of
// its pick space. What this copy's player can ready is SfvPickRulesTests';
// the catalog's life (empty, filled once, sealed) is CatalogLifecycleTests'.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"
#include "common/RoomRules.hxx"
#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;

TEST_CASE("the catalog holds the game's fighters, costumes, colours and stages, and the table rules SFV plays")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(ember::Usable(catalog));
	CHECK(catalog.fighterCount == 5);
	// One past the highest costume (RYU's 4) and colour (RYU's EX02, 101).
	CHECK(catalog.costumeLimit == 5);
	CHECK(catalog.colorLimit == 102);
	CHECK(catalog.ultraChoices == picks::kLoadoutChoices); // the loadouts (section 40)
	// The table rules the shell offers: the room's that SFV plays.
	CHECK_FALSE(catalog.editions);
	CHECK(catalog.rematchInPlace); // the post-match prompt over the held WIN (RoomBattle)
	REQUIRE(catalog.roundCountChoices == 4);
	REQUIRE(catalog.roundTimeChoices == 5);
	const int rounds[] = {1, 3, 5, 7}, times[] = {30, 60, 99, 300, 9999};
	for (int i = 0; i < 4; i++) CHECK(catalog.roundCounts[i] == rounds[i]);
	for (int i = 0; i < 5; i++) CHECK(catalog.roundTimes[i] == times[i]);
	CHECK_FALSE(ember::PlayableRoundCount(15));
	// 9999 s is the game's no time limit, and a Practice room's (section 27).
	CHECK(ember::PlayableRoundTime(9999));
	CHECK_FALSE(ember::PlayableRoundTime(546));
	CHECK(FighterCode(0) == "RYU");
	CHECK(FighterCode(4) == "BSN");
	CHECK(FighterCode(5).empty());
	CHECK(FighterCode(-1).empty());
	CHECK(StageCode(2) == "S25");
	// The universe holds a stage this copy has not installed; Random is no stage.
	CHECK(StageCount() == 4);
	CHECK(StageCode(3) == "S26");
	CHECK(StageCode(4).empty());
	CHECK(StageCode(picks::kRandomStage).empty());
	// The shell names a stage by the game's name, by its code where it has none.
	CHECK(StageName(0) == "Stage One");
	CHECK(StageName(1) == "S02");
	CHECK(StageName(2) == "Stage Twenty-five");
	CHECK(StageName(3) == "S26");
	CHECK(StageName(4).empty());
	CHECK(FighterId(L"KEN") == 2);
	CHECK_FALSE(FighterId(L"ZZZ").has_value());
	CHECK(StageId(L"S25") == 2);
	CHECK(StageId(L"S26") == 3);
	CHECK_FALSE(StageId(L"RYU").has_value());
	// Each fighter's costumes in the game's order, and each costume's colours.
	CHECK(CostumeIds(0, false) == std::vector<int>{0, 1, 4});
	CHECK(CostumeIds(4, false) == std::vector<int>{0, 1});
	CHECK(CostumeIds(4, true) == std::vector<int>{0}); // BSN's second is not installed here
	CHECK(CostumeIds(5, false).empty());
	CHECK(ColourIds(0, 4) == std::vector<int>{0, 1, 100, 101});
	CHECK(ColourIds(1, 2) == std::vector<int>{0});
	CHECK(ColourIds(1, 1).empty()); // CNL has no costume 1
	CHECK(CostumeIds(1, true) == std::vector<int>{0});     // CNL's costume 2 is not this player's
	CHECK(ColourIds(0, 4, true) == std::vector<int>{0, 1}); // nor RYU's EX colours of costume 4
}

TEST_CASE("the shared screens name and list SFV's fighters by the game's names, in its order")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(std::string(catalog.fighterName(0)) == "Ryu");
	CHECK(std::string(catalog.fighterName(4)) == "M. Bison");
	CHECK(std::string(catalog.fighterName(3)) == "CMY"); // the game gave no name
	CHECK(std::string(catalog.fighterName(5)) == "?");
	CHECK(std::string(catalog.fighterName(-1)) == "?");
	// One past the highest costume number: every costume the spec has is known.
	CHECK(catalog.costumeCount(0) == 5);
	CHECK(catalog.costumeCount(3) == 4);
	CHECK(catalog.costumeCount(5) == 0);
	for (int at = 0; at < catalog.fighterCount; at++) CHECK(catalog.rosterOrder[at] == at);
}

TEST_CASE("a pick names a fighter of the roster, one of its costumes and one of that costume's colours")
{
	FillTestCatalog();
	const auto valid = ember::Catalog().validPick;
	CHECK(valid(PickOf(0), false));
	CHECK(valid(PickOf(0, 4, 101), true)); // a room's edition rule means nothing to SFV
	CHECK(valid(PickOf(3, 3, 6), false));  // the spec's, though this copy lacks it
	CHECK_FALSE(valid(PickOf(5), false));
	CHECK_FALSE(valid(PickOf(-1), false));
	CHECK_FALSE(valid(PickOf(0, 2, 0), false));   // RYU has no costume 2
	CHECK_FALSE(valid(PickOf(0, 0, 3), false));   // nor colour 3 in costume 0
	CHECK_FALSE(valid(PickOf(0, 4, 2), false));   // nor colour 2 in costume 4
	CHECK_FALSE(valid(PickOf(1, 1, 0), false));   // a costume of another fighter
	CHECK_FALSE(valid(PickOf(3, 0, 0), false));   // CMY's only costume is 3
	CHECK_FALSE(valid(PickOf(0, -1, 0), false));
	// An Ultra number is a loadout the fighter has (section 40): every one of
	// RYU's, CNL's V-Skill II but not its V-Trigger II, and no number past
	// the four loadouts.
	CHECK(valid(PickOf(0, 0, 0, kTwoTwo), false));
	CHECK(valid(PickOf(1, 0, 0, kTwoOne), false));
	CHECK_FALSE(valid(PickOf(1, 0, 0, kOneTwo), false));
	CHECK_FALSE(valid(PickOf(2, 0, 0, kTwoOne), false)); // KEN has one of each
	Pick ultra = PickOf(0);
	ultra.ultra = picks::kLoadoutChoices;
	CHECK_FALSE(valid(ultra, false));
	ultra.ultra = -1;
	CHECK_FALSE(valid(ultra, false));
	Pick handicap = PickOf(0);
	handicap.handicap = 2;
	CHECK_FALSE(valid(handicap, false));
	// SF4's edition, personal action and win quote ride along unread.
	Pick sf4Fields = PickOf(1);
	sf4Fields.edition = 14;
	sf4Fields.personalAction = 255;
	sf4Fields.winQuote = 3;
	CHECK(valid(sf4Fields, false));
}

TEST_CASE("a stage is one of the game's stages, by its place in the list")
{
	FillTestCatalog();
	const auto valid = ember::Catalog().validStage;
	CHECK(valid(0));
	CHECK(valid(2));
	CHECK(valid(3)); // the universe's, though this copy lacks it
	CHECK_FALSE(valid(4));
	CHECK_FALSE(valid(-1));
	CHECK_FALSE(valid(picks::kRandomStage)); // never a stage on the wire
}

TEST_CASE("which stages this copy has installed is its own: the room hears them, and the stage row offers this player's")
{
	FillTestCatalog();
	CHECK(StageInstalled(0));
	CHECK(StageInstalled(2));
	CHECK_FALSE(StageInstalled(kUninstalledStage));
	CHECK_FALSE(StageInstalled(4));
	CHECK_FALSE(StageInstalled(picks::kRandomStage));
	CHECK(MissingStages() == std::vector<std::int64_t>{kUninstalledStage});
	CHECK(ember::game::Game().state.MissingStages() == MissingStages());
	// The library takes the list as the game gives it: ascending ids, each
	// once. And SFV's copies can lack stages, so the room carries the list.
	const std::vector<std::int64_t> missing = ember::game::Game().state.MissingStages();
	CHECK(std::is_sorted(missing.begin(), missing.end()));
	CHECK(std::adjacent_find(missing.begin(), missing.end()) == missing.end());
	CHECK(ember::Catalog().stagesCanBeMissing);
	// S02 is installed here though the account's cache lacks it: the room
	// hears only what this copy has not installed (section 41).
	CHECK(StageInstalled(kNotEntitledStage));
	// This player's own offers, under the default rule (the fixture's `owned`
	// is ContentOwnership::Entitlements'): installed and entitled.
	CHECK(StageOffered(0));
	CHECK_FALSE(StageOffered(kNotEntitledStage));
	CHECK(StageOffered(2));
	CHECK_FALSE(StageOffered(kUninstalledStage));
	CHECK_FALSE(StageOffered(4));
	CHECK_FALSE(StageOffered(picks::kRandomStage));
	// The stage row: Random, then the universe in its order less the stages
	// this player is not offered (this copy's missing S26, and S02 the cache
	// lacks); one only the opponent lacks is listed as such (section 43), and
	// only Random and the rest can be chosen.
	using Offer = StageOffer;
	CHECK(StageOptions(MissingStages()) ==
		std::vector<StageOption>{{picks::kRandomStage, Offer::Playable}, {0, Offer::Playable}, {2, Offer::Playable}});
	const std::vector<StageOption> atTable = StageOptions({2, kUninstalledStage});
	CHECK(atTable ==
		std::vector<StageOption>{{picks::kRandomStage, Offer::Playable}, {0, Offer::Playable}, {2, Offer::OpponentLacks}});
	CHECK(PlayableIds(atTable) == std::vector<int>{picks::kRandomStage, 0});
	// A stage the opponent lacks that this player is not offered either is
	// left out, not dimmed: not being theirs to choose hides it.
	CHECK(StageOptions({kNotEntitledStage, kUninstalledStage}) ==
		std::vector<StageOption>{{picks::kRandomStage, Offer::Playable}, {0, Offer::Playable}, {2, Offer::Playable}});
	// Neither this copy's missing stage nor the stage the cache lacks is ever
	// listed, even where the runtime's set leaves them out.
	CHECK(PlayableIds(StageOptions({})) == std::vector<int>{picks::kRandomStage, 0, 2});
	// The opponent lacking every stage this copy has: Random alone can be chosen.
	CHECK(PlayableIds(StageOptions({0, 1, 2, kUninstalledStage})) == std::vector<int>{picks::kRandomStage});
}

TEST_CASE("a room's match launches its fighters, stage, rounds and time by the game's codes")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(2, 0, 1, 5, 60);
	match.picks[1] = PickOf(0, 4, 101); // RYU's EX02 in costume 4
	const std::optional<mod::MatchPicks> picks = LaunchPicks(match).picks;
	REQUIRE(picks);
	CHECK(picks->target == mod::LaunchTarget::Versus);
	CHECK(picks->players[0].fighter == L"KEN");
	CHECK(picks->players[0].costume == 0);
	CHECK(picks->players[0].colour == 0);
	CHECK(picks->players[1].fighter == L"RYU");
	CHECK(picks->players[1].costume == 4);
	CHECK(picks->players[1].colour == 101);
	CHECK(picks->rules.stage == L"S02");
	// S02 is installed here, though this copy's account does not grant it and
	// its player is never offered it: a stage the room's P1 chose plays on
	// any copy that has it installed (section 41).
	REQUIRE_FALSE(StageOffered(kNotEntitledStage));
	CHECK(match.stage == kNotEntitledStage);
	CHECK(LaunchPicks(match).refusal.log.empty());
	CHECK(picks->rules.rounds == 5);
	CHECK(picks->rules.timeFrames == 60 * 60);

	match.picks[1] = PickOf(0, 4, 2);
	CHECK_FALSE(LaunchPicks(match).picks);                // a colour the costume lacks
	match.picks[1] = PickOf(5);
	CHECK_FALSE(LaunchPicks(match).picks);                // a fighter outside the roster
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 4)).picks);     // a stage outside the list
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, picks::kRandomStage)).picks); // Random is resolved before it travels
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 99)).picks); // a room's endless rounds
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, 9998)).picks); // longer than SFV's clock holds
	// The room's longest time is the game's no time limit, and a Practice
	// room's match says it is one.
	const std::optional<mod::MatchPicks> endless = LaunchPicks(MatchOf(0, 0, 0, 3, 9999)).picks;
	REQUIRE(endless);
	CHECK(endless->rules.timeFrames == mod::MatchRules::kNoTimeLimit);
	CHECK_FALSE(endless->practice);
	RoomMatch practice = MatchOf(0, 0, 0, 1, sf4e::room::PracticeRoundTime);
	practice.practice = true;
	const std::optional<mod::MatchPicks> practised = LaunchPicks(practice).picks;
	REQUIRE(practised);
	CHECK(practised->practice);
	CHECK(practised->rules.timeFrames == mod::MatchRules::kNoTimeLimit);
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 7, 300)).picks);
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 1, 30)).picks);
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 3, 546)).picks);
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 2)).picks);       // an even round count
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, 29)).picks);   // shorter than SFV's shortest round
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, -1)).picks);   // no time SFV plays
}

TEST_CASE("a room's match never launches a stage, fighter or costume this copy has not installed")
{
	FillTestCatalog();
	// The spec has them all, so they are valid on the wire; this copy lacks
	// their packages, so it refuses to launch them and says which.
	using ember::game::EntryRefusal;
	const MatchLaunch onMissingStage = LaunchPicks(MatchOf(0, 1, kUninstalledStage));
	CHECK_FALSE(onMissingStage.picks);
	CHECK(onMissingStage.refusal.reason == EntryRefusal::StageNotInstalled);
	CHECK(onMissingStage.refusal.log == "stage S26 is not installed here");
	RoomMatch missingFighter = MatchOf(0, 1, 0);
	missingFighter.picks[1] = PickOf(3, 3, 5); // CMY
	const MatchLaunch withoutFighter = LaunchPicks(missingFighter);
	CHECK_FALSE(withoutFighter.picks);
	CHECK(withoutFighter.refusal.reason == EntryRefusal::UnsupportedMatch);
	CHECK(withoutFighter.refusal.log == "P2's fighter CMY is not installed here");
	RoomMatch missingCostume = MatchOf(0, 1, 0);
	missingCostume.picks[0] = PickOf(4, 1, 0); // BSN's costume 1
	const MatchLaunch withoutCostume = LaunchPicks(missingCostume);
	CHECK_FALSE(withoutCostume.picks);
	CHECK(withoutCostume.refusal.reason == EntryRefusal::UnsupportedMatch);
	CHECK(withoutCostume.refusal.log == "P1's BSN costume 1 is not installed here");
	// A stage outside the universe is not the game's at all, and rounds SFV
	// cannot play are no match it plays.
	CHECK(LaunchPicks(MatchOf(0, 1, 4)).refusal.reason == EntryRefusal::UnsupportedStage);
	CHECK(LaunchPicks(MatchOf(0, 1, 2, 2)).refusal.reason == EntryRefusal::UnsupportedMatch);
	// The same match on an installed stage, in installed looks, launches.
	const MatchLaunch installed = LaunchPicks(MatchOf(0, 1, 2));
	CHECK(installed.picks);
	CHECK(installed.refusal.log.empty());
	RoomMatch installedLooks = MatchOf(0, 1, 2);
	installedLooks.picks[0] = PickOf(4, 0, 1);
	CHECK(LaunchPicks(installedLooks).picks);
}

TEST_CASE("the build a room checks carries a fingerprint of the game's content lists")
{
	const std::string fingerprint = ContentFingerprint(TestContent());
	CHECK(fingerprint.size() == 16);
	CHECK(ContentFingerprint(TestContent()) == fingerprint);
	mod::ContentLists reordered = TestContent();
	std::swap(reordered.fighters[0], reordered.fighters[1]);
	CHECK(ContentFingerprint(reordered) != fingerprint);
	mod::ContentLists extra = TestContent();
	extra.stages.push_back(Stage("S27"));
	CHECK(ContentFingerprint(extra) != fingerprint);
	// A stage of the universe more, fewer or in another place is another
	// build, installed here or not.
	mod::ContentLists fewer = TestContent();
	fewer.stages.pop_back();
	CHECK(ContentFingerprint(fewer) != fingerprint);
	mod::ContentLists stageOrder = TestContent();
	std::swap(stageOrder.stages[0], stageOrder.stages[1]);
	CHECK(ContentFingerprint(stageOrder) != fingerprint);
	mod::ContentLists recoded = TestContent();
	recoded.stages[3].code = "S27";
	CHECK(ContentFingerprint(recoded) != fingerprint);
	// A costume or a colour more, less or moved is another pick space.
	mod::ContentLists costume = TestContent();
	costume.fighters[1].costumes.push_back(Costume(5, {0}));
	CHECK(ContentFingerprint(costume) != fingerprint);
	mod::ContentLists colour = TestContent();
	colour.fighters[0].costumes[2].colours.pop_back();
	CHECK(ContentFingerprint(colour) != fingerprint);
	mod::ContentLists moved = TestContent();
	moved.fighters[0].costumes[0].colours = {{0}, {1}};
	moved.fighters[0].costumes[1].colours.push_back({2});
	CHECK(ContentFingerprint(moved) != fingerprint);
	// A fighter with a V-Trigger or a V-Skill more or less is another pick
	// space (section 40), and so is one whose counts swap.
	mod::ContentLists trigger = TestContent();
	trigger.fighters[1].vTriggers.push_back("Trigger Two");
	CHECK(ContentFingerprint(trigger) != fingerprint);
	mod::ContentLists skill = TestContent();
	skill.fighters[0].vSkills.pop_back();
	CHECK(ContentFingerprint(skill) != fingerprint);
	mod::ContentLists swapped = TestContent();
	std::swap(swapped.fighters[1].vSkills, swapped.fighters[1].vTriggers);
	CHECK(ContentFingerprint(swapped) != fingerprint);
	// Names follow each copy's language, and what is installed or owned is
	// each copy's own: none of them changes the build.
	mod::ContentLists translated = TestContent();
	translated.stages[0].name = "Etappe eins";
	// Two installs that lack different stages share the build
	// (EMBER_LIBRARY.md section 38), and so do ones that own different ones.
	translated.stages[3].installed = true;
	translated.stages[1].installed = false;
	translated.stages[2].owned = false;
	// Nor which stages the account's cache grants (section 41).
	translated.stages[0].entitled = false;
	translated.stages[3].entitled = true;
	translated.stages[3].owned = true;
	translated.fighters[0].name = "Ryu, translated";
	translated.fighters[0].costumes[0].name = "Story, translated";
	translated.fighters[0].vSkills[1] = "Skill Two, translated";
	translated.fighters[1].vTriggers[0] = "Trigger One, translated";
	translated.fighters[3].installed = true;
	translated.fighters[4].costumes[1].installed = true;
	translated.fighters[2].owned = true;
	translated.fighters[1].costumes[1].owned = true;
	for (mod::ColourContent& entry : translated.fighters[0].costumes[2].colours) entry.owned = entry.id == 0;
	// Nor what the account's entitlement cache grants, nor the rule that
	// turns it into ownership (EMBER_LIBRARY.md section 41).
	translated.fighters[1].costumes[2].entitled = true;
	translated.fighters[1].costumes[2].colours[0].entitled = true;
	for (mod::ColourContent& entry : translated.fighters[0].costumes[2].colours) entry.entitled = false;
	translated.fighters[0].entitled = false;
	translated.fighters[4].costumes[0].installed = false;
	translated.ownership.looks = mod::ContentOwnership::Installed;
	translated.ownership.stages = mod::ContentOwnership::Installed;
	CHECK(ContentFingerprint(translated) == fingerprint);
	// A costume more moves the picks' part of the log's fingerprints, never
	// the stages' part.
	const auto parts = [](const mod::ContentLists& lists) {
		const std::string text = picks::PartFingerprints(PickSpaceOf(lists));
		const size_t comma = text.find(", ");
		REQUIRE(comma != std::string::npos);
		return std::make_pair(text.substr(0, comma), text.substr(comma + 2));
	};
	CHECK(parts(costume).first != parts(TestContent()).first);
	CHECK(parts(costume).second == parts(TestContent()).second);
	CHECK(parts(colour).first != parts(TestContent()).first);
	CHECK(parts(translated) == parts(TestContent()));
	// A code that moved from one list to the other.
	CHECK(ContentFingerprint(CodesOnly({"RYU", "S01"}, {"S02"})) != ContentFingerprint(CodesOnly({"RYU"}, {"S01", "S02"})));
	CHECK(ContentFingerprint(CodesOnly({"RY", "U"}, {"S01"})) != ContentFingerprint(CodesOnly({"R", "YU"}, {"S01"})));

	FillTestCatalog();
	CHECK(MatchBuildId() == std::string(kMatchBuild) + "/" + fingerprint);
	CHECK(ember::game::Game().state.BuildId() == MatchBuildId());
	CHECK(MatchBuildId().size() <= 128); // the room's limit for a build
}

// The numbers of SFV's whole universe (EMBER_LIBRARY.md section 41): CHUN-LI
// has costumes 0 to 34, her "EX character" skin (23) one colour, and EX
// colours run to 112 (EX13).
TEST_CASE("every costume SFV has fits the pick space: costume 34, a one-colour costume, EX colours to 112")
{
	picks::PickSpace space;
	picks::Fighter cnl{"CNL", {}, 2, 1};
	for (int id = 0; id < 35; id++) cnl.costumes.push_back({id, id == 23 ? std::vector<int>{0} : std::vector<int>{0, 1}});
	cnl.costumes[0].colours = {0, 1, 100, 112};
	space.fighters = {{"RYU", {{0, {0, 1, 101}}, {24, {0, 1}}}, 2, 2}, cnl};
	space.stages = {"S01"};
	CHECK(picks::Problem(space).empty());
	CHECK(picks::CostumeCount(space, 1) == 35);
	CHECK(picks::CostumeCount(space, 0) == 25);
	CHECK(picks::CostumeLimit(space) == 35);
	CHECK(picks::ColourLimit(space) == 113);
	CHECK(picks::ValidPick(space, PickOf(1, 23, 0)));
	CHECK_FALSE(picks::ValidPick(space, PickOf(1, 23, 1)));
	CHECK(picks::ValidPick(space, PickOf(1, 34, 1)));
	CHECK_FALSE(picks::ValidPick(space, PickOf(1, 35, 0)));
	CHECK(picks::ValidPick(space, PickOf(1, 0, 112)));
	CHECK(picks::ValidPick(space, PickOf(0, 24, 1)));
	CHECK_FALSE(picks::ValidPick(space, PickOf(0, 24, 2)));
	CHECK(picks::ValidPick(space, PickOf(0, 0, 101)));
	// And the room host reads it back as the same build.
	picks::PickSpace back;
	std::string build, error;
	REQUIRE(picks::FromJson(picks::ToJson(space, picks::BuildId(space)), back, build, error));
	CHECK(picks::Fingerprint(back) == picks::Fingerprint(space));
}

TEST_CASE("the room host's data is the catalog's pick space, and reads back as the same build")
{
	FillTestCatalog();
	const std::string text = PickSpaceJson();
	picks::PickSpace space;
	std::string build, error;
	REQUIRE(picks::FromJson(text, space, build, error));
	CHECK(build == MatchBuildId());
	CHECK(picks::BuildId(space) == MatchBuildId());
	CHECK(picks::Fingerprint(space) == ContentFingerprint(TestContent()));
	CHECK(picks::Problem(space).empty());
	// The same rules over the data as over the game's lists.
	for (int fighter = 0; fighter < (int)space.fighters.size(); fighter++) {
		for (int costume = 0; costume < 12; costume++) {
			for (int colour = 0; colour < 120; colour++) {
				Pick pick;
				pick.fighter = fighter;
				pick.costume = costume;
				pick.color = colour;
				CHECK(picks::ValidPick(space, pick) == ember::Catalog().validPick(pick, false));
			}
		}
		for (int ultra = -1; ultra <= picks::kLoadoutChoices; ultra++) {
			Pick pick = PickOf(fighter);
			ember::Catalog().normalizePick(pick, false, nullptr, nullptr);
			pick.ultra = ultra;
			CHECK(picks::ValidPick(space, pick) == ember::Catalog().validPick(pick, false));
		}
		CHECK(space.fighters[(size_t)fighter].vSkills == (int)TestContent().fighters[(size_t)fighter].vSkills.size());
		CHECK(space.fighters[(size_t)fighter].vTriggers == (int)TestContent().fighters[(size_t)fighter].vTriggers.size());
	}
	CHECK(picks::CostumeLimit(space) == ember::Catalog().costumeLimit);
	CHECK(picks::ColourLimit(space) == ember::Catalog().colorLimit);
	// Only what ToJson writes is read.
	nlohmann::json extra = nlohmann::json::parse(text);
	extra["names"] = nlohmann::json::array();
	CHECK_FALSE(picks::FromJson(extra.dump(), space, build, error));
	nlohmann::json colour = nlohmann::json::parse(text);
	colour["fighters"][0]["costumes"][0]["colours"][0] = "01";
	CHECK_FALSE(picks::FromJson(colour.dump(), space, build, error));
	nlohmann::json stages = nlohmann::json::parse(text);
	stages.erase("stages");
	CHECK_FALSE(picks::FromJson(stages.dump(), space, build, error));
	// A fighter without its V-Skill or V-Trigger count is refused, and the
	// data's counts read back as written; a count outside 1 and 2 reads, and
	// Problem refuses it, as it refuses one in lists made by hand.
	nlohmann::json counts = nlohmann::json::parse(text);
	counts["fighters"][0].erase("v_triggers");
	CHECK_FALSE(picks::FromJson(counts.dump(), space, build, error));
	counts = nlohmann::json::parse(text);
	counts["fighters"][1]["v_skills"] = "2";
	CHECK_FALSE(picks::FromJson(counts.dump(), space, build, error));
	counts = nlohmann::json::parse(text);
	counts["fighters"][2]["v_skills"] = 3;
	REQUIRE(picks::FromJson(counts.dump(), space, build, error));
	CHECK(space.fighters[2].vSkills == 3);
	CHECK_FALSE(picks::Problem(space).empty());
	counts["fighters"][2]["v_skills"] = 0;
	REQUIRE(picks::FromJson(counts.dump(), space, build, error));
	CHECK_FALSE(picks::Problem(space).empty());
	CHECK_FALSE(picks::FromJson("not json", space, build, error));
}

TEST_CASE("the rules a room host offers for SFV are the ones VersusLaunch plays, 9999 s as no time limit")
{
	FillTestCatalog();
	for (std::uint8_t rounds : sf4e::room::RoundCounts) {
		const bool listed = std::find(std::begin(picks::kRoundCounts), std::end(picks::kRoundCounts), rounds) != std::end(picks::kRoundCounts);
		CHECK(listed == mod::PlayableRounds(rounds));
	}
	for (std::uint16_t seconds : sf4e::room::RoundTimes) {
		const bool listed = std::find(std::begin(picks::kRoundTimes), std::end(picks::kRoundTimes), seconds) != std::end(picks::kRoundTimes);
		CHECK(listed == mod::RoomClockFrames(seconds).has_value());
	}
	// The room's longest time, a Practice room's, is the game's no time limit.
	CHECK(mod::RoomClockFrames(sf4e::room::PracticeRoundTime) == mod::MatchRules::kNoTimeLimit);
	CHECK(mod::RoomClockFrames(99) == 99 * 60);
	CHECK_FALSE(mod::RoundClockFrames(9999).has_value()); // the pipe's own times stay 30-546 s
	CHECK(ember::PlayableRoundTime(sf4e::room::PracticeRoundTime) == ember::Usable(ember::Catalog()));
}
