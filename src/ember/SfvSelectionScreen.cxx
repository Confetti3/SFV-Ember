#include "ember/SfvSelectionScreen.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/MenuRows.hxx"
#include "ui/SelectionLabels.hxx"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace sfve::adapter {

namespace {

using sf4e::ui::MenuAction;
using sf4e::ui::MenuChoice;
using sf4e::ui::MenuEntry;
namespace loc = sf4e::loc;

// The rows' ids, and the options' ids of the fighter and stage choices.
constexpr const char* kFighter = "fighter";
constexpr const char* kCostume = "costume";
constexpr const char* kColour = "color";
// The game's costume codes (picks::kLoadCodes, EMBER_LIBRARY.md section 55).
constexpr const char* kCostumeCode = "costume-code";
constexpr const char* kVSkill = "vskill";
constexpr const char* kVTrigger = "vtrigger";
// The model size, Normal or Tiny (picks::kSizeNormal, kSizeTiny): a room that
// allows tiny fighters only (EMBER_LIBRARY.md section 59).
constexpr const char* kSize = "size";
constexpr const char* kStage = "stage";
constexpr const char* kDone = "done";

// The loadout's rows, one per part, in SFV's own order.
struct LoadoutRow {
	const char* id;
	picks::LoadoutPart part;
};
constexpr LoadoutRow kLoadoutRows[] = {{kVSkill, picks::LoadoutPart::VSkill}, {kVTrigger, picks::LoadoutPart::VTrigger}};

// The pane is as wide as the fighter's banner (CharaSelectIcon_l, 380x258).
constexpr float kBannerWidth = 380;

// Why a stage the opponent lacks cannot be chosen: the runtime's own words,
// on the stage row's option and in the pane.
constexpr const char* kStageNotShared = "runtime.ready.stage_not_shared";

std::string OptionId(int id)
{
	return "#" + std::to_string(id);
}

std::optional<int> OptionOf(const std::string& option)
{
	if (option.size() < 2 || option[0] != '#') return std::nullopt;
	return std::atoi(option.c_str() + 1);
}

bool Contains(const std::vector<int>& ids, int id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// The roster in the game's select-screen order (the catalog's rosterOrder).
std::vector<int> Roster()
{
	const ember::GameCatalog& catalog = ember::Catalog();
	return std::vector<int>(catalog.rosterOrder, catalog.rosterOrder + catalog.fighterCount);
}

// The roster less the fighters this copy's player cannot play (the
// catalog's fighterLocked): not installed here, or (with Ember's ownership
// gate on) not theirs or with no look of theirs.
std::vector<int> PlayableRoster()
{
	std::vector<int> playable;
	for (int id : Roster()) {
		if (!ember::Catalog().fighterLocked(id, {})) playable.push_back(id);
	}
	return playable;
}

// A costume code's words: its buttons, or the shared None.
std::string LoadCodeName(int index)
{
	const char* label = index > 0 && index < picks::kLoadCodeCount ? picks::kLoadCodes[index].label : nullptr;
	return label ? std::string(label) : std::string(loc::T("common.none"));
}

// The code indices Left and Right step through: every one, None first.
std::vector<int> LoadCodeIds()
{
	std::vector<int> ids;
	for (int index = 0; index < picks::kLoadCodeCount; index++) ids.push_back(index);
	return ids;
}

// The sizes Left and Right step through, and the words for each.
std::vector<int> SizeIds()
{
	return {picks::kSizeNormal, picks::kSizeTiny};
}

std::string SizeName(int size)
{
	return loc::T(size == picks::kSizeTiny ? "selection.size.tiny" : "selection.size.normal");
}

// A value row Left and Right step; with `choices`, Select opens them all.
MenuEntry StepRow(const char* id, const char* label, std::string value, const char* detail, bool enabled,
	std::vector<MenuChoice> choices = {}, int chosen = -1)
{
	MenuEntry row = sf4e::ui::Value(id, loc::T(label), std::move(value), loc::T(detail), enabled);
	row.choices = std::move(choices);
	if (chosen >= 0) row.chosen = OptionId(chosen);
	return row;
}

// The loadout part a row stands for; none for any other row.
std::optional<picks::LoadoutPart> PartOfRow(const std::string& id)
{
	for (const LoadoutRow& row : kLoadoutRows) {
		if (id == row.id) return row.part;
	}
	return std::nullopt;
}

// The part's choice numbers the fighter has: 0, and 1 where it has two.
std::vector<int> PartNumbers(std::int64_t fighter, picks::LoadoutPart part)
{
	std::vector<int> numbers;
	for (int number = 0; number < PartChoices(fighter, part); number++) numbers.push_back(number);
	return numbers;
}

// A part's row, labelled in the game's words: I or II, each choice with the
// game's name for its move; one the fighter has only one of cannot change.
MenuEntry PartRow(const LoadoutRow& row, const sf4e::selection::Pick& pick, bool editable, const char* adjusts)
{
	const int chosen = picks::LoadoutOf(pick).choice[row.part];
	const std::vector<int> numbers = PartNumbers(pick.fighter, row.part);
	std::vector<MenuChoice> choices;
	for (int number : numbers) {
		choices.push_back(
			{OptionId(number), picks::Numeral(number), ChoiceText(number, MoveName(pick.fighter, row.part, number)), editable});
	}
	MenuEntry entry =
		sf4e::ui::Value(row.id, PartLabel(row.part), picks::Numeral(chosen), loc::T(adjusts), editable && numbers.size() > 1);
	entry.choices = std::move(choices);
	entry.chosen = OptionId(chosen);
	return entry;
}

// The pick with the part's row's change made (Left and Right, or a choice);
// true when the loadout changed. Only a choice the fighter has is taken.
bool ApplyPart(const MenuAction& action, picks::LoadoutPart part, sf4e::selection::Pick& pick)
{
	const picks::Loadout before = picks::LoadoutOf(pick);
	const std::vector<int> numbers = PartNumbers(pick.fighter, part);
	int number = before.choice[part];
	if (action.kind == MenuAction::Adjust) sf4e::ui::Step(number, numbers, action.delta);
	else if (action.kind == MenuAction::Chosen) {
		const std::optional<int> option = OptionOf(action.text);
		if (option && Contains(numbers, *option)) number = *option;
	}
	picks::Loadout after = before;
	after.choice[part] = (std::uint8_t)number;
	picks::SetLoadout(pick, after);
	return after != before;
}

// Where the screen starts for each way the shell opens it, and whether a
// choice made on that row closes it again. The room's Fighter and Stage rows
// and the costume shortcut open it for that one change; the room's Ultra (the
// loadout) and a winner's look and loadout are more than one row's change,
// so it stays open until Done or Back.
struct Opening {
	sf4e::ui::SelectionStart start;
	const char* row;
	bool closesOnChoice;
};
constexpr Opening kOpenings[] = {
	{sf4e::ui::SelectionStart::Roster, kFighter, true},
	{sf4e::ui::SelectionStart::Stage, kStage, true},
	{sf4e::ui::SelectionStart::Costumes, kCostume, true},
	{sf4e::ui::SelectionStart::Ultras, kVSkill, false},
	{sf4e::ui::SelectionStart::LookUltra, kCostume, false},
};

} // namespace

bool StepTableSelection(const sf4e::ui::ShellAction::SelectionStep& step, bool canEditSelection, bool canChangeUltra)
{
	using Field = sf4e::ui::ShellAction::SelectionStep::Field;
	PlayerPick pick = CurrentPick();
	switch (step.field) {
	case Field::Ultra: {
		if (!canChangeUltra) return false;
		int index = picks::LoadoutIndex(picks::LoadoutOf(pick.pick));
		sf4e::ui::Step(index, LoadoutIndices(pick.pick.fighter), step.delta);
		picks::SetLoadout(pick.pick, *picks::LoadoutAt(index));
		break;
	}
	case Field::Color:
		if (!canEditSelection) return false;
		sf4e::ui::Step(pick.pick.color, ColourIds(pick.pick.fighter, pick.pick.costume, true), step.delta);
		break;
	case Field::None:
		return false;
	}
	SetPick(pick);
	return true;
}

std::vector<MenuEntry> SfvSelectionScreen::Rows(const PlayerPick& pick, bool editable, bool withStage, bool stageEditable,
	const std::vector<StageOption>& stageOptions, bool withSize)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const char* adjusts = editable ? "selection.adjust_saves" : "selection.locked_detail";
	// Every fighter, costume and colour of the spec is listed, as SF4's grid
	// keeps an unowned fighter's card: one this player cannot ready cannot be
	// chosen, and one the game does not let them pick says so (NOT OWNED,
	// SF4's words). What can be chosen is the catalog's pickable lists, the
	// same ones their pick is fitted to, so a costume with no colour they can
	// ready is never offered. Left and Right step through only those.
	const auto notOwned = [](bool unowned) { return unowned ? std::string(loc::T("selection.not_owned")) : std::string(); };
	std::vector<MenuChoice> fighters;
	for (int id : Roster()) {
		fighters.push_back({OptionId(id), catalog.fighterName(id), notOwned(FighterNotOwned(id)),
			editable && !catalog.fighterLocked(id, {})});
	}
	const std::vector<int> readyCostumes = CostumeIds(pick.pick.fighter, true);
	std::vector<MenuChoice> costumes;
	for (int id : CostumeIds(pick.pick.fighter, false)) {
		costumes.push_back({OptionId(id), CostumeName(pick.pick.fighter, id), notOwned(CostumeNotOwned(pick.pick.fighter, id)),
			editable && Contains(readyCostumes, id)});
	}
	const std::vector<int> readyColours = ColourIds(pick.pick.fighter, pick.pick.costume, true);
	std::vector<MenuChoice> colours;
	for (int id : ColourIds(pick.pick.fighter, pick.pick.costume)) {
		colours.push_back({OptionId(id), ColourName(id), notOwned(ColourNotOwned(pick.pick.fighter, pick.pick.costume, id)),
			editable && Contains(readyColours, id)});
	}
	// A stage the opponent lacks is listed, unable to be chosen, with the
	// runtime's own reason (EMBER_LIBRARY.md section 43).
	std::vector<MenuChoice> stages;
	for (const StageOption& option : stageOptions) {
		const bool lacking = option.offer == StageOffer::OpponentLacks;
		stages.push_back({OptionId(option.id), sf4e::ui::StageLabel(option.id),
			lacking ? std::string(loc::T(kStageNotShared)) : std::string(), editable && stageEditable && !lacking});
	}
	std::vector<MenuEntry> rows;
	rows.push_back(StepRow(kFighter, "selection.fighter", catalog.fighterName(pick.pick.fighter),
		editable ? "selection.select_saves" : "selection.locked_detail", editable, std::move(fighters), pick.pick.fighter));
	rows.push_back(StepRow(kCostume, "selection.costume", CostumeName(pick.pick.fighter, pick.pick.costume), adjusts, editable,
		std::move(costumes), pick.pick.costume));
	rows.push_back(StepRow(kColour, "selection.color_row", ColourName(pick.pick.color), adjusts, editable, std::move(colours),
		pick.pick.color));
	// The buttons held as a room's battle loads, for the game's own hidden
	// looks (Urien's LP+MK+HP): SF4's personal action row's words, which SFV
	// Ember's catalogs give as "Costume code" (locales/*.po).
	const int code = picks::LoadCodeIn(pick.pick).value_or(0);
	std::vector<MenuChoice> codes;
	for (int index : LoadCodeIds()) codes.push_back({OptionId(index), LoadCodeName(index), std::string(), editable});
	rows.push_back(StepRow(kCostumeCode, "selection.personal_action", LoadCodeName(code), adjusts, editable, std::move(codes), code));
	for (const LoadoutRow& row : kLoadoutRows) rows.push_back(PartRow(row, pick.pick, editable, adjusts));
	// A tiny fighters room's own choice: the picture only, nothing of the fight.
	if (withSize) {
		std::vector<MenuChoice> sizes;
		for (int size : SizeIds()) sizes.push_back({OptionId(size), SizeName(size), std::string(), editable});
		rows.push_back(StepRow(kSize, "selection.size", SizeName(pick.pick.size), adjusts, editable, std::move(sizes), pick.pick.size));
	}
	if (withStage) {
		rows.push_back(StepRow(kStage, "selection.stage", sf4e::ui::StageLabel(pick.stage),
			!editable ? "selection.locked_detail" : stageEditable ? "selection.p1_stage" : "selection.only_p1_stage",
			editable && stageEditable, std::move(stages), pick.stage));
	}
	rows.push_back(sf4e::ui::Row(kDone, loc::T("keyboard.done"), loc::T("selection.status_adjust")));
	return rows;
}

bool SfvSelectionScreen::Apply(const MenuAction& action, PlayerPick& pick, const std::vector<StageOption>& stageOptions)
{
	if (const std::optional<picks::LoadoutPart> part = PartOfRow(action.id)) return ApplyPart(action, *part, pick.pick);
	const ember::GameCatalog& catalog = ember::Catalog();
	const PlayerPick before = pick;
	// Left and Right go through what this copy's player can ready: the
	// fighters they can play, the fighter's costumes with a look they can
	// ready, and that costume's colours making one. A costume or colour the
	// new fighter or costume lacks, or that is not theirs, is fitted as the
	// pick is stored (SetPick), and so is a loadout part the new fighter
	// lacks. A choice they cannot ready is refused here too, whatever the
	// list let through, and so is a stage the opponent lacks.
	const std::vector<int> stageChoices = PlayableIds(stageOptions);
	if (action.kind == MenuAction::Adjust) {
		if (action.id == kFighter) sf4e::ui::Step(pick.pick.fighter, PlayableRoster(), action.delta);
		else if (action.id == kCostume) sf4e::ui::Step(pick.pick.costume, CostumeIds(pick.pick.fighter, true), action.delta);
		else if (action.id == kColour) {
			sf4e::ui::Step(pick.pick.color, ColourIds(pick.pick.fighter, pick.pick.costume, true), action.delta);
		} else if (action.id == kStage) sf4e::ui::Step(pick.stage, stageChoices, action.delta);
		else if (action.id == kCostumeCode) {
			int code = picks::LoadCodeIn(pick.pick).value_or(0);
			sf4e::ui::Step(code, LoadCodeIds(), action.delta);
			picks::SetLoadCode(pick.pick, code);
		} else if (action.id == kSize) {
			int size = pick.pick.size;
			sf4e::ui::Step(size, SizeIds(), action.delta);
			pick.pick.size = size;
		}
	} else if (action.kind == MenuAction::Chosen) {
		const std::optional<int> option = OptionOf(action.text);
		const auto pickable = [&option](const std::vector<int>& ids) { return option && Contains(ids, *option); };
		if (option && action.id == kFighter && ember::ValidFighter(*option) && !catalog.fighterLocked(*option, {})) {
			pick.pick.fighter = *option;
		} else if (action.id == kCostume && pickable(CostumeIds(pick.pick.fighter, true))) {
			pick.pick.costume = *option;
		} else if (action.id == kColour && pickable(ColourIds(pick.pick.fighter, pick.pick.costume, true))) {
			pick.pick.color = *option;
		} else if (action.id == kStage && pickable(stageChoices)) pick.stage = *option;
		else if (action.id == kCostumeCode && pickable(LoadCodeIds())) picks::SetLoadCode(pick.pick, *option);
		else if (action.id == kSize && pickable(SizeIds())) pick.pick.size = *option;
	}
	return pick.pick.fighter != before.pick.fighter || pick.pick.costume != before.pick.costume ||
		pick.pick.color != before.pick.color || pick.stage != before.stage ||
		pick.pick.personalAction != before.pick.personalAction || pick.pick.size != before.pick.size;
}

void SfvSelectionScreen::Draw(bool editable, bool stageEditable, const std::string& error,
	const std::vector<std::int64_t>& unavailableStages, bool tinyAllowed)
{
	auto& nav = menu_.navigation;
	// The shell says where Back goes and which of its shortcuts this screen
	// hands back; one it has just opened starts on its first row, or on the
	// row it was opened for.
	const sf4e::ui::EmbeddedReturn& embedded = sf4e::ui::EmbeddedReturnContext();
	if (sf4e::ui::TakeEmbeddedFresh()) {
		nav.Home();
		openOn_.clear();
		for (const Opening& opening : kOpenings) {
			if (opening.start != embedded.openOn) continue;
			nav.Prefer(opening.row);
			if (opening.closesOnChoice) openOn_ = opening.row;
		}
	}
	const char* title = loc::T(dummy_ ? "training.dummy" : "screen.selection");
	menu_.rootName = title;
	menu_.exitName = embedded.exitName;
	menu_.shortcutHints = embedded.shortcutHints;
	PlayerPick pick = dummy_ ? CurrentDummy() : CurrentPick();
	const std::vector<StageOption> stageOptions = StageOptions(unavailableStages);
	const std::vector<MenuEntry> rows = Rows(pick, editable, !dummy_, stageEditable && !dummy_, stageOptions, tinyAllowed && !dummy_);
	const char* status = loc::T(editable ? "selection.status_adjust" : "selection.locked_detail");
	// The pane beside the rows is the pick's preview, as wide as the fighter's
	// banner. The choice dialog draws an option's detail under its list,
	// which a long list (45 fighters, a costume's 17 colours) pushes below a
	// 540-line screen, so the pane says it too.
	menu_.customDetail = true;
	menu_.paneWidth = kBannerWidth;
	// The navigation as the menu holds it while it draws the pane, at most a
	// frame behind the choice list.
	const sf4e::ui::GameMenu::Detail preview = [this, &pick, &rows, &stageOptions](const std::string& focus) {
		std::optional<OpenChoice> choice;
		if (menu_.navigation.Choosing()) choice = OpenChoice{menu_.navigation.DialogId(), menu_.navigation.ChoiceIndex()};
		DrawPickPreview(PreviewOf(pick, focus, choice, rows, stageOptions));
	};
	const MenuAction action = menu_.Draw(title, rows, error.empty() ? status : error.c_str(), preview, 1,
		{}, {}, false, 100, error.empty() ? sf4e::ui::Tone::Neutral : sf4e::ui::Tone::Error);
	if (action.kind == MenuAction::Close || action.kind == MenuAction::Shortcut) sf4e::ui::ForwardMenuAction(action);
	if (action.kind == MenuAction::Activate && action.id == kDone) sf4e::ui::ForwardMenuAction({MenuAction::Close});
	if (!editable || (action.id == kStage && (!stageEditable || dummy_)) || !Apply(action, pick, stageOptions)) return;
	if (dummy_) SetDummy(pick);
	else SetPick(pick);
	// Opened for this one change, a choice made is it: back where it came from.
	if (action.kind == MenuAction::Chosen && action.id == openOn_) sf4e::ui::ForwardMenuAction({MenuAction::Close});
}

PreviewFocus SfvSelectionScreen::FocusOf(const std::string& rowId)
{
	if (rowId == kStage) return PreviewFocus::Stage;
	if (PartOfRow(rowId)) return PreviewFocus::Loadout;
	return PreviewFocus::Look;
}

PlayerPick SfvSelectionScreen::Previewed(const PlayerPick& pick, const std::string& rowId, const std::string& optionId)
{
	PlayerPick shown = pick;
	const std::optional<int> option = OptionOf(optionId);
	if (!option) return shown;
	sf4e::selection::Pick& look = shown.pick;
	if (const std::optional<picks::LoadoutPart> part = PartOfRow(rowId)) {
		if (Contains(PartNumbers(look.fighter, *part), *option)) {
			picks::Loadout loadout = picks::LoadoutOf(look);
			loadout.choice[*part] = (std::uint8_t)*option;
			picks::SetLoadout(look, loadout);
		}
	} else if (rowId == kFighter && ember::ValidFighter(*option)) {
		// The fighter as choosing it would store the pick (SfvCatalog's
		// FitLookWithin): in a look this player can ready, or for one they
		// cannot play, as the spec fits it.
		look.fighter = *option;
		FitLookWithin(look);
	} else if (rowId == kCostume && Contains(CostumeIds(look.fighter, false), *option)) {
		// A costume this player can ready as choosing it would store it; one
		// they cannot is shown as it is, in the colour the spec fits to it.
		look.costume = *option;
		const bool ready = Contains(CostumeIds(look.fighter, true), *option);
		ready ? FitLookWithin(look) : FitToSpec(look);
	} else if (rowId == kColour && Contains(ColourIds(look.fighter, look.costume), *option)) {
		look.color = *option;
	} else if (rowId == kSize && Contains(SizeIds(), *option)) {
		look.size = *option;
	} else if (rowId == kStage) {
		shown.stage = *option;
	}
	return shown;
}

PickPreview SfvSelectionScreen::PreviewOf(const PlayerPick& pick, const std::string& focus, const std::optional<OpenChoice>& choice,
	const std::vector<MenuEntry>& rows, const std::vector<StageOption>& stageOptions)
{
	PickPreview preview;
	// With a choice open, its row and its focused option, the index checked
	// against this frame's rows. A fighter, costume or colour that cannot be
	// chosen says why (NOT OWNED); a part's choice's detail is its move,
	// which the pane's loadout lines already show; a stage's is below.
	std::string rowId = focus, optionId;
	if (choice) {
		rowId = choice->row;
		for (const MenuEntry& row : rows) {
			if (row.id != rowId || choice->option >= row.choices.size()) continue;
			const sf4e::ui::MenuChoice& option = row.choices[choice->option];
			optionId = option.id;
			if (!option.enabled && FocusOf(rowId) == PreviewFocus::Look) preview.note = option.detail;
		}
	}
	preview.pick = Previewed(pick, rowId, optionId);
	preview.focus = FocusOf(rowId);
	if (const std::optional<picks::LoadoutPart> part = PartOfRow(rowId)) preview.part = *part;
	for (const StageOption& option : stageOptions) {
		if (option.id == preview.pick.stage) preview.stageShared = option.offer != StageOffer::OpponentLacks;
	}
	// A stage the opponent lacks says so whenever the pane shows it: the
	// stored stage with the row focused, as an option with its choice open.
	if (preview.focus == PreviewFocus::Stage && !preview.stageShared) preview.note = loc::T(kStageNotShared);
	return preview;
}

} // namespace sfve::adapter
