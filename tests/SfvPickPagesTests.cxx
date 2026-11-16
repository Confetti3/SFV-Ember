// The beta.8 pages (docs/design/beta8/select.md), walked without a frame
// (SfvPickPages): the board's cards and the look its art pane shows, the look
// page's rows that open pages, where each opening starts, the costume, colour,
// loadout and stage cards and what Select on them stores or refuses, and
// Random, which takes each roll into the player's pick and leaves the dummy
// alone. The rows and previews are SfvSelectionScreenTests.
// The fighters are CatalogFixture's: RYU two of each, CNL two V-Skills and
// one V-Trigger, KEN one of each and not this player's, CMY not installed.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"
#include "PickScreenFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvLoadoutNotes.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickPages.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRandomFighter.hxx"
#include "ember/SfvSelectGrid.hxx"

#include "common/Localization.hxx"
#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;
using namespace sfve::pick_screen_fixture;

namespace {

std::vector<std::string> IdsOf(const std::vector<MenuEntry>& rows)
{
	std::vector<std::string> ids;
	for (const MenuEntry& row : rows) ids.push_back(row.id);
	return ids;
}

using Pages = std::vector<std::pair<std::string, std::string>>;

} // namespace

TEST_CASE("the board has a card for every fighter in the roster's order, then Random and Eleven; one this player cannot play keeps its place")
{
	FillTestCatalog();
	const PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> rows = pickpages::BoardRows(ryu, false, true, false, true);
	CHECK(IdsOf(rows) ==
		std::vector<std::string>{"fighter-0", "fighter-1", "fighter-2", "fighter-3", "fighter-4", "random-fighter", "eleven-fighter"});
	CHECK(RowOf(rows, "fighter-0").enabled);
	CHECK(RowOf(rows, "fighter-0").label == "Ryu");
	CHECK(RowOf(rows, "fighter-1").enabled);
	CHECK(RowOf(rows, "fighter-4").enabled);
	CHECK(RowOf(rows, "fighter-1").hint == sf4e::loc::T("menu.hint.save_fighter"));
	// KEN is not this player's and CMY is not installed: shown, not chosen.
	for (const char* id : {"fighter-2", "fighter-3"}) {
		CAPTURE(id);
		CHECK_FALSE(RowOf(rows, id).enabled);
		CHECK(RowOf(rows, id).detail == sf4e::loc::T("selection.not_owned"));
	}
	CHECK(RowOf(rows, "random-fighter").enabled);
	CHECK(RowOf(rows, "random-fighter").label == sf4e::loc::T("selection.random_card"));
	// ELEVEN's card is named as the game names it, and chosen as Random is.
	CHECK(RowOf(rows, "eleven-fighter").enabled);
	CHECK(RowOf(rows, "eleven-fighter").label == ElevenName());
	CHECK(RowOf(rows, "eleven-fighter").hint == sf4e::loc::T("menu.hint.save_fighter"));
	// The dummy's board has no Random and no Eleven.
	CHECK_FALSE(HasRow(pickpages::BoardRows(ryu, false, true, false, false), "random-fighter"));
	CHECK_FALSE(HasRow(pickpages::BoardRows(ryu, false, true, false, false), "eleven-fighter"));
	// Locked: only the stored fighter, whose look page opens; Random only while it is the pick.
	const std::vector<MenuEntry> locked = pickpages::BoardRows(ryu, false, false, false, true);
	CHECK(RowOf(locked, "fighter-0").enabled);
	CHECK_FALSE(RowOf(locked, "fighter-1").enabled);
	CHECK_FALSE(RowOf(locked, "random-fighter").enabled);
	CHECK(RowOf(pickpages::BoardRows(ryu, true, false, false, true), "random-fighter").enabled);
	// Eleven only while it is the pick, and then Random is not the stored card.
	CHECK_FALSE(RowOf(locked, "eleven-fighter").enabled);
	const std::vector<MenuEntry> elevenLocked = pickpages::BoardRows(ryu, true, false, false, true, true);
	CHECK(RowOf(elevenLocked, "eleven-fighter").enabled);
	CHECK_FALSE(RowOf(elevenLocked, "random-fighter").enabled);
	// A winner who keeps the fighter chooses no other, and not Random.
	const std::vector<MenuEntry> kept = pickpages::BoardRows(ryu, false, true, true, true);
	CHECK(RowOf(kept, "fighter-0").enabled);
	CHECK_FALSE(RowOf(kept, "fighter-1").enabled);
	CHECK(RowOf(kept, "fighter-1").detail == sf4e::loc::T("selection.winner_keeps_fighter"));
	CHECK_FALSE(RowOf(kept, "random-fighter").enabled);
	CHECK_FALSE(RowOf(kept, "eleven-fighter").enabled);
}

TEST_CASE("the board's art pane shows the look choosing the fighter stores: their remembered look on the player's screen")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	ForgetDummyForTest();
	// RYU last stored in Nostalgia, colour 02, both II; then CNL.
	PlayerPick ryu = PlayerPickOf(kRyu, kTwoTwo);
	ryu.pick.costume = 4;
	ryu.pick.color = 1;
	ryu = SetPick(ryu);
	PlayerPick cnl = PlayerPickOf(kCnl, kTwoOne);
	cnl.pick.color = 1;
	cnl = SetPick(cnl);
	REQUIRE(RememberedLook(kRyu).has_value());
	CHECK(SameLook(*RememberedLook(kRyu), ryu.pick));
	// The player's screen: RYU as last stored.
	PlayerPick shown = pickpages::BoardPick(cnl, "fighter-0", Whose::Player);
	CHECK(SameLook(shown.pick, ryu.pick));
	CHECK(SameLook(LookFor(cnl.pick, kRyu, true), ryu.pick));
	// The dummy's screen has a memory of its own: RYU, which the dummy never
	// had, is in its default look, not CNL's (nor the player's RYU).
	shown = pickpages::BoardPick(cnl, "fighter-0", Whose::Dummy);
	CHECK(shown.pick.fighter == kRyu);
	CHECK(shown.pick.costume == 0);
	CHECK(shown.pick.color == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	// Once the dummy has had RYU in another look, that look is back on its
	// screen, and the player's RYU is still as the player last stored it.
	PlayerPick dummyRyu = PlayerPickOf(kRyu, kTwoOne);
	dummyRyu.pick.color = 1;
	SetDummy(dummyRyu);
	shown = pickpages::BoardPick(cnl, "fighter-0", Whose::Dummy);
	CHECK(SameLook(shown.pick, dummyRyu.pick));
	CHECK(SameLook(pickpages::BoardPick(cnl, "fighter-0", Whose::Player).pick, ryu.pick));
	// A fighter this player cannot play, as the spec fits it.
	shown = pickpages::BoardPick(cnl, "fighter-2", Whose::Dummy);
	CHECK(shown.pick.fighter == kKen);
	CHECK(shown.pick.costume == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	// The stored fighter, Random's card and any other id: the pick itself.
	for (const char* id : {"fighter-1", "random-fighter", "eleven-fighter", "stage-2", "fighter-99"}) {
		CAPTURE(id);
		CHECK(SameLook(pickpages::BoardPick(cnl, id, Whose::Player).pick, cnl.pick));
	}
	// The stage is never part of a fighter's look.
	cnl.stage = 2;
	CHECK(pickpages::BoardPick(cnl, "fighter-0", Whose::Player).stage == 2);
	ForgetDummyForTest();
	ForgetPickMemoryForTest();
}

TEST_CASE("the look page's rows open the board and the cards, and the costume code its choices")
{
	FillTestCatalog();
	using sf4e::ui::MenuSelectOpens;
	using sf4e::ui::SelectOpens;
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	const std::vector<MenuEntry> rows = pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, true, true, stages);
	for (const char* id : {"fighter", "costume", "color", "vskill", "vtrigger", "stage"}) {
		CAPTURE(id);
		const MenuEntry& row = RowOf(rows, id);
		CHECK(row.choices.empty());
		CHECK(row.opens);
		CHECK(row.adjustable);
		CHECK(MenuSelectOpens(row) == SelectOpens::Activate);
	}
	CHECK(MenuSelectOpens(RowOf(rows, "costume-code")) == SelectOpens::Choice);
	CHECK(MenuSelectOpens(RowOf(rows, "done")) == SelectOpens::Activate);
	// KEN's single V-Skill and V-Trigger still open their cards, to read
	// them, but do not step.
	const std::vector<MenuEntry> ken = pickpages::LookRows(PlayerPickOf(kKen, kOneOne), false, true, true, true, stages);
	for (const char* id : {"vskill", "vtrigger"}) {
		CAPTURE(id);
		CHECK(RowOf(ken, id).enabled);
		CHECK_FALSE(RowOf(ken, id).adjustable);
		CHECK(MenuSelectOpens(RowOf(ken, id)) == SelectOpens::Activate);
	}
	// P2: the stage row opens the stage cards, which say who chooses, and
	// does not step.
	const std::vector<MenuEntry> p2 = pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, true, false, stages);
	CHECK(RowOf(p2, "stage").enabled);
	CHECK_FALSE(RowOf(p2, "stage").adjustable);
	CHECK(RowOf(p2, "stage").detail == sf4e::loc::T("selection.only_p1_stage"));
	// Locked: the look's rows do nothing; the loadout and the stage open to read.
	const std::vector<MenuEntry> locked = pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), false, false, true, true, stages);
	CHECK(MenuSelectOpens(RowOf(locked, "costume")) == SelectOpens::Nothing);
	CHECK(MenuSelectOpens(RowOf(locked, "vskill")) == SelectOpens::Activate);
	CHECK_FALSE(RowOf(locked, "vskill").adjustable);
	// Random: the fighter row names the roll as Random's.
	const std::vector<MenuEntry> eleven =
		pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), true, true, true, true, stages, false, true);
	CHECK(RowOf(eleven, "fighter").value == ElevenTitle("Ryu"));
	const std::vector<MenuEntry> random = pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), true, true, true, true, stages);
	CHECK(RowOf(random, "fighter").value == sf4e::loc::Tf("selection.random_fighter", "Ryu"));
	// The dummy's: no stage row.
	CHECK_FALSE(HasRow(pickpages::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, false, false, {}), "stage"));
}

TEST_CASE("each way the shell opens the screen starts on its page, and the room's one changes close it again")
{
	FillTestCatalog();
	using sf4e::ui::SelectionStart;
	PlayerPick pick = PlayerPickOf(kRyu, kTwoOne);
	pick.pick.costume = 4;
	pick.stage = 2;
	const auto opening = [&pick](SelectionStart start, bool random = false, bool dummy = false, bool eleven = false) {
		return pickpages::OpeningFor(start, pick, random, dummy, eleven);
	};
	CHECK(opening(SelectionStart::Board).pages == Pages{{"home", "fighter-0"}});
	CHECK_FALSE(opening(SelectionStart::Board).closesOnChoice);
	CHECK(opening(SelectionStart::Roster).pages == Pages{{"home", "fighter-0"}});
	CHECK(opening(SelectionStart::Roster).closesOnChoice);
	// Random's card is the stored one, on the player's screen only.
	CHECK(opening(SelectionStart::Roster, true).pages == Pages{{"home", "random-fighter"}});
	CHECK(opening(SelectionStart::Board, true, true).pages == Pages{{"home", "fighter-0"}});
	// Eleven's card is, while Eleven is the pick (Random's flag is set with it).
	CHECK(opening(SelectionStart::Roster, true, false, true).pages == Pages{{"home", "eleven-fighter"}});
	CHECK(opening(SelectionStart::Board, true, true, true).pages == Pages{{"home", "fighter-0"}});
	CHECK(opening(SelectionStart::Costumes).pages == Pages{{"home", "fighter-0"}, {"look", "costume"}, {"costumes", "costume-4"}});
	CHECK(opening(SelectionStart::Costumes).closesOnChoice);
	CHECK(opening(SelectionStart::Ultras).pages == Pages{{"home", "fighter-0"}, {"look", "vskill"}, {"loadout", "vskill-1"}});
	CHECK_FALSE(opening(SelectionStart::Ultras).closesOnChoice);
	CHECK(opening(SelectionStart::LookUltra).pages == Pages{{"home", "fighter-0"}, {"look", "vskill"}});
	CHECK_FALSE(opening(SelectionStart::LookUltra).closesOnChoice);
	CHECK(opening(SelectionStart::Options).pages == Pages{{"home", "fighter-0"}, {"look", "costume"}});
	CHECK(opening(SelectionStart::Stage).pages == Pages{{"home", "fighter-0"}, {"look", "stage"}, {"stage", "stage-2"}});
	CHECK(opening(SelectionStart::Stage).closesOnChoice);
	// The dummy's screen has no stage page.
	CHECK(opening(SelectionStart::Stage, false, true).pages == Pages{{"home", "fighter-0"}});
}

TEST_CASE("the costume and colour cards list every look, and one this player cannot ready cannot be chosen")
{
	FillTestCatalog();
	PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> costumes = pickpages::CostumeCards(ryu, true);
	CHECK(IdsOf(costumes) == std::vector<std::string>{"costume-0", "costume-1", "costume-4"});
	CHECK(RowOf(costumes, "costume-0").enabled);
	CHECK(RowOf(costumes, "costume-0").label == CostumeName(kRyu, 0));
	CHECK(RowOf(costumes, "costume-4").enabled);
	// RYU's costume 1: none of its colours is theirs.
	CHECK_FALSE(RowOf(costumes, "costume-1").enabled);
	CHECK(RowOf(costumes, "costume-1").detail == sf4e::loc::T("selection.not_owned"));
	const std::vector<MenuEntry> colours = pickpages::ColourCards(ryu, 4, true);
	CHECK(IdsOf(colours) == std::vector<std::string>{"color-0", "color-1", "color-100", "color-101"});
	CHECK(RowOf(colours, "color-1").enabled);
	CHECK(RowOf(colours, "color-1").label == ColourName(1));
	CHECK(RowOf(colours, "color-1").hint == sf4e::loc::T("menu.hint.save_color"));
	CHECK(RowOf(colours, "color-100").label == ColourName(100));
	CHECK_FALSE(RowOf(colours, "color-100").enabled);
	CHECK(RowOf(colours, "color-100").detail == sf4e::loc::T("selection.not_owned"));
	// Locked: nothing can be chosen.
	for (const MenuEntry& row : pickpages::CostumeCards(ryu, false)) CHECK_FALSE(row.enabled);
	for (const MenuEntry& row : pickpages::ColourCards(ryu, 0, false)) CHECK_FALSE(row.enabled);
	// A costume the fighter does not have: the one row that says so.
	const std::vector<MenuEntry> none = pickpages::ColourCards(ryu, 99, true);
	REQUIRE(none.size() == 1u);
	CHECK_FALSE(none[0].enabled);
	CHECK(none[0].label == sf4e::loc::T("selection.no_colors"));
}

TEST_CASE("the loadout cards are each choice the fighter has, with its note, and Select stores that choice")
{
	FillTestCatalog();
	PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> cards = pickpages::LoadoutCards(ryu, true);
	CHECK(IdsOf(cards) == std::vector<std::string>{"vskill-0", "vskill-1", "vtrigger-0", "vtrigger-1"});
	for (size_t i = 0; i < cards.size(); i++) {
		CAPTURE(cards[i].id);
		CHECK(cards[i].key == i); // the picture's number, part * 2 + choice
		CHECK(cards[i].enabled);
	}
	CHECK(RowOf(cards, "vskill-1").label == ChoiceText(1, "Skill Two"));
	CHECK(RowOf(cards, "vtrigger-0").detail == LoadoutNote(kRyu, picks::LoadoutPart::VTrigger, 0));
	CHECK(IdsOf(pickpages::LoadoutCards(PlayerPickOf(kCnl, kOneOne), true)) ==
		std::vector<std::string>{"vskill-0", "vskill-1", "vtrigger-0"});
	for (const MenuEntry& card : pickpages::LoadoutCards(ryu, false)) CHECK_FALSE(card.enabled);
	// Select stores the choice, and only a choice the fighter has.
	CHECK(pickpages::ChooseLoadout("vtrigger-1", ryu));
	CHECK(picks::LoadoutOf(ryu.pick) == kOneTwo);
	CHECK_FALSE(pickpages::ChooseLoadout("vtrigger-1", ryu));
	CHECK(pickpages::ChooseLoadout("vskill-1", ryu));
	CHECK(picks::LoadoutOf(ryu.pick) == kTwoTwo);
	PlayerPick cnl = PlayerPickOf(kCnl, kOneOne);
	CHECK_FALSE(pickpages::ChooseLoadout("vtrigger-1", cnl));
	CHECK_FALSE(pickpages::ChooseLoadout("vskill-2", cnl));
	CHECK_FALSE(pickpages::ChooseLoadout("vskill", cnl));
	CHECK(picks::LoadoutOf(cnl.pick) == kOneOne);
}

TEST_CASE("a stage card stores the stage; P2, a watcher and a stage the opponent lacks or this player is not offered are refused")
{
	FillTestCatalog();
	using Answer = pickpages::StageAnswer;
	// At a table whose opponent lacks S01 (id 0); this copy lacks S26, and
	// the account's cache lacks S02 (id 1).
	const std::vector<StageOption> stages = StageOptions({0, kUninstalledStage});
	PlayerPick pick = PlayerPickOf(kRyu, kOneOne);
	pick.stage = picks::kRandomStage;
	const std::vector<MenuEntry> cards = pickpages::StageCards(pick, true, true, stages);
	CHECK(IdsOf(cards) == std::vector<std::string>{"stage-255", "stage-0", "stage-2"});
	CHECK(RowOf(cards, "stage-255").enabled);
	CHECK(RowOf(cards, "stage-255").detail == sf4e::loc::T("selection.random_stage.detail"));
	CHECK(RowOf(cards, "stage-2").enabled);
	CHECK(RowOf(cards, "stage-2").hint == sf4e::loc::T("menu.hint.save_stage"));
	CHECK_FALSE(RowOf(cards, "stage-0").enabled);
	CHECK(RowOf(cards, "stage-0").detail == sf4e::loc::T("runtime.ready.stage_not_shared"));
	// Select on a card stores its stage.
	CHECK(pickpages::ChooseStage("stage-2", pick, true, true, stages) == Answer::Stored);
	CHECK(pick.stage == 2);
	CHECK(pickpages::ChooseStage("stage-2", pick, true, true, stages) == Answer::Unchanged);
	// The opponent lacks S01; S02 and S26 are not this player's to offer.
	CHECK(pickpages::ChooseStage("stage-0", pick, true, true, stages) == Answer::NotShared);
	CHECK(pickpages::ChooseStage("stage-1", pick, true, true, stages) == Answer::NotOffered);
	CHECK(pickpages::ChooseStage("stage-3", pick, true, true, stages) == Answer::NotOffered);
	CHECK(pickpages::ChooseStage("stage-x", pick, true, true, stages) == Answer::NotOffered);
	CHECK(pick.stage == 2);
	CHECK(pickpages::ChooseStage("stage-255", pick, true, true, stages) == Answer::Stored);
	CHECK(pick.stage == picks::kRandomStage);
	// P2: every card shown, none chosen, each saying only P1 chooses.
	const std::vector<MenuEntry> p2 = pickpages::StageCards(pick, true, false, stages);
	CHECK(IdsOf(p2) == IdsOf(cards));
	CHECK_FALSE(RowOf(p2, "stage-2").enabled);
	CHECK(RowOf(p2, "stage-2").detail == sf4e::loc::T("selection.only_p1_stage"));
	CHECK(pickpages::ChooseStage("stage-2", pick, true, false, stages) == Answer::OnlyP1);
	// A watcher's (or anyone's locked) screen.
	CHECK(pickpages::ChooseStage("stage-2", pick, false, false, stages) == Answer::Locked);
	CHECK(pick.stage == picks::kRandomStage);
}

TEST_CASE("a stage the room bans is listed tagged and dimmed with the reason, never chosen, and Random stays choosable")
{
	FillTestCatalog();
	using Answer = pickpages::StageAnswer;
	const std::vector<StageOption> stages = StageOptions({}, {2});
	PlayerPick pick = PlayerPickOf(kRyu, kOneOne);
	pick.stage = 0;
	const std::vector<MenuEntry> cards = pickpages::StageCards(pick, true, true, stages);
	CHECK(IdsOf(cards) == std::vector<std::string>{"stage-255", "stage-0", "stage-2"});
	// The banned card's caption carries the tag; the others are as before.
	CHECK(RowOf(cards, "stage-2").label == std::string("Stage Twenty-five (") + sf4e::loc::T("room.banned_stages.tag") + ")");
	CHECK(RowOf(cards, "stage-0").label == "Stage One");
	CHECK_FALSE(RowOf(cards, "stage-2").enabled);
	CHECK(RowOf(cards, "stage-2").detail == sf4e::loc::T("selection.stage_banned"));
	CHECK(RowOf(cards, "stage-2").hint.empty());
	CHECK(RowOf(cards, "stage-0").enabled);
	CHECK(RowOf(cards, "stage-255").enabled);
	// Select on it stores nothing and says why; Random and a stage allowed are chosen.
	CHECK(pickpages::ChooseStage("stage-2", pick, true, true, stages) == Answer::Banned);
	CHECK(pick.stage == 0);
	CHECK(pickpages::ChooseStage("stage-255", pick, true, true, stages) == Answer::Stored);
	CHECK(pick.stage == picks::kRandomStage);
	// P2 and a watcher are told only P1 chooses (the ban is not theirs to hear about).
	CHECK(pickpages::ChooseStage("stage-2", pick, true, false, stages) == Answer::OnlyP1);
	CHECK(pickpages::ChooseStage("stage-2", pick, false, false, stages) == Answer::Locked);
	// A stored stage the room bans afterwards stays as stored: P1's Ready sends the Training stage for it.
	pick.stage = 2;
	CHECK(pickpages::ChooseStage("stage-0", pick, true, true, stages) == Answer::Stored);
}

TEST_CASE("Random takes each roll once into the player's pick, in the fighter's remembered look, and leaves the dummy alone")
{
	FillTestCatalog();
	// RYU last stored in Nostalgia, colour 02; the pick is now CNL; the
	// dummy is BSN.
	PlayerPick ryu = PlayerPickOf(kRyu, kTwoTwo);
	ryu.pick.costume = 4;
	ryu.pick.color = 1;
	ryu = SetPick(ryu);
	SetPick(PlayerPickOf(kCnl, kOneOne));
	const PlayerPick dummy = SetDummy(PlayerPickOf(kBsn, kOneOne));
	sf4e::NetplayFacade::RuntimeSnapshot snapshot{};
	snapshot.randomFighter = kRyu;
	snapshot.randomRoll = 0xB8000001;
	// Not Random: nothing taken.
	SetPickRandom(false);
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.fighter == kCnl);
	// Random: the roll, once, in RYU's remembered look.
	SetPickRandom(true);
	REQUIRE(PickRandom());
	FollowRandomFighter(snapshot);
	CHECK(SameLook(CurrentPick().pick, ryu.pick));
	PlayerPick edited = CurrentPick();
	edited.pick.color = 0;
	SetPick(edited);
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.color == 0);
	// The next roll, to CNL: taken; the dummy is untouched throughout.
	snapshot.randomFighter = kCnl;
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.fighter == kCnl);
	CHECK(SameLook(CurrentDummy().pick, dummy.pick));
	// A roll to no fighter of the catalog is never taken.
	snapshot.randomFighter = 99;
	snapshot.randomRoll++;
	FollowRandomFighter(snapshot);
	CHECK(CurrentPick().pick.fighter == kCnl);
	SetPickRandom(false);
	CHECK_FALSE(PickRandom());
	// The dummy's screen has no Random card.
	CHECK_FALSE(HasRow(pickpages::BoardRows(CurrentDummy(), false, true, false, false), "random-fighter"));
	// No dummy picked, as the other tests expect to find it.
	ForgetDummyForTest();
}

TEST_CASE("the board places the shipped table's tiles where the game's own select screen draws them")
{
	const grid::SelectGrid& table = grid::EmbeddedSelectGrid();
	REQUIRE(table.columns > 0);
	// Tile centres measured on the game's 1080-line select screen, in pixels,
	// and its 70 px pitch: the first row's first tile at 167, RYU 867, KEN
	// 1053, Akira 1753; the second row's Luke 129, Nash 899, M. Bison 1020.
	const auto place = [&table](const char* code) {
		const std::optional<grid::Cell> cell = grid::CellOf(table, code);
		REQUIRE(cell.has_value());
		return 167 + 70 * pickpages::BoardColumn(table, *cell);
	};
	CHECK(std::abs(place("Z39") - 167.f) <= .5f);
	const std::vector<std::pair<const char*, float>> measured{
		{"RYU", 867.f}, {"KEN", 1053.f}, {"Z41", 1753.f}, {"Z42", 129.f}, {"NSH", 899.f}, {"VEG", 1020.f}};
	for (const std::pair<const char*, float>& expected : measured) {
		CAPTURE(expected.first);
		CHECK(std::abs(place(expected.first) - expected.second) <= 4.f);
	}
}

TEST_CASE("the pad crosses the Random tile as the game's cursor does: Left and Right from the tiles beside it, both rows")
{
	FillTestCatalog();
	// Random in the first row's middle, a tile in its column on the second
	// row (as the game's second row has one more on the left): RYU | Random |
	// KEN over CNL CMY | BSN.
	grid::SelectGrid table;
	std::string error;
	REQUIRE(grid::ParseSelectGrid(R"({"columns": 3, "rowOffsets": [0, -0.54],
		"fighters": [{"code": "RYU", "column": 0, "row": 0}, {"code": "KEN", "column": 2, "row": 0},
			{"code": "CNL", "column": 0, "row": 1}, {"code": "CMY", "column": 1, "row": 1}, {"code": "BSN", "column": 2, "row": 1}],
		"random": {"column": 1, "row": 0}})",
		table, error));
	std::vector<MenuEntry> rows = pickpages::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, true);
	pickpages::LinkRandom(rows, table);
	CHECK(RowOf(rows, "random-fighter").left == "fighter-0");
	CHECK(RowOf(rows, "random-fighter").right == "fighter-2");
	CHECK(RowOf(rows, "fighter-0").right == "random-fighter");
	CHECK(RowOf(rows, "fighter-2").left == "random-fighter");
	CHECK(RowOf(rows, "fighter-3").right == "random-fighter"); // CMY, at Random's foot on the left
	CHECK(RowOf(rows, "fighter-4").left == "random-fighter");  // BSN, at its foot on the right
	CHECK(RowOf(rows, "fighter-1").left.empty());
	CHECK(RowOf(rows, "fighter-1").right.empty());
	CHECK(RowOf(rows, "fighter-0").up.empty());
	CHECK(RowOf(rows, "fighter-0").down.empty());
	// The pad: Left from KEN lands on Random, then on RYU; Right from RYU
	// on Random, then on KEN; Right from CMY and Left from BSN on Random.
	sf4e::ui::MenuNavigation nav;
	double now = 0;
	const auto press = [&](unsigned button) {
		nav.Update({0, now += 1}, rows);
		nav.Update({button, now += 1}, rows);
		nav.Update({0, now += 1}, rows);
		return nav.Focus();
	};
	using Button = sf4e::ui::MenuInput;
	nav.Focus("fighter-2", rows);
	CHECK(press(Button::Left) == "random-fighter");
	CHECK(press(Button::Left) == "fighter-0");
	CHECK(press(Button::Right) == "random-fighter");
	CHECK(press(Button::Right) == "fighter-2");
	nav.Focus("fighter-3", rows);
	CHECK(press(Button::Right) == "random-fighter");
	nav.Focus("fighter-4", rows);
	CHECK(press(Button::Left) == "random-fighter");
	// The dummy's board has no Random card: nothing points at one.
	std::vector<MenuEntry> dummy = pickpages::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, false);
	pickpages::LinkRandom(dummy, table);
	for (const MenuEntry& row : dummy) {
		CAPTURE(row.id);
		CHECK(row.left.empty());
		CHECK(row.right.empty());
	}
}

TEST_CASE("a dimmed card is reached by the pad like any other, so the pane says why, and Select on it does nothing")
{
	FillTestCatalog();
	// RYU's Nostalgia: colours 01 and 02 are this player's, EX01 and EX02 not.
	const std::vector<MenuEntry> rows = pickpages::ColourCards(PlayerPickOf(kRyu, kOneOne), 4, true);
	REQUIRE_FALSE(RowOf(rows, "color-100").enabled);
	sf4e::ui::MenuNavigation nav;
	double now = 0;
	const auto press = [&](unsigned button) {
		sf4e::ui::MenuAction last;
		nav.Update({0, now += 1}, rows);
		last = nav.Update({button, now += 1}, rows);
		nav.Update({0, now += 1}, rows);
		return last;
	};
	using Button = sf4e::ui::MenuInput;
	nav.Focus("color-1", rows);
	press(Button::Down);
	CHECK(nav.Focus() == "color-100");
	press(Button::Down);
	CHECK(nav.Focus() == "color-101");
	// Select on it: no action, the focus stays.
	CHECK(press(Button::Select).kind == MenuAction::None);
	CHECK(nav.Focus() == "color-101");
	// Back up to the colours this player can ready, and Select stores one.
	press(Button::Up);
	press(Button::Up);
	CHECK(nav.Focus() == "color-1");
	const MenuAction chosen = press(Button::Select);
	CHECK(chosen.kind == MenuAction::Activate);
	CHECK(chosen.id == "color-1");
	// The same on the board: KEN's tile (not this player's) is focused, and
	// Select on it does nothing.
	const std::vector<MenuEntry> board = pickpages::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, true);
	sf4e::ui::MenuNavigation boardNav;
	boardNav.Focus("fighter-2", board);
	CHECK(boardNav.Focus() == "fighter-2");
	boardNav.Update({0, now += 1}, board);
	CHECK(boardNav.Update({Button::Select, now += 1}, board).kind == MenuAction::None);
}

TEST_CASE("a look whose own colour has no picture shows its costume's picture, tagged with the look's colour")
{
	FillTestCatalog();
	// The package holds each costume in its first colour: that is the colour
	// a costume's picture is of.
	CHECK(PictureColour(kRyu, 4) == 0);
	CHECK(PictureColour(kCnl, 0) == 0);
	CHECK(PictureColour(kRyu, 99) == -1);
	// The look's own picture: shown as it is, untagged (loading counts: the
	// pane shows its frame until it is known).
	CHECK(ChooseLookPicture(true, false, false) == LookPictureChoice{LookSource::Own, false});
	CHECK(ChooseLookPicture(true, true, true) == LookPictureChoice{LookSource::Own, false});
	// Colour 05 of a costume pictured in colour 01: the costume's picture,
	// tagged with colour 05, never passed off as it.
	CHECK(ChooseLookPicture(false, true, false) == LookPictureChoice{LookSource::Costume, true});
	// The costume's first colour itself: its picture is that colour.
	CHECK(ChooseLookPicture(false, true, true) == LookPictureChoice{LookSource::Costume, false});
	// Neither in the package: the game's own figure of the fighter, tagged.
	CHECK(ChooseLookPicture(false, false, false) == LookPictureChoice{LookSource::Fighter, true});
	CHECK(ChooseLookPicture(false, false, true) == LookPictureChoice{LookSource::Fighter, true});
	// The colour cards are the costume's colours, every one, named as the
	// game's costume menu names them; Select stores the colour as before.
	PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> colours = pickpages::ColourCards(ryu, 4, true);
	CHECK(IdsOf(colours) == std::vector<std::string>{"color-0", "color-1", "color-100", "color-101"});
	CHECK(RowOf(colours, "color-101").label == "EX02");
}

TEST_CASE("a costume the game has no words for in this language is named by the shared word and its number, never its text id")
{
	const std::string costume = sf4e::loc::T("selection.costume");
	CHECK(ShownCostumeName("Story", 0) == "Story");
	CHECK(ShownCostumeName("Battle Costume", 3) == "Battle Costume");
	CHECK(ShownCostumeName("ID_SYS_BSet_0224", 24) == costume + " 24");
	CHECK(ShownCostumeName("", 7) == costume + " 7");
	// A name that only starts like an id is a name.
	CHECK(ShownCostumeName("ID_ Story", 1) == "ID_ Story");
	// Every place a costume is named reads it so: the costume cards among them.
	FillTestCatalog();
	CHECK(RowOf(pickpages::CostumeCards(PlayerPickOf(kRyu, kOneOne), true), "costume-1").label == costume + " 1");
	CHECK(CostumeName(kRyu, 1) == costume + " 1");
}

TEST_CASE("the costume cards and the costume row never offer the costume the game ships nothing for")
{
	FillTestCatalog();
	// RYU's costume 15 (marked unshipped by the shipped pick space).
	const PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	CHECK_FALSE(HasRow(pickpages::CostumeCards(ryu, true), "costume-15"));
	const std::vector<MenuEntry> rows = pickpages::Rows(ryu, true, true, true, StageOptions(MissingStages()));
	const MenuEntry& costume = RowOf(rows, "costume");
	CHECK(std::none_of(costume.choices.begin(), costume.choices.end(), [](const sf4e::ui::MenuChoice& choice) { return choice.id == "#15"; }));
	// Neither stepping nor choosing reaches it, and a preview of it is fitted off it.
	PlayerPick pick = ryu;
	pick.pick.costume = 4;
	CHECK_FALSE(pickpages::Apply(Adjust("costume", 1), pick, StageOptions(MissingStages())));
	CHECK(pick.pick.costume == 4);
	CHECK_FALSE(pickpages::Apply(Choose("costume", "#15"), pick, StageOptions(MissingStages())));
	CHECK(pickpages::Previewed(ryu, "costume", "#15").pick.costume == 0);
	CHECK(pickpages::BoardPick(PlayerPickOf(kCnl, kOneOne), "fighter-0", Whose::Dummy).pick.costume != 15);
}

TEST_CASE("choosing a fighter is the same by the board, the look page's Fighter row, a Random roll and the pipe: the fighter in its own look")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	ForgetDummyForTest();
	SetPickRandom(false);
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	// RYU last stored in Nostalgia, colour 02, both II; the pick is CNL in colour 02.
	PlayerPick ryu = PlayerPickOf(kRyu, kTwoTwo);
	ryu.pick.costume = 4;
	ryu.pick.color = 1;
	ryu = SetPick(ryu);
	PlayerPick cnl = PlayerPickOf(kCnl, kTwoOne);
	cnl.pick.color = 1;
	cnl = SetPick(cnl);
	// The look page's Fighter row, stepped back to RYU or chosen: RYU as last stored.
	PlayerPick shown = cnl;
	REQUIRE(pickpages::Apply(Adjust("fighter", -1), shown, stages));
	CHECK(SameLook(shown.pick, ryu.pick));
	shown = cnl;
	REQUIRE(pickpages::Apply(Choose("fighter", "#0"), shown, stages));
	CHECK(SameLook(shown.pick, ryu.pick));
	// BSN, never stored: its default look, not CNL's colour 02 or V-Skill II.
	shown = cnl;
	REQUIRE(pickpages::Apply(Choose("fighter", "#4"), shown, stages));
	CHECK(shown.pick.fighter == kBsn);
	CHECK(shown.pick.costume == 0);
	CHECK(shown.pick.color == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	// The dummy's page has a memory of its own: RYU, never the dummy's, is in
	// its default look, and the look the dummy last had once it has had one.
	shown = cnl;
	REQUIRE(pickpages::Apply(Choose("fighter", "#0"), shown, stages, Whose::Dummy));
	CHECK(shown.pick.color == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	PlayerPick dummyRyu = PlayerPickOf(kRyu, kTwoOne);
	dummyRyu.pick.color = 1;
	SetDummy(dummyRyu);
	shown = cnl;
	REQUIRE(pickpages::Apply(Choose("fighter", "#0"), shown, stages, Whose::Dummy));
	CHECK(SameLook(shown.pick, dummyRyu.pick));
	// Every way to choose a fighter gives the look the page shows, from CNL.
	std::uint64_t roll = 0xD0000001;
	const auto viaBoard = [&](int fighter) { return pickpages::BoardPick(cnl, "fighter-" + std::to_string(fighter), Whose::Player).pick; };
	const auto viaChoose = [&](int fighter) { return ChooseFighter(fighter).pick; }; // the page's row and the board's card
	const auto viaPipe = [&](int fighter) {
		return ChooseFighter(fighter, [](PlayerPick& edited) { edited.stage = 2; }).pick; // `pick CODE S02`
	};
	const auto viaRandom = [&](int fighter) {
		SetPickRandom(true);
		sf4e::NetplayFacade::RuntimeSnapshot snapshot{};
		snapshot.randomFighter = fighter;
		snapshot.randomRoll = ++roll;
		FollowRandomFighter(snapshot);
		const sf4e::selection::Pick rolled = CurrentPick().pick;
		SetPickRandom(false);
		return rolled;
	};
	for (const int fighter : {kRyu, kBsn}) {
		CAPTURE(fighter);
		const sf4e::selection::Pick expected = fighter == kRyu ? ryu.pick : PickOf(kBsn);
		const std::function<sf4e::selection::Pick(int)> vias[] = {viaBoard, viaChoose, viaPipe, viaRandom};
		for (const auto& via : vias) {
			SetPick(cnl);
			CHECK(SameLook(via(fighter), expected));
		}
	}
	ForgetPickMemoryForTest();
	ForgetDummyForTest();
}

TEST_CASE("choosing a fighter by the look page or the pipe turns Random and Eleven off, as the board does")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	PlayerPick ryu = PlayerPickOf(kRyu, kTwoTwo);
	ryu.pick.color = 1;
	SetPick(ryu);
	SetPick(PlayerPickOf(kCnl, kOneOne)); // the fighter chosen last: CNL
	for (const bool eleven : {false, true}) {
		CAPTURE(eleven);
		// Random rolled BSN.
		if (eleven) SetPickEleven(true);
		else SetPickRandom(true);
		SetPick(PlayerPickOf(kBsn, kOneOne));
		REQUIRE(PickRandom());
		const PlayerPick chosen = ChooseFighter(kRyu);
		CHECK_FALSE(PickRandom());
		CHECK_FALSE(PickEleven());
		CHECK(SameLook(chosen.pick, ryu.pick));
		// It is the fighter the player chose, so it is the one saved.
		std::uint64_t revision = 0;
		CHECK(PickMemoryNow(revision).ChosenFighter() == kRyu);
		SetPick(PlayerPickOf(kCnl, kOneOne));
		CHECK(PickMemoryNow(revision).ChosenFighter() == kCnl);
	}
	// Not an edit it makes of a fighter already chosen: Random stays off and the look stays.
	CHECK(SameLook(ChooseFighter(kRyu).pick, ryu.pick));
	CHECK_FALSE(PickRandom());
	ForgetPickMemoryForTest();
}

TEST_CASE("an edit of the stored pick is one step: it reads the pick as it is, may keep it, and may call the pick's own functions")
{
	FillTestCatalog();
	ForgetPickMemoryForTest();
	SetPickRandom(false);
	PlayerPick ryu = PlayerPickOf(kRyu, kTwoTwo);
	ryu.pick.color = 1;
	SetPick(ryu);
	// Kept: nothing stored, nothing changes.
	std::uint64_t before = 0, after = 0;
	PickMemoryNow(before);
	CHECK(SameLook(UpdatePick([](PlayerPick&) { return false; }).pick, ryu.pick));
	PickMemoryNow(after);
	CHECK(after == before);
	// Stored: the edit sees the stored pick, and may read the memory and the looks.
	const PlayerPick edited = UpdatePick([&](PlayerPick& pick) {
		CHECK(SameLook(pick.pick, ryu.pick));
		CHECK(RememberedLook(kRyu).has_value());
		pick.pick = LookFor(pick.pick, kBsn, true);
		return true;
	});
	CHECK(edited.pick.fighter == kBsn);
	CHECK(SameLook(CurrentPick().pick, edited.pick));
	ForgetPickMemoryForTest();
}
