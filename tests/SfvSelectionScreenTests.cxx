// The two ways Ember's shell changes this player's V-Skill and V-Trigger
// (EMBER_LIBRARY.md section 40), walked without a frame: SFV's pick screen,
// a row for each part (SfvSelectionScreen's Rows and Apply, what its Draw
// shows and does), and the table page's and post-match prompt's one Ultra
// value (StepTableSelection), which steps only the loadouts the fighter has.
// The fighters are CatalogFixture's: RYU two of each, CNL two V-Skills and
// one V-Trigger, KEN one of each.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelectionScreen.hxx"

#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <algorithm>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;

namespace {

constexpr int kRyu = 0, kCnl = 1, kKen = 2;

PlayerPick PlayerPickOf(int fighter, picks::Loadout loadout)
{
	PlayerPick pick;
	pick.pick = PickOf(fighter, 0, 0, loadout);
	return pick;
}

const MenuEntry& RowOf(const std::vector<MenuEntry>& rows, const char* id)
{
	const auto row = std::find_if(rows.begin(), rows.end(), [id](const MenuEntry& entry) { return entry.id == id; });
	REQUIRE(row != rows.end());
	return *row;
}

bool HasRow(const std::vector<MenuEntry>& rows, const char* id)
{
	return std::any_of(rows.begin(), rows.end(), [id](const MenuEntry& entry) { return entry.id == id; });
}

std::vector<MenuEntry> PlayerRows(const PlayerPick& pick)
{
	return SfvSelectionScreen::Rows(pick, true, true, true, StageChoices(MissingStages()));
}

MenuAction Adjust(const char* row, int delta)
{
	MenuAction action;
	action.kind = MenuAction::Adjust;
	action.id = row;
	action.delta = delta;
	return action;
}

MenuAction Choose(const char* row, const char* option)
{
	MenuAction action;
	action.kind = MenuAction::Chosen;
	action.id = row;
	action.text = option;
	return action;
}

sf4e::ui::ShellAction::SelectionStep Step(sf4e::ui::ShellAction::SelectionStep::Field field, int delta)
{
	sf4e::ui::ShellAction::SelectionStep step;
	step.field = field;
	step.delta = delta;
	return step;
}

} // namespace

TEST_CASE("the pick screen has a V-Skill and a V-Trigger row, and a part the fighter has one of cannot change")
{
	FillTestCatalog();
	// RYU: both rows step, each offering I and II.
	const std::vector<MenuEntry> ryu = PlayerRows(PlayerPickOf(kRyu, kTwoOne));
	const MenuEntry& ryuSkill = RowOf(ryu, "vskill");
	CHECK(ryuSkill.enabled);
	CHECK(ryuSkill.value == "II");
	CHECK(ryuSkill.chosen == "#1");
	CHECK(ryuSkill.choices.size() == 2u);
	CHECK(ryuSkill.choices[1].detail == "II Skill Two"); // the game's pattern is English's until it is read
	const MenuEntry& ryuTrigger = RowOf(ryu, "vtrigger");
	CHECK(ryuTrigger.enabled);
	CHECK(ryuTrigger.value == "I");
	CHECK(ryuTrigger.choices.size() == 2u);
	// CNL: two V-Skills and one V-Trigger, so only the V-Trigger row is shown
	// unable to change, with its one choice.
	const std::vector<MenuEntry> cnl = PlayerRows(PlayerPickOf(kCnl, kTwoOne));
	CHECK(RowOf(cnl, "vskill").enabled);
	CHECK_FALSE(RowOf(cnl, "vtrigger").enabled);
	CHECK(RowOf(cnl, "vtrigger").choices.size() == 1u);
	// KEN: one of each, so neither row changes.
	const std::vector<MenuEntry> ken = PlayerRows(PlayerPickOf(kKen, kOneOne));
	CHECK_FALSE(RowOf(ken, "vskill").enabled);
	CHECK_FALSE(RowOf(ken, "vtrigger").enabled);
	// A locked screen changes neither.
	const std::vector<MenuEntry> locked = SfvSelectionScreen::Rows(PlayerPickOf(kRyu, kOneOne), false, true, true, {});
	CHECK_FALSE(RowOf(locked, "vskill").enabled);
	CHECK_FALSE(RowOf(locked, "vtrigger").enabled);
	// The dummy's screen has the loadout rows and no stage row.
	const std::vector<MenuEntry> dummy = SfvSelectionScreen::Rows(PlayerPickOf(kRyu, kOneOne), true, false, false, {});
	CHECK(HasRow(dummy, "vskill"));
	CHECK(HasRow(dummy, "vtrigger"));
	CHECK_FALSE(HasRow(dummy, "stage"));
	CHECK(HasRow(PlayerRows(PlayerPickOf(kRyu, kOneOne)), "stage"));
}

TEST_CASE("the pick screen changes a part only to a choice the fighter has, and says when the loadout changed")
{
	FillTestCatalog();
	const std::vector<int> stages = StageChoices(MissingStages());
	PlayerPick pick = PlayerPickOf(kCnl, kOneOne);
	// Right on V-Skill: II, a change.
	CHECK(SfvSelectionScreen::Apply(Adjust("vskill", 1), pick, stages));
	CHECK(picks::LoadoutOf(pick.pick) == kTwoOne);
	// Right again: II is the last, nothing changes.
	CHECK_FALSE(SfvSelectionScreen::Apply(Adjust("vskill", 1), pick, stages));
	CHECK(picks::LoadoutOf(pick.pick) == kTwoOne);
	// CNL's one V-Trigger: neither a step nor choosing II changes it.
	CHECK_FALSE(SfvSelectionScreen::Apply(Adjust("vtrigger", 1), pick, stages));
	CHECK_FALSE(SfvSelectionScreen::Apply(Choose("vtrigger", "#1"), pick, stages));
	CHECK(picks::LoadoutOf(pick.pick) == kTwoOne);
	// Choosing V-Skill I from the list: a change back.
	CHECK(SfvSelectionScreen::Apply(Choose("vskill", "#0"), pick, stages));
	CHECK(picks::LoadoutOf(pick.pick) == kOneOne);
	// The rest of the pick is untouched by the loadout's rows.
	CHECK(pick.pick.fighter == kCnl);
	CHECK(pick.pick.costume == 0);
	CHECK(pick.pick.color == 0);
	// RYU's V-Trigger steps to II and back.
	PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	CHECK(SfvSelectionScreen::Apply(Adjust("vtrigger", 1), ryu, stages));
	CHECK(picks::LoadoutOf(ryu.pick) == kOneTwo);
	CHECK(SfvSelectionScreen::Apply(Adjust("vtrigger", -1), ryu, stages));
	CHECK(picks::LoadoutOf(ryu.pick) == kOneOne);
}

TEST_CASE("the table page's and post-match prompt's Ultra steps only the fighter's own loadouts, while the runtime allows it")
{
	FillTestCatalog();
	using Field = sf4e::ui::ShellAction::SelectionStep::Field;
	SetPick(PlayerPickOf(kCnl, kOneOne));
	REQUIRE(CurrentPick().pick.fighter == kCnl);
	REQUIRE(LoadoutIndices(kCnl) == std::vector<int>{0, 2});
	// Over the WIN the pick is locked but the Ultra still changes
	// (canEditSelection false, canChangeUltra true): CNL steps I/I -> II/I and
	// no further, never through a V-Trigger II it lacks.
	CHECK(StepTableSelection(Step(Field::Ultra, 1), false, true));
	CHECK(picks::LoadoutOf(CurrentPick().pick) == kTwoOne);
	CHECK(StepTableSelection(Step(Field::Ultra, 1), false, true));
	CHECK(picks::LoadoutOf(CurrentPick().pick) == kTwoOne);
	CHECK(StepTableSelection(Step(Field::Ultra, -1), false, true));
	CHECK(picks::LoadoutOf(CurrentPick().pick) == kOneOne);
	// The colour is the pick's, locked then.
	const int colour = CurrentPick().pick.color;
	CHECK_FALSE(StepTableSelection(Step(Field::Color, 1), false, true));
	CHECK(CurrentPick().pick.color == colour);
	// Once the prompt is answered the Ultra is locked too.
	CHECK_FALSE(StepTableSelection(Step(Field::Ultra, 1), true, false));
	CHECK(picks::LoadoutOf(CurrentPick().pick) == kOneOne);
	// At the table, the colour steps through the costume's own.
	CHECK(StepTableSelection(Step(Field::Color, 1), true, false));
	CHECK(CurrentPick().pick.color == 1);
	CHECK_FALSE(StepTableSelection(Step(Field::None, 1), true, true));
	// RYU has all four, in Ultra number order.
	SetPick(PlayerPickOf(kRyu, kOneOne));
	for (const picks::Loadout expected : {kOneTwo, kTwoOne, kTwoTwo}) {
		CHECK(StepTableSelection(Step(Field::Ultra, 1), true, true));
		CHECK(picks::LoadoutOf(CurrentPick().pick) == expected);
	}
}
