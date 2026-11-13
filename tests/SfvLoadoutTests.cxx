// A pick's V-Skill and V-Trigger (EMBER_LIBRARY.md section 40), checked
// without the game: the one codec between SFV's Loadout and the shared
// pick's Ultra slot, the pick space's rules over each fighter's counts, the
// catalog's view of them, the content reader's fitting of the counts the
// game's name assets give, the launch's form of a loadout (the pipe's
// words, the per-fighter record's two fields), and the words the shared
// screens name a loadout by.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/GameCatalog.hxx"
#include "common/RandomFighter.hxx"
#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;

namespace {

// A pick space of two fighters on one stage: TWO with two of each, ONE with
// one of each, each in one costume and colour.
picks::PickSpace TwoFighters()
{
	picks::PickSpace space;
	space.fighters.push_back({"TWO", {{0, {0}}}, 2, 2});
	space.fighters.push_back({"ONE", {{0, {0}}}, 1, 1});
	space.stages = {"S01"};
	return space;
}

} // namespace

TEST_CASE("a loadout and its Ultra number are one codec, and nothing else is a loadout")
{
	const picks::Loadout all[] = {kOneOne, kOneTwo, kTwoOne, kTwoTwo};
	for (int index = 0; index < picks::kLoadoutChoices; index++) {
		REQUIRE(picks::LoadoutAt(index));
		CHECK(*picks::LoadoutAt(index) == all[index]);
		CHECK(picks::LoadoutIndex(all[index]) == index);
		Pick pick;
		picks::SetLoadout(pick, all[index]);
		CHECK(picks::LoadoutOf(pick) == all[index]);
		CHECK(picks::LoadoutIn(pick) == all[index]);
	}
	CHECK_FALSE(picks::LoadoutAt(picks::kLoadoutChoices));
	CHECK_FALSE(picks::LoadoutAt(-1));
	// A slot that holds no loadout reads as I and I, and holds none.
	Pick stray;
	stray.ultra = 9;
	CHECK(picks::LoadoutOf(stray) == kOneOne);
	CHECK_FALSE(picks::LoadoutIn(stray));
	// The parts, as the game numbers them.
	CHECK(kTwoOne.choice[picks::LoadoutPart::VSkill] == 1);
	CHECK(kTwoOne.choice[picks::LoadoutPart::VTrigger] == 0);
	CHECK(picks::LoadoutOfNumbers(1, 1) == kTwoTwo);
	CHECK(picks::LoadoutOfNumbers(0, 1) == kOneTwo);
	CHECK_FALSE(picks::LoadoutOfNumbers(0, -1));
	CHECK_FALSE(picks::LoadoutOfNumbers(2, 0));
	CHECK(picks::LoadoutText(kTwoOne) == "vskill II vtrigger I");
	CHECK(std::string(picks::Numeral(0)) == "I");
	CHECK(std::string(picks::Numeral(1)) == "II");
	CHECK(std::string(picks::Numeral(2)).empty());
	// The catalog's Ultra numbers are the loadouts, and this build's match
	// version is the one that launches them.
	CHECK(picks::kLoadoutChoices == 4);
	CHECK(std::string(picks::kMatchBuild) == "sfv-ember/13173604/0.7");
}

TEST_CASE("a pick holds only loadouts its fighter has, and every copy fits one alike")
{
	const picks::PickSpace space = TwoFighters();
	REQUIRE(picks::Problem(space).empty());
	Pick pick;
	picks::SetLoadout(pick, kTwoTwo);
	CHECK(picks::ValidPick(space, pick));
	pick.fighter = 1;
	CHECK_FALSE(picks::ValidPick(space, pick)); // ONE has no II of either
	picks::SetLoadout(pick, kOneOne);
	CHECK(picks::ValidPick(space, pick));
	pick.ultra = picks::kLoadoutChoices;
	CHECK_FALSE(picks::ValidPick(space, pick));
	pick.ultra = -1;
	CHECK_FALSE(picks::ValidPick(space, pick));
	CHECK(picks::LoadoutIndices(space.fighters[0]) == std::vector<int>{0, 1, 2, 3});
	CHECK(picks::LoadoutIndices(space.fighters[1]) == std::vector<int>{0});

	// The fitting keeps a loadout the fighter has, through a costume's change...
	Pick kept;
	kept.costume = 7; // not TWO's
	picks::SetLoadout(kept, kTwoOne);
	CHECK(picks::FitPick(space, kept));
	CHECK(kept.costume == 0);
	CHECK(picks::LoadoutOf(kept) == kTwoOne);
	CHECK_FALSE(picks::FitPick(space, kept));
	// ...fits each part the fighter lacks to I, and leaves the other: a
	// fighter with two V-Skills and one V-Trigger keeps V-Skill II.
	picks::PickSpace mixed = space;
	mixed.fighters[1].vSkills = 2;
	Pick fitted;
	fitted.fighter = 1;
	picks::SetLoadout(fitted, kTwoTwo);
	CHECK(picks::FitPick(mixed, fitted));
	CHECK(picks::LoadoutOf(fitted) == kTwoOne);
	// ...and a slot that holds no loadout becomes I and I.
	Pick stray;
	stray.ultra = 6;
	CHECK(picks::FitPick(space, stray));
	CHECK(picks::LoadoutOf(stray) == kOneOne);
	// Two copies of the same pick space fit the same pick the same way.
	const picks::PickSpace other = TwoFighters();
	Pick here, there;
	here.fighter = there.fighter = 1;
	picks::SetLoadout(here, kTwoTwo);
	picks::SetLoadout(there, kTwoTwo);
	picks::FitPick(space, here);
	picks::FitPick(other, there);
	CHECK(picks::SameFitting(here, there));

	// A count outside 1 and 2 makes no pick space.
	picks::PickSpace none = space;
	none.fighters[0].vTriggers = 0;
	CHECK_FALSE(picks::Problem(none).empty());
	picks::PickSpace three = space;
	three.fighters[1].vSkills = 3;
	CHECK_FALSE(picks::Problem(three).empty());
}

TEST_CASE("the fingerprint counts each fighter's V-Skills and V-Triggers, never their names")
{
	const picks::PickSpace space = TwoFighters();
	const std::string fingerprint = picks::Fingerprint(space);
	picks::PickSpace fewer = space;
	fewer.fighters[0].vTriggers = 1;
	CHECK(picks::Fingerprint(fewer) != fingerprint);
	picks::PickSpace swapped = space;
	swapped.fighters[0].vSkills = 1;
	swapped.fighters[1].vSkills = 2;
	CHECK(picks::Fingerprint(swapped) != fingerprint);
	// The pick space round-trips through the room host's data with its counts.
	std::string build, error;
	picks::PickSpace back;
	REQUIRE(picks::FromJson(picks::ToJson(space, picks::BuildId(space)), back, build, error));
	CHECK(back.fighters[0].vSkills == 2);
	CHECK(back.fighters[1].vTriggers == 1);
	CHECK(picks::Fingerprint(back) == fingerprint);
	CHECK(build == picks::BuildId(space));
}

TEST_CASE("the content reader makes any count the game's assets give one or two choices")
{
	// None (a fighter the asset does not list) is one unnamed choice; three
	// are the first two; one or two are as given. Each change is logged.
	CHECK(mod::FitChoices({}, "V-Skill", "RYU") == std::vector<std::string>{""});
	CHECK(mod::FitChoices({"A", "B", "C"}, "V-Trigger", "RYU") == std::vector<std::string>{"A", "B"});
	CHECK(mod::FitChoices({"A", "B"}, "V-Trigger", "RYU") == std::vector<std::string>{"A", "B"});
	CHECK(mod::FitChoices({"A"}, "V-Skill", "RYU") == std::vector<std::string>{"A"});
	// A fighter's content before the assets are read has one of each.
	const mod::FighterContent unread{};
	CHECK(unread.vSkills.size() == 1u);
	CHECK(unread.vTriggers.size() == 1u);
	CHECK(mod::ChoicesText(TestContent().fighters[1]) == "V-Skills 2 (\"Skill One\", \"Skill Two\"), V-Triggers 1 (\"Trigger One\")");
}

TEST_CASE("the catalog offers each fighter's own loadouts, and this player's pick keeps one through a fighter's change")
{
	FillTestCatalog();
	CHECK(PartChoices(0, picks::LoadoutPart::VSkill) == 2);
	CHECK(PartChoices(1, picks::LoadoutPart::VTrigger) == 1);
	CHECK(PartChoices(3, picks::LoadoutPart::VSkill) == 1);
	CHECK(PartChoices(5, picks::LoadoutPart::VSkill) == 0); // not a fighter of the catalog
	CHECK(LoadoutIndices(0) == std::vector<int>{0, 1, 2, 3});
	CHECK(LoadoutIndices(1) == std::vector<int>{0, 2});
	CHECK(LoadoutIndices(3) == std::vector<int>{0, 1});
	CHECK(LoadoutIndices(2) == std::vector<int>{0});
	CHECK(LoadoutIndices(9).empty());
	CHECK(MoveName(0, picks::LoadoutPart::VTrigger, 1) == "Trigger Two");
	CHECK(MoveName(1, picks::LoadoutPart::VTrigger, 1).empty()); // CNL has one
	CHECK(MoveName(4, picks::LoadoutPart::VSkill, 0).empty());   // BSN's are unnamed

	// The stored pick: II and II on RYU stays; on CNL its V-Trigger becomes I.
	PlayerPick pick = CurrentPick();
	pick.pick = PickOf(0, 0, 0, kTwoTwo);
	CHECK(picks::LoadoutOf(SetPick(pick).pick) == kTwoTwo);
	pick.pick = PickOf(1, 0, 0, kTwoTwo);
	CHECK(picks::LoadoutOf(SetPick(pick).pick) == kTwoOne);
	// A fighter this player cannot play (KEN) gives way to the first they
	// can (RYU), and the loadout is fitted to that one.
	pick.pick = PickOf(2, 0, 0, kOneOne);
	const PlayerPick fitted = SetPick(pick);
	CHECK(fitted.pick.fighter == 0);
	CHECK(picks::LoadoutOf(fitted.pick) == kOneOne);
	CHECK(PickText(fitted).find("vskill I vtrigger I") != std::string::npos);

	// A Random roll plays the loadout last used on the rolled fighter,
	// fitted to it, as its costume and colour are.
	const sf4e::selection::Availability ryu = ember::game::Game().state.FighterAvailability(0);
	CHECK(picks::LoadoutOf(sf4e::selection::RolledPick(0, PickOf(0, 0, 0, kOneTwo), false, &ryu)) == kOneTwo);
	const sf4e::selection::Availability cnl = ember::game::Game().state.FighterAvailability(1);
	CHECK(picks::LoadoutOf(sf4e::selection::RolledPick(1, PickOf(1, 0, 0, kTwoTwo), false, &cnl)) == kTwoOne);
}

TEST_CASE("a room's match launches both players' loadouts")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(0, 1, 2);
	picks::SetLoadout(match.picks[0], kTwoTwo);
	picks::SetLoadout(match.picks[1], kTwoOne);
	const std::optional<mod::MatchPicks> launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK(launched->players[0].vSkill == 1);
	CHECK(launched->players[0].vTrigger == 1);
	CHECK(launched->players[1].vSkill == 1);
	CHECK(launched->players[1].vTrigger == 0);
	// The one place a pick's loadout becomes the launch's two numbers.
	const mod::FighterPick cnl = FighterPickOf(match.picks[1]);
	CHECK(cnl.fighter == L"CNL");
	CHECK(cnl.vSkill == 1);
	CHECK(cnl.vTrigger == 0);
	// One the fighter lacks never launches: CNL has one V-Trigger.
	picks::SetLoadout(match.picks[1], kOneTwo);
	const MatchLaunch refused = LaunchPicks(match);
	CHECK_FALSE(refused.picks);
	CHECK(refused.refusal.reason == ember::game::EntryRefusal::UnsupportedMatch);
	CHECK_FALSE(refused.refusal.log.empty());
}

TEST_CASE("the pipe names a V-Skill and a V-Trigger as the game does, 1 and 2")
{
	mod::FighterPick pick;
	mod::MatchRules rules;
	REQUIRE(mod::ParseSetup("RYU:0:1:2:1 S25 3 99", &pick, &rules));
	CHECK(pick.fighter == L"RYU");
	CHECK(pick.costume == 0);
	CHECK(pick.colour == 1);
	CHECK(pick.vSkill == 1);
	CHECK(pick.vTrigger == 0);
	REQUIRE(mod::ParseSetup("KEN:0:2:-:2", &pick, &rules));
	CHECK(pick.vSkill == 0);
	CHECK(pick.vTrigger == 1);
	REQUIRE(mod::ParseSetup("KEN:0:2", &pick, &rules));
	CHECK(pick.vSkill == 0); // a launch always writes both: I unless named
	CHECK(pick.vTrigger == 0);
	CHECK_FALSE(mod::ParseSetup("RYU:0:1:3", &pick, &rules));
	CHECK_FALSE(mod::ParseSetup("RYU:0:1:0", &pick, &rules));
	CHECK_FALSE(mod::ParseSetup("RYU:0:1:1:0", &pick, &rules));
	CHECK_FALSE(mod::ParseSetup("RYU:0:1:1:II", &pick, &rules));
	CHECK_FALSE(mod::ParseSetup("RYU:0:1:1:1:1", &pick, &rules));
}

TEST_CASE("the launch writes the loadout into the game's per-fighter record at +48 and +56")
{
	namespace jump = mod::sfv::jump;
	alignas(8) std::uint8_t chara[jump::kCharPkgSize];
	for (std::uint8_t& byte : chara) byte = 0xCD;
	mod::FighterPick pick;
	pick.costume = 3;
	pick.colour = 101;
	pick.vSkill = 1;
	pick.vTrigger = 0;
	mod::FillCharPkg(chara, pick);
	CHECK(mod::Field<std::int32_t>(chara, 16) == 3);
	CHECK(mod::Field<std::int32_t>(chara, 20) == 101);
	CHECK(mod::Field<std::int32_t>(chara, 48) == 0); // V-Trigger I
	CHECK(mod::Field<std::int32_t>(chara, 56) == 1); // V-Skill II
	CHECK(jump::kCharVTrigger == 48);
	CHECK(jump::kCharVSkill == 56);
	// Nothing else of the record changes: the fields between are the game's.
	CHECK(mod::Field<std::int32_t>(chara, 40) == (std::int32_t)0xCDCDCDCD);
	CHECK(mod::Field<std::int32_t>(chara, 44) == (std::int32_t)0xCDCDCDCD);
	CHECK(mod::Field<std::int32_t>(chara, 52) == (std::int32_t)0xCDCDCDCD);
	CHECK(chara[60] == 0xCD);
	// A costume or colour not given keeps the record's; the loadout never does.
	mod::FighterPick defaults;
	mod::FillCharPkg(chara, defaults);
	CHECK(mod::Field<std::int32_t>(chara, 16) == 3);
	CHECK(mod::Field<std::int32_t>(chara, 20) == 101);
	CHECK(mod::Field<std::int32_t>(chara, 48) == 0);
	CHECK(mod::Field<std::int32_t>(chara, 56) == 0);
}

TEST_CASE("a choice is worded by the game's pattern for it, with the move's name")
{
	CHECK(FormatChoice("I: {common1}", "I", "Mind's Eye") == "I: Mind's Eye");
	CHECK(FormatChoice("{common1} (II)", "II", "Axe Kick") == "Axe Kick (II)");
	CHECK(FormatChoice("", "II", "Axe Kick") == "II Axe Kick");     // the game has no pattern
	CHECK(FormatChoice("Second", "II", "Axe Kick") == "Second Axe Kick"); // one without the argument
	CHECK(FormatChoice("I: {common1}", "I", "") == "I");             // no name
	// An argument Ember does not fill never reaches the screen.
	CHECK(FormatChoice("{common1} {common2}", "II", "Axe Kick") == "II Axe Kick");
	CHECK(FormatChoice("{0}: {common1}", "I", "Mind's Eye") == "I Mind's Eye");
	// Until the game gives its words (no game runs in a test), the game's
	// English in Ember's case, so nothing changes case when its words arrive.
	CHECK(std::string(PartLabel(picks::LoadoutPart::VSkill)) == "V-Skill");
	CHECK(std::string(PartLabel(picks::LoadoutPart::VTrigger)) == "V-Trigger");
	CHECK(ChoiceText(1, "Axe Kick") == "II Axe Kick");
	FillTestCatalog();
	CHECK(LoadoutName(0, kTwoOne) == "V-Skill II \xC2\xB7 V-Trigger I: Skill Two \xC2\xB7 Trigger One");
	CHECK(LoadoutName(4, kTwoOne) == "V-Skill II \xC2\xB7 V-Trigger I"); // BSN's are unnamed
	CHECK(LoadoutMoves(1, kTwoOne) == "Skill Two \xC2\xB7 Trigger One");
	CHECK(LoadoutMoves(4, kOneTwo) == "V-Skill I \xC2\xB7 V-Trigger II");
}

TEST_CASE("the game's two labels are set in Ember's case, in every script that has case")
{
	// The game's English labels differ in case; Ember's rows read "Fighter",
	// "Costume", "Color".
	CHECK(RowCase("V-Skill") == "V-Skill");
	CHECK(RowCase("V-TRIGGER") == "V-Trigger");
	CHECK(RowCase("v-trigger") == "V-Trigger");
	CHECK(RowCase("HABILIDAD V") == "Habilidad V");
	CHECK(RowCase("") == "");
	// A digit is part of its word.
	CHECK(RowCase("2ND V") == "2nd V");
	// An accented capital and Cyrillic are cased as ASCII is, so two labels
	// of one language read alike.
	CHECK(RowCase("D\xC3\x89" "CLENCHEUR V") == "D\xC3\xA9" "clencheur V");
	CHECK(RowCase("V-\xD0\xA2\xD0\xA0\xD0\x98\xD0\x93\xD0\x93\xD0\x95\xD0\xA0") ==
		"V-\xD0\xA2\xD1\x80\xD0\xB8\xD0\xB3\xD0\xB3\xD0\xB5\xD1\x80");
	// Japanese has no case: it passes through as the game wrote it.
	CHECK(RowCase("V\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\xAB") == "V\xE3\x82\xB9\xE3\x82\xAD\xE3\x83\xAB");
}

TEST_CASE("a loaded look differs from the launched one by its costume, its colour or its code, whose case does not count")
{
	const mod::PlayerLook launched{"RYU", 4, 1};
	CHECK_FALSE(mod::LookMismatch(launched, {"RYU", 4, 1}));
	// The setting record's code and the launch's FString compare as the
	// game compares codes.
	CHECK_FALSE(mod::LookMismatch(launched, {"ryu", 4, 1}));
	CHECK(mod::LookMismatch(launched, {"KEN", 4, 1}));
	CHECK(mod::LookMismatch(launched, {"RY", 4, 1}));
	// The load put a costume this copy lacks back to 0, or the colour moved.
	CHECK(mod::LookMismatch(launched, {"RYU", 0, 1}));
	CHECK(mod::LookMismatch(launched, {"RYU", 4, 0}));
}
