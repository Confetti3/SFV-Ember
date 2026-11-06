#include "ember/SfvSelectionScreen.hxx"

#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/MenuRows.hxx"

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
constexpr const char* kStage = "stage";
constexpr const char* kDone = "done";

std::string OptionId(int id)
{
	return "#" + std::to_string(id);
}

std::optional<int> OptionOf(const std::string& option)
{
	if (option.size() < 2 || option[0] != '#') return std::nullopt;
	return std::atoi(option.c_str() + 1);
}

// The roster in the game's select-screen order (the catalog's rosterOrder).
std::vector<int> Roster()
{
	const ember::GameCatalog& catalog = ember::Catalog();
	return std::vector<int>(catalog.rosterOrder, catalog.rosterOrder + catalog.fighterCount);
}

std::vector<int> Stages()
{
	std::vector<int> stages((size_t)StageCount());
	for (int id = 0; id < StageCount(); id++) stages[(size_t)id] = id;
	return stages;
}

std::vector<int> Numbers(int limit)
{
	std::vector<int> numbers((size_t)limit);
	for (int n = 0; n < limit; n++) numbers[(size_t)n] = n;
	return numbers;
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

} // namespace

std::vector<MenuEntry> SfvSelectionScreen::Rows(const PlayerPick& pick, bool editable, bool stageEditable) const
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const char* adjusts = editable ? "selection.adjust_saves" : "selection.locked_detail";
	std::vector<MenuChoice> fighters;
	for (int id : Roster()) fighters.push_back({OptionId(id), catalog.fighterName(id), {}, editable});
	std::vector<MenuChoice> stages;
	for (int id : Stages()) stages.push_back({OptionId(id), StageCode(id), {}, editable && stageEditable});
	std::vector<MenuEntry> rows;
	rows.push_back(StepRow(kFighter, "selection.fighter", catalog.fighterName(pick.pick.fighter),
		editable ? "selection.select_saves" : "selection.locked_detail", editable, std::move(fighters), pick.pick.fighter));
	rows.push_back(StepRow(kCostume, "selection.costume", std::to_string(pick.pick.costume), adjusts, editable));
	rows.push_back(StepRow(kColour, "selection.color_row", std::to_string(pick.pick.color), adjusts, editable));
	rows.push_back(StepRow(kStage, "selection.stage", StageCode(pick.stage),
		!editable ? "selection.locked_detail" : stageEditable ? "selection.p1_stage" : "selection.only_p1_stage",
		editable && stageEditable, std::move(stages), pick.stage));
	rows.push_back(sf4e::ui::Row(kDone, loc::T("keyboard.done"), loc::T("selection.status_adjust")));
	return rows;
}

bool SfvSelectionScreen::Apply(const MenuAction& action, PlayerPick& pick)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const PlayerPick before = pick;
	if (action.kind == MenuAction::Adjust) {
		if (action.id == kFighter) sf4e::ui::Step(pick.pick.fighter, Roster(), action.delta);
		else if (action.id == kCostume) sf4e::ui::Step(pick.pick.costume, Numbers(catalog.costumeLimit), action.delta);
		else if (action.id == kColour) sf4e::ui::Step(pick.pick.color, Numbers(catalog.colorLimit), action.delta);
		else if (action.id == kStage) sf4e::ui::Step(pick.stage, Stages(), action.delta);
	} else if (action.kind == MenuAction::Chosen) {
		const std::optional<int> option = OptionOf(action.text);
		if (option && action.id == kFighter && ember::ValidFighter(*option)) pick.pick.fighter = *option;
		else if (option && action.id == kStage && catalog.validStage(*option)) pick.stage = *option;
	}
	return pick.pick.fighter != before.pick.fighter || pick.pick.costume != before.pick.costume ||
		pick.pick.color != before.pick.color || pick.stage != before.stage;
}

void SfvSelectionScreen::Draw(bool editable, bool stageEditable, const std::string& error)
{
	auto& nav = menu_.navigation;
	// The shell says where Back goes and which of its shortcuts this screen
	// hands back; one it has just opened starts on its first row, or on the
	// row it was opened for.
	const sf4e::ui::EmbeddedReturn& embedded = sf4e::ui::EmbeddedReturnContext();
	if (sf4e::ui::TakeEmbeddedFresh()) {
		nav.Home();
		using sf4e::ui::SelectionStart;
		const SelectionStart openOn = embedded.openOn;
		openOn_ = openOn == SelectionStart::Roster ? kFighter
			: openOn == SelectionStart::Stage      ? kStage
			: openOn == SelectionStart::Costumes   ? kCostume
												   : "";
		if (!openOn_.empty()) nav.Prefer(openOn_);
	}
	menu_.rootName = loc::T("screen.selection");
	menu_.exitName = embedded.exitName;
	menu_.shortcutHints = embedded.shortcutHints;
	PlayerPick pick = CurrentPick();
	const std::vector<MenuEntry> rows = Rows(pick, editable, stageEditable);
	const char* status = loc::T(editable ? "selection.status_adjust" : "selection.locked_detail");
	const MenuAction action = menu_.Draw(loc::T("screen.selection"), rows, error.empty() ? status : error.c_str(), {}, 1, {},
		{}, false, 100, error.empty() ? sf4e::ui::Tone::Neutral : sf4e::ui::Tone::Error);
	if (action.kind == MenuAction::Close || action.kind == MenuAction::Shortcut) sf4e::ui::ForwardMenuAction(action);
	if (action.kind == MenuAction::Activate && action.id == kDone) sf4e::ui::ForwardMenuAction({MenuAction::Close});
	if (!editable || (action.id == kStage && !stageEditable) || !Apply(action, pick)) return;
	SetPick(pick);
	// Opened for this one change, a choice made is it: back where it came from.
	if (action.kind == MenuAction::Chosen && action.id == openOn_) sf4e::ui::ForwardMenuAction({MenuAction::Close});
}

} // namespace sfve::adapter
