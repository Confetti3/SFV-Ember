// The two ways Ember's shell changes this player's V-Skill and V-Trigger
// (EMBER_LIBRARY.md section 40), walked without a frame: SFV's pick screen,
// a row for each part (SfvSelectionScreen's Rows and Apply, what its Draw
// shows and does), and the table page's and post-match prompt's one Ultra
// value (StepTableSelection), which steps only the loadouts the fighter has.
// Then the pick screen's stage row with a stage the opponent lacks and one
// this player is not offered (section 41), and what its pane previews and
// says (section 43). The fighters are CatalogFixture's: RYU two
// of each, CNL two V-Skills and one V-Trigger, KEN one of each.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelectionScreen.hxx"

#include "common/Localization.hxx"
#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <algorithm>
#include <optional>
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
	return SfvSelectionScreen::Rows(pick, true, true, true, StageOptions(MissingStages()));
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

TEST_CASE("the pick screen offers the game's costume codes, None first, and changes only the code")
{
	FillTestCatalog();
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	PlayerPick pick = PlayerPickOf(kCnl, kOneOne);
	const std::vector<MenuEntry> rows = PlayerRows(pick);
	const MenuEntry& row = RowOf(rows, "costume-code");
	CHECK(row.enabled);
	CHECK(row.chosen == "#0");
	REQUIRE(row.choices.size() == (size_t)picks::kLoadCodeCount);
	CHECK(row.choices[1].label == "LP+MK+HP");
	// Right: LP+MK+HP, a change; Left from None stays at None.
	CHECK(SfvSelectionScreen::Apply(Adjust("costume-code", 1), pick, stages));
	CHECK(picks::LoadCodeIn(pick.pick) == 1);
	CHECK(RowOf(PlayerRows(pick), "costume-code").value == "LP+MK+HP");
	CHECK(SfvSelectionScreen::Apply(Choose("costume-code", "#0"), pick, stages));
	CHECK(picks::LoadCodeIn(pick.pick) == 0);
	CHECK_FALSE(SfvSelectionScreen::Apply(Adjust("costume-code", -1), pick, stages));
	CHECK_FALSE(SfvSelectionScreen::Apply(Choose("costume-code", "#99"), pick, stages));
	CHECK(SfvSelectionScreen::Apply(Choose("costume-code", "#2"), pick, stages));
	CHECK(picks::LoadCodeIn(pick.pick) == 2);
	// The rest of the pick is untouched.
	CHECK(pick.pick.fighter == kCnl);
	CHECK(picks::LoadoutOf(pick.pick) == kOneOne);
}

TEST_CASE("the pick screen changes a part only to a choice the fighter has, and says when the loadout changed")
{
	FillTestCatalog();
	const std::vector<StageOption> stages = StageOptions(MissingStages());
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

TEST_CASE("the stage row lists a stage the opponent lacks with the reason, leaves out one this player is not offered, and Left, Right and a choice pass both by")
{
	FillTestCatalog();
	// At a table whose opponent lacks S01 (id 0); this copy lacks S26, and
	// the account's cache lacks S02 (id 1), which is installed here.
	const std::vector<StageOption> stages = StageOptions({0, kUninstalledStage});
	PlayerPick pick = PlayerPickOf(kRyu, kOneOne);
	pick.stage = picks::kRandomStage;
	// The rows outlive the reference RowOf returns into them.
	const std::vector<MenuEntry> rows = SfvSelectionScreen::Rows(pick, true, true, true, stages);
	const MenuEntry& row = RowOf(rows, "stage");
	REQUIRE(row.choices.size() == 3);
	CHECK(row.choices[1].id == "#0");
	CHECK_FALSE(row.choices[1].enabled);
	CHECK(row.choices[1].detail == sf4e::loc::T("runtime.ready.stage_not_shared"));
	CHECK(row.choices[2].id == "#2");
	CHECK(row.choices[2].enabled);
	CHECK(row.choices[2].detail.empty());
	CHECK(std::none_of(row.choices.begin(), row.choices.end(), [](const auto& choice) { return choice.id == "#1"; }));
	CHECK(std::none_of(row.choices.begin(), row.choices.end(), [](const auto& choice) { return choice.id == "#3"; }));
	// Right from Random skips S01 (dimmed) and S02 (not listed) for S25;
	// choosing either is refused.
	CHECK(SfvSelectionScreen::Apply(Adjust("stage", 1), pick, stages));
	CHECK(pick.stage == 2);
	CHECK_FALSE(SfvSelectionScreen::Apply(Choose("stage", "#0"), pick, stages));
	CHECK_FALSE(SfvSelectionScreen::Apply(Choose("stage", "#1"), pick, stages));
	CHECK(pick.stage == 2);
	CHECK(SfvSelectionScreen::Apply(Choose("stage", "#255"), pick, stages));
	CHECK(pick.stage == picks::kRandomStage);
}

TEST_CASE("the pane shows the focused row's part of the pick")
{
	CHECK(SfvSelectionScreen::FocusOf("fighter") == PreviewFocus::Look);
	CHECK(SfvSelectionScreen::FocusOf("costume") == PreviewFocus::Look);
	CHECK(SfvSelectionScreen::FocusOf("color") == PreviewFocus::Look);
	CHECK(SfvSelectionScreen::FocusOf("vskill") == PreviewFocus::Loadout);
	CHECK(SfvSelectionScreen::FocusOf("vtrigger") == PreviewFocus::Loadout);
	CHECK(SfvSelectionScreen::FocusOf("stage") == PreviewFocus::Stage);
	CHECK(SfvSelectionScreen::FocusOf("done") == PreviewFocus::Look);
	CHECK(SfvSelectionScreen::FocusOf("") == PreviewFocus::Look);
}

TEST_CASE("with a choice open the pane previews its focused option, ready or not")
{
	FillTestCatalog();
	PlayerPick pick = PlayerPickOf(kRyu, kTwoTwo);
	pick.pick.costume = 4;
	pick.pick.color = 1;
	pick.stage = 0;
	// No option: the pick as it is.
	CHECK(SfvSelectionScreen::Previewed(pick, "costume", "").pick.costume == 4);
	// A costume keeps the colour where it has it and this player can ready
	// it, else takes its first one they can, as choosing it stores.
	PlayerPick shown = SfvSelectionScreen::Previewed(pick, "costume", "#0");
	CHECK(shown.pick.costume == 0);
	CHECK(shown.pick.color == 1);
	pick.pick.color = 100;
	shown = SfvSelectionScreen::Previewed(pick, "costume", "#0");
	CHECK(shown.pick.color == 0);
	// A costume this player cannot ready is shown as it is (RYU's costume 1).
	CHECK(SfvSelectionScreen::Previewed(pick, "costume", "#1").pick.costume == 1);
	// A colour, an EX one this player cannot ready among them.
	pick.pick.color = 0;
	CHECK(SfvSelectionScreen::Previewed(pick, "color", "#101").pick.color == 101);
	CHECK(SfvSelectionScreen::Previewed(pick, "color", "#7").pick.color == 0); // not a colour of the costume
	// A part's choice, only one the fighter has.
	CHECK(picks::LoadoutOf(SfvSelectionScreen::Previewed(pick, "vskill", "#0").pick) == kOneTwo);
	PlayerPick cnl = PlayerPickOf(kCnl, kOneOne);
	CHECK(picks::LoadoutOf(SfvSelectionScreen::Previewed(cnl, "vtrigger", "#1").pick) == kOneOne);
	// A fighter this player cannot play (KEN): as the spec fits it, its
	// first look, the loadout fitted to it.
	shown = SfvSelectionScreen::Previewed(pick, "fighter", "#2");
	CHECK(shown.pick.fighter == kKen);
	CHECK(shown.pick.costume == 0);
	CHECK(shown.pick.color == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	// A stage, and Random.
	CHECK(SfvSelectionScreen::Previewed(pick, "stage", "#2").stage == 2);
	CHECK(SfvSelectionScreen::Previewed(pick, "stage", "#255").stage == picks::kRandomStage);
	// The stored pick itself is never touched.
	CHECK(pick.pick.fighter == kRyu);
	CHECK(pick.stage == 0);
}

TEST_CASE("a fighter or costume previewed is what choosing it stores")
{
	FillTestCatalog();
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	const auto same = [](const sf4e::selection::Pick& a, const sf4e::selection::Pick& b) {
		return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && picks::LoadoutOf(a) == picks::LoadoutOf(b);
	};
	// From RYU's Nostalgia in colour 02 with both II: every fighter and
	// costume this player can choose, previewed and then chosen.
	PlayerPick from = PlayerPickOf(kRyu, kTwoTwo);
	from.pick.costume = 4;
	from.pick.color = 1;
	const std::vector<MenuEntry> rows = PlayerRows(from);
	for (const char* row : {"fighter", "costume"}) {
		for (const sf4e::ui::MenuChoice& option : RowOf(rows, row).choices) {
			if (!option.enabled) continue;
			CAPTURE(row);
			CAPTURE(option.id);
			const PlayerPick shown = SfvSelectionScreen::Previewed(from, row, option.id);
			PlayerPick chosen = from;
			SfvSelectionScreen::Apply(Choose(row, option.id.c_str()), chosen, stages);
			CHECK(same(shown.pick, SetPick(chosen).pick));
		}
	}
	// CNL keeps colour 02, which its first costume has and this player can
	// ready, where its first look would be colour 01.
	const PlayerPick cnl = SfvSelectionScreen::Previewed(from, "fighter", "#1");
	CHECK(cnl.pick.costume == 0);
	CHECK(cnl.pick.color == 1);
	CHECK(picks::LoadoutOf(cnl.pick) == kTwoOne);
}

TEST_CASE("the pane says why an option cannot be chosen, and that a stage shown is one the opponent lacks, with its row focused or its choice open")
{
	FillTestCatalog();
	// At a table whose opponent lacks S01 (id 0), with S01 stored.
	const std::vector<StageOption> stages = StageOptions({0, kUninstalledStage});
	PlayerPick pick = PlayerPickOf(kRyu, kOneOne);
	pick.stage = 0;
	const std::vector<MenuEntry> rows = SfvSelectionScreen::Rows(pick, true, true, true, stages);
	const std::string notShared = sf4e::loc::T("runtime.ready.stage_not_shared");
	// The stage row focused, no choice open: the stored stage, dimmed, and why.
	PickPreview preview = SfvSelectionScreen::PreviewOf(pick, "stage", std::nullopt, rows, stages);
	CHECK(preview.focus == PreviewFocus::Stage);
	CHECK(preview.pick.stage == 0);
	CHECK_FALSE(preview.stageShared);
	CHECK(preview.note == notShared);
	// The stage choice open on S25 (its third option): shared, nothing to say.
	preview = SfvSelectionScreen::PreviewOf(pick, "stage", SfvSelectionScreen::OpenChoice{"stage", 2}, rows, stages);
	CHECK(preview.pick.stage == 2);
	CHECK(preview.stageShared);
	CHECK(preview.note.empty());
	// Back on S01 in the open choice: why again.
	preview = SfvSelectionScreen::PreviewOf(pick, "stage", SfvSelectionScreen::OpenChoice{"stage", 1}, rows, stages);
	CHECK(preview.pick.stage == 0);
	CHECK(preview.note == notShared);
	// Another row focused: the look, with nothing to say about the stage.
	preview = SfvSelectionScreen::PreviewOf(pick, "fighter", std::nullopt, rows, stages);
	CHECK(preview.focus == PreviewFocus::Look);
	CHECK(preview.note.empty());
	// The costume choice open on RYU's costume 1, which this player cannot
	// ready: shown, with SF4's NOT OWNED words.
	preview = SfvSelectionScreen::PreviewOf(pick, "fighter", SfvSelectionScreen::OpenChoice{"costume", 1}, rows, stages);
	CHECK(preview.pick.pick.costume == 1);
	CHECK(preview.note == sf4e::loc::T("selection.not_owned"));
	// A part's choice: its line brighter, its move already on it, no note.
	preview = SfvSelectionScreen::PreviewOf(pick, "fighter", SfvSelectionScreen::OpenChoice{"vtrigger", 1}, rows, stages);
	CHECK(preview.focus == PreviewFocus::Loadout);
	CHECK(preview.part == picks::LoadoutPart::VTrigger);
	CHECK(picks::LoadoutOf(preview.pick.pick) == kOneTwo);
	CHECK(preview.note.empty());
	// An index past the row's choices previews the pick itself.
	preview = SfvSelectionScreen::PreviewOf(pick, "fighter", SfvSelectionScreen::OpenChoice{"costume", 99}, rows, stages);
	CHECK(preview.pick.pick.costume == pick.pick.costume);
	CHECK(preview.note.empty());
}
