// The two ways Ember's shell changes this player's V-Skill and V-Trigger
// (EMBER_LIBRARY.md section 40), walked without a frame: SFV's pick screen,
// a row for each part (SfvSelectionScreen's Rows and Apply, what its Draw
// shows and does), and the table page's and post-match prompt's one Ultra
// value (StepTableSelection), which steps only the loadouts the fighter has.
// Then the pick screen's stage row with a stage the opponent lacks and one
// this player is not offered (section 41), and what its pane previews and
// says (section 43). Then the beta.8 pages (docs/design/beta8/select.md):
// the board's cards and the look its art pane shows, the look page's rows
// that open pages, where each opening starts, the costume, colour, loadout
// and stage cards and what Select on them stores or refuses, and Random,
// which takes each roll into the player's pick and leaves the dummy alone.
// The fighters are CatalogFixture's: RYU two of each, CNL two V-Skills and
// one V-Trigger, KEN one of each and not this player's, CMY not installed.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvLoadoutNotes.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRandomFighter.hxx"
#include "ember/SfvSelectGrid.hxx"
#include "ember/SfvSelectionScreen.hxx"

#include "common/Localization.hxx"
#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;

namespace {

constexpr int kRyu = 0, kCnl = 1, kKen = 2, kBsn = 4;

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

namespace {

std::vector<std::string> IdsOf(const std::vector<MenuEntry>& rows)
{
	std::vector<std::string> ids;
	for (const MenuEntry& row : rows) ids.push_back(row.id);
	return ids;
}

bool SameLook(const sf4e::selection::Pick& a, const sf4e::selection::Pick& b)
{
	return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && picks::LoadoutOf(a) == picks::LoadoutOf(b);
}

using Pages = std::vector<std::pair<std::string, std::string>>;

} // namespace

TEST_CASE("the board has a card for every fighter in the roster's order, then Random and Eleven; one this player cannot play keeps its place")
{
	FillTestCatalog();
	const PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> rows = SfvSelectionScreen::BoardRows(ryu, false, true, false, true);
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
	CHECK_FALSE(HasRow(SfvSelectionScreen::BoardRows(ryu, false, true, false, false), "random-fighter"));
	CHECK_FALSE(HasRow(SfvSelectionScreen::BoardRows(ryu, false, true, false, false), "eleven-fighter"));
	// Locked: only the stored fighter, whose look page opens; Random only while it is the pick.
	const std::vector<MenuEntry> locked = SfvSelectionScreen::BoardRows(ryu, false, false, false, true);
	CHECK(RowOf(locked, "fighter-0").enabled);
	CHECK_FALSE(RowOf(locked, "fighter-1").enabled);
	CHECK_FALSE(RowOf(locked, "random-fighter").enabled);
	CHECK(RowOf(SfvSelectionScreen::BoardRows(ryu, true, false, false, true), "random-fighter").enabled);
	// Eleven only while it is the pick, and then Random is not the stored card.
	CHECK_FALSE(RowOf(locked, "eleven-fighter").enabled);
	const std::vector<MenuEntry> elevenLocked = SfvSelectionScreen::BoardRows(ryu, true, false, false, true, true);
	CHECK(RowOf(elevenLocked, "eleven-fighter").enabled);
	CHECK_FALSE(RowOf(elevenLocked, "random-fighter").enabled);
	// A winner who keeps the fighter chooses no other, and not Random.
	const std::vector<MenuEntry> kept = SfvSelectionScreen::BoardRows(ryu, false, true, true, true);
	CHECK(RowOf(kept, "fighter-0").enabled);
	CHECK_FALSE(RowOf(kept, "fighter-1").enabled);
	CHECK(RowOf(kept, "fighter-1").detail == sf4e::loc::T("selection.winner_keeps_fighter"));
	CHECK_FALSE(RowOf(kept, "random-fighter").enabled);
	CHECK_FALSE(RowOf(kept, "eleven-fighter").enabled);
}

TEST_CASE("the board's art pane shows the look choosing the fighter stores: their remembered look on the player's screen")
{
	FillTestCatalog();
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
	PlayerPick shown = SfvSelectionScreen::BoardPick(cnl, "fighter-0", true);
	CHECK(SameLook(shown.pick, ryu.pick));
	CHECK(SameLook(LookFor(cnl.pick, kRyu, true), ryu.pick));
	// The dummy's screen keeps no memory: CNL's look fitted to RYU.
	shown = SfvSelectionScreen::BoardPick(cnl, "fighter-0", false);
	CHECK(shown.pick.fighter == kRyu);
	CHECK(shown.pick.costume == 0);
	CHECK(shown.pick.color == 1);
	CHECK(picks::LoadoutOf(shown.pick) == kTwoOne);
	// A fighter this player cannot play, as the spec fits it.
	shown = SfvSelectionScreen::BoardPick(cnl, "fighter-2", false);
	CHECK(shown.pick.fighter == kKen);
	CHECK(shown.pick.costume == 0);
	CHECK(picks::LoadoutOf(shown.pick) == kOneOne);
	// The stored fighter, Random's card and any other id: the pick itself.
	for (const char* id : {"fighter-1", "random-fighter", "eleven-fighter", "stage-2", "fighter-99"}) {
		CAPTURE(id);
		CHECK(SameLook(SfvSelectionScreen::BoardPick(cnl, id, true).pick, cnl.pick));
	}
	// The stage is never part of a fighter's look.
	cnl.stage = 2;
	CHECK(SfvSelectionScreen::BoardPick(cnl, "fighter-0", true).stage == 2);
}

TEST_CASE("the look page's rows open the board and the cards, and the costume code its choices")
{
	FillTestCatalog();
	using sf4e::ui::MenuSelectOpens;
	using sf4e::ui::SelectOpens;
	const std::vector<StageOption> stages = StageOptions(MissingStages());
	const std::vector<MenuEntry> rows = SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, true, true, stages);
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
	const std::vector<MenuEntry> ken = SfvSelectionScreen::LookRows(PlayerPickOf(kKen, kOneOne), false, true, true, true, stages);
	for (const char* id : {"vskill", "vtrigger"}) {
		CAPTURE(id);
		CHECK(RowOf(ken, id).enabled);
		CHECK_FALSE(RowOf(ken, id).adjustable);
		CHECK(MenuSelectOpens(RowOf(ken, id)) == SelectOpens::Activate);
	}
	// P2: the stage row opens the stage cards, which say who chooses, and
	// does not step.
	const std::vector<MenuEntry> p2 = SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, true, false, stages);
	CHECK(RowOf(p2, "stage").enabled);
	CHECK_FALSE(RowOf(p2, "stage").adjustable);
	CHECK(RowOf(p2, "stage").detail == sf4e::loc::T("selection.only_p1_stage"));
	// Locked: the look's rows do nothing; the loadout and the stage open to read.
	const std::vector<MenuEntry> locked = SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), false, false, true, true, stages);
	CHECK(MenuSelectOpens(RowOf(locked, "costume")) == SelectOpens::Nothing);
	CHECK(MenuSelectOpens(RowOf(locked, "vskill")) == SelectOpens::Activate);
	CHECK_FALSE(RowOf(locked, "vskill").adjustable);
	// Random: the fighter row names the roll as Random's.
	const std::vector<MenuEntry> eleven =
		SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), true, true, true, true, stages, false, true);
	CHECK(RowOf(eleven, "fighter").value == ElevenTitle("Ryu"));
	const std::vector<MenuEntry> random = SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), true, true, true, true, stages);
	CHECK(RowOf(random, "fighter").value == sf4e::loc::Tf("selection.random_fighter", "Ryu"));
	// The dummy's: no stage row.
	CHECK_FALSE(HasRow(SfvSelectionScreen::LookRows(PlayerPickOf(kRyu, kOneOne), false, true, false, false, {}), "stage"));
}

TEST_CASE("each way the shell opens the screen starts on its page, and the room's one changes close it again")
{
	FillTestCatalog();
	using sf4e::ui::SelectionStart;
	PlayerPick pick = PlayerPickOf(kRyu, kTwoOne);
	pick.pick.costume = 4;
	pick.stage = 2;
	const auto opening = [&pick](SelectionStart start, bool random = false, bool dummy = false, bool eleven = false) {
		return SfvSelectionScreen::OpeningFor(start, pick, random, dummy, eleven);
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
	const std::vector<MenuEntry> costumes = SfvSelectionScreen::CostumeCards(ryu, true);
	CHECK(IdsOf(costumes) == std::vector<std::string>{"costume-0", "costume-1", "costume-4"});
	CHECK(RowOf(costumes, "costume-0").enabled);
	CHECK(RowOf(costumes, "costume-0").label == CostumeName(kRyu, 0));
	CHECK(RowOf(costumes, "costume-4").enabled);
	// RYU's costume 1: none of its colours is theirs.
	CHECK_FALSE(RowOf(costumes, "costume-1").enabled);
	CHECK(RowOf(costumes, "costume-1").detail == sf4e::loc::T("selection.not_owned"));
	const std::vector<MenuEntry> colours = SfvSelectionScreen::ColourCards(ryu, 4, true);
	CHECK(IdsOf(colours) == std::vector<std::string>{"color-0", "color-1", "color-100", "color-101"});
	CHECK(RowOf(colours, "color-1").enabled);
	CHECK(RowOf(colours, "color-1").label == ColourName(1));
	CHECK(RowOf(colours, "color-1").hint == sf4e::loc::T("menu.hint.save_color"));
	CHECK(RowOf(colours, "color-100").label == ColourName(100));
	CHECK_FALSE(RowOf(colours, "color-100").enabled);
	CHECK(RowOf(colours, "color-100").detail == sf4e::loc::T("selection.not_owned"));
	// Locked: nothing can be chosen.
	for (const MenuEntry& row : SfvSelectionScreen::CostumeCards(ryu, false)) CHECK_FALSE(row.enabled);
	for (const MenuEntry& row : SfvSelectionScreen::ColourCards(ryu, 0, false)) CHECK_FALSE(row.enabled);
	// A costume the fighter does not have: the one row that says so.
	const std::vector<MenuEntry> none = SfvSelectionScreen::ColourCards(ryu, 99, true);
	REQUIRE(none.size() == 1u);
	CHECK_FALSE(none[0].enabled);
	CHECK(none[0].label == sf4e::loc::T("selection.no_colors"));
}

TEST_CASE("the loadout cards are each choice the fighter has, with its note, and Select stores that choice")
{
	FillTestCatalog();
	PlayerPick ryu = PlayerPickOf(kRyu, kOneOne);
	const std::vector<MenuEntry> cards = SfvSelectionScreen::LoadoutCards(ryu, true);
	CHECK(IdsOf(cards) == std::vector<std::string>{"vskill-0", "vskill-1", "vtrigger-0", "vtrigger-1"});
	for (size_t i = 0; i < cards.size(); i++) {
		CAPTURE(cards[i].id);
		CHECK(cards[i].key == i); // the picture's number, part * 2 + choice
		CHECK(cards[i].enabled);
	}
	CHECK(RowOf(cards, "vskill-1").label == ChoiceText(1, "Skill Two"));
	CHECK(RowOf(cards, "vtrigger-0").detail == LoadoutNote(kRyu, picks::LoadoutPart::VTrigger, 0));
	CHECK(IdsOf(SfvSelectionScreen::LoadoutCards(PlayerPickOf(kCnl, kOneOne), true)) ==
		std::vector<std::string>{"vskill-0", "vskill-1", "vtrigger-0"});
	for (const MenuEntry& card : SfvSelectionScreen::LoadoutCards(ryu, false)) CHECK_FALSE(card.enabled);
	// Select stores the choice, and only a choice the fighter has.
	CHECK(SfvSelectionScreen::ChooseLoadout("vtrigger-1", ryu));
	CHECK(picks::LoadoutOf(ryu.pick) == kOneTwo);
	CHECK_FALSE(SfvSelectionScreen::ChooseLoadout("vtrigger-1", ryu));
	CHECK(SfvSelectionScreen::ChooseLoadout("vskill-1", ryu));
	CHECK(picks::LoadoutOf(ryu.pick) == kTwoTwo);
	PlayerPick cnl = PlayerPickOf(kCnl, kOneOne);
	CHECK_FALSE(SfvSelectionScreen::ChooseLoadout("vtrigger-1", cnl));
	CHECK_FALSE(SfvSelectionScreen::ChooseLoadout("vskill-2", cnl));
	CHECK_FALSE(SfvSelectionScreen::ChooseLoadout("vskill", cnl));
	CHECK(picks::LoadoutOf(cnl.pick) == kOneOne);
}

TEST_CASE("a stage card stores the stage; P2, a watcher and a stage the opponent lacks or this player is not offered are refused")
{
	FillTestCatalog();
	using Answer = SfvSelectionScreen::StageAnswer;
	// At a table whose opponent lacks S01 (id 0); this copy lacks S26, and
	// the account's cache lacks S02 (id 1).
	const std::vector<StageOption> stages = StageOptions({0, kUninstalledStage});
	PlayerPick pick = PlayerPickOf(kRyu, kOneOne);
	pick.stage = picks::kRandomStage;
	const std::vector<MenuEntry> cards = SfvSelectionScreen::StageCards(pick, true, true, stages);
	CHECK(IdsOf(cards) == std::vector<std::string>{"stage-255", "stage-0", "stage-2"});
	CHECK(RowOf(cards, "stage-255").enabled);
	CHECK(RowOf(cards, "stage-255").detail == sf4e::loc::T("selection.random_stage.detail"));
	CHECK(RowOf(cards, "stage-2").enabled);
	CHECK(RowOf(cards, "stage-2").hint == sf4e::loc::T("menu.hint.save_stage"));
	CHECK_FALSE(RowOf(cards, "stage-0").enabled);
	CHECK(RowOf(cards, "stage-0").detail == sf4e::loc::T("runtime.ready.stage_not_shared"));
	// Select on a card stores its stage.
	CHECK(SfvSelectionScreen::ChooseStage("stage-2", pick, true, true, stages) == Answer::Stored);
	CHECK(pick.stage == 2);
	CHECK(SfvSelectionScreen::ChooseStage("stage-2", pick, true, true, stages) == Answer::Unchanged);
	// The opponent lacks S01; S02 and S26 are not this player's to offer.
	CHECK(SfvSelectionScreen::ChooseStage("stage-0", pick, true, true, stages) == Answer::NotShared);
	CHECK(SfvSelectionScreen::ChooseStage("stage-1", pick, true, true, stages) == Answer::NotOffered);
	CHECK(SfvSelectionScreen::ChooseStage("stage-3", pick, true, true, stages) == Answer::NotOffered);
	CHECK(SfvSelectionScreen::ChooseStage("stage-x", pick, true, true, stages) == Answer::NotOffered);
	CHECK(pick.stage == 2);
	CHECK(SfvSelectionScreen::ChooseStage("stage-255", pick, true, true, stages) == Answer::Stored);
	CHECK(pick.stage == picks::kRandomStage);
	// P2: every card shown, none chosen, each saying only P1 chooses.
	const std::vector<MenuEntry> p2 = SfvSelectionScreen::StageCards(pick, true, false, stages);
	CHECK(IdsOf(p2) == IdsOf(cards));
	CHECK_FALSE(RowOf(p2, "stage-2").enabled);
	CHECK(RowOf(p2, "stage-2").detail == sf4e::loc::T("selection.only_p1_stage"));
	CHECK(SfvSelectionScreen::ChooseStage("stage-2", pick, true, false, stages) == Answer::OnlyP1);
	// A watcher's (or anyone's locked) screen.
	CHECK(SfvSelectionScreen::ChooseStage("stage-2", pick, false, false, stages) == Answer::Locked);
	CHECK(pick.stage == picks::kRandomStage);
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
	CHECK_FALSE(HasRow(SfvSelectionScreen::BoardRows(CurrentDummy(), false, true, false, false), "random-fighter"));
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
		return 167 + 70 * SfvSelectionScreen::BoardColumn(table, *cell);
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
	std::vector<MenuEntry> rows = SfvSelectionScreen::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, true);
	SfvSelectionScreen::LinkRandom(rows, table);
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
	std::vector<MenuEntry> dummy = SfvSelectionScreen::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, false);
	SfvSelectionScreen::LinkRandom(dummy, table);
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
	const std::vector<MenuEntry> rows = SfvSelectionScreen::ColourCards(PlayerPickOf(kRyu, kOneOne), 4, true);
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
	const std::vector<MenuEntry> board = SfvSelectionScreen::BoardRows(PlayerPickOf(kRyu, kOneOne), false, true, false, true);
	sf4e::ui::MenuNavigation boardNav;
	boardNav.Focus("fighter-2", board);
	CHECK(boardNav.Focus() == "fighter-2");
	boardNav.Update({0, now += 1}, board);
	CHECK(boardNav.Update({Button::Select, now += 1}, board).kind == MenuAction::None);
}
