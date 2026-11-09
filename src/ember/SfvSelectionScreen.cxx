#include "ember/SfvSelectionScreen.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/MenuRows.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

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
constexpr const char* kStage = "stage";
constexpr const char* kDone = "done";

// The fighter's banner (CharaSelectIcon_l) is 380x258 and a stage's picture
// (StageSelectBG) 2048x512: the shapes their frames keep while they load.
constexpr float kBannerWidth = 380;
constexpr float kBannerShape = 258.f / 380.f;
constexpr float kStageShape = 512.f / 2048.f;

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

// A value row Left and Right step; with `choices`, Select opens them all.
MenuEntry StepRow(const char* id, const char* label, std::string value, const char* detail, bool enabled,
	std::vector<MenuChoice> choices = {}, int chosen = -1)
{
	MenuEntry row = sf4e::ui::Value(id, loc::T(label), std::move(value), loc::T(detail), enabled);
	row.choices = std::move(choices);
	if (chosen >= 0) row.chosen = OptionId(chosen);
	return row;
}

ImU32 Rgb(std::uint32_t rgb)
{
	return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF);
}

// A picture `width` wide at its own shape from `at`, or an empty frame of the
// shape it is expected to have while it loads; the height it takes, none
// where the game has no picture.
float DrawPicture(ImDrawList* draw, const sf4e::ui::SelectionImage& image, ImVec2 at, float width, float expectedShape)
{
	if (image.missing) return 0;
	const float s = sf4e::ui::Scale();
	const float height = image.width > 0 ? width * image.height / image.width : width * expectedShape;
	const ImVec2 end(at.x + width, at.y + height);
	if (image.texture) draw->AddImage(image.texture, at, end, image.uvMin, image.uvMax);
	else draw->AddRect(at, end, sf4e::ui::v2::Colors().border, 4 * s, 0, s);
	return height;
}

} // namespace

std::vector<MenuEntry> SfvSelectionScreen::Rows(const PlayerPick& pick, bool editable, bool stageEditable,
	const std::vector<int>& stageChoices) const
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
	std::vector<MenuChoice> stages;
	for (int id : stageChoices) stages.push_back({OptionId(id), sf4e::ui::StageLabel(id), {}, editable && stageEditable});
	std::vector<MenuEntry> rows;
	rows.push_back(StepRow(kFighter, "selection.fighter", catalog.fighterName(pick.pick.fighter),
		editable ? "selection.select_saves" : "selection.locked_detail", editable, std::move(fighters), pick.pick.fighter));
	rows.push_back(StepRow(kCostume, "selection.costume", CostumeName(pick.pick.fighter, pick.pick.costume), adjusts, editable,
		std::move(costumes), pick.pick.costume));
	rows.push_back(StepRow(kColour, "selection.color_row", ColourName(pick.pick.color), adjusts, editable, std::move(colours),
		pick.pick.color));
	if (!dummy_) {
		rows.push_back(StepRow(kStage, "selection.stage", sf4e::ui::StageLabel(pick.stage),
			!editable ? "selection.locked_detail" : stageEditable ? "selection.p1_stage" : "selection.only_p1_stage",
			editable && stageEditable, std::move(stages), pick.stage));
	}
	rows.push_back(sf4e::ui::Row(kDone, loc::T("keyboard.done"), loc::T("selection.status_adjust")));
	return rows;
}

bool SfvSelectionScreen::Apply(const MenuAction& action, PlayerPick& pick, const std::vector<int>& stageChoices)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const PlayerPick before = pick;
	// Left and Right go through what this copy's player can ready: the
	// fighters they can play, the fighter's costumes with a look they can
	// ready, and that costume's colours making one. A costume or colour the
	// new fighter or costume lacks, or that is not theirs, is fitted as the
	// pick is stored (SetPick). A choice they cannot ready is refused here
	// too, whatever the list let through.
	if (action.kind == MenuAction::Adjust) {
		if (action.id == kFighter) sf4e::ui::Step(pick.pick.fighter, PlayableRoster(), action.delta);
		else if (action.id == kCostume) sf4e::ui::Step(pick.pick.costume, CostumeIds(pick.pick.fighter, true), action.delta);
		else if (action.id == kColour) {
			sf4e::ui::Step(pick.pick.color, ColourIds(pick.pick.fighter, pick.pick.costume, true), action.delta);
		} else if (action.id == kStage) sf4e::ui::Step(pick.stage, stageChoices, action.delta);
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
	}
	return pick.pick.fighter != before.pick.fighter || pick.pick.costume != before.pick.costume ||
		pick.pick.color != before.pick.color || pick.stage != before.stage;
}

void SfvSelectionScreen::Draw(bool editable, bool stageEditable, const std::string& error,
	const std::vector<std::int64_t>& unavailableStages)
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
	const char* title = loc::T(dummy_ ? "training.dummy" : "screen.selection");
	menu_.rootName = title;
	menu_.exitName = embedded.exitName;
	menu_.shortcutHints = embedded.shortcutHints;
	PlayerPick pick = dummy_ ? CurrentDummy() : CurrentPick();
	const std::vector<int> stageChoices = StageChoices(unavailableStages);
	const std::vector<MenuEntry> rows = Rows(pick, editable, stageEditable && !dummy_, stageChoices);
	const char* status = loc::T(editable ? "selection.status_adjust" : "selection.locked_detail");
	// The pane beside the rows is the pick's preview, as wide as the fighter's
	// banner. The choice dialog draws an option's detail under its list,
	// which a long list (45 fighters, a costume's 17 colours) pushes below a
	// 540-line screen, so the pane says it too.
	menu_.customDetail = true;
	menu_.paneWidth = kBannerWidth;
	const std::string note = FocusedChoiceNote(rows);
	const sf4e::ui::GameMenu::Detail preview = [&pick, &note](const std::string& focus) { DrawPreview(pick, focus, note); };
	const MenuAction action = menu_.Draw(title, rows, error.empty() ? status : error.c_str(), preview, 1,
		{}, {}, false, 100, error.empty() ? sf4e::ui::Tone::Neutral : sf4e::ui::Tone::Error);
	if (action.kind == MenuAction::Close || action.kind == MenuAction::Shortcut) sf4e::ui::ForwardMenuAction(action);
	if (action.kind == MenuAction::Activate && action.id == kDone) sf4e::ui::ForwardMenuAction({MenuAction::Close});
	if (!editable || (action.id == kStage && (!stageEditable || dummy_)) || !Apply(action, pick, stageChoices)) return;
	if (dummy_) SetDummy(pick);
	else SetPick(pick);
	// Opened for this one change, a choice made is it: back where it came from.
	if (action.kind == MenuAction::Chosen && action.id == openOn_) sf4e::ui::ForwardMenuAction({MenuAction::Close});
}

std::string SfvSelectionScreen::FocusedChoiceNote(const std::vector<MenuEntry>& rows) const
{
	const auto& nav = menu_.navigation;
	if (!nav.Choosing()) return {};
	for (const MenuEntry& row : rows) {
		if (row.id == nav.DialogId() && nav.ChoiceIndex() < row.choices.size()) return row.choices[nav.ChoiceIndex()].detail;
	}
	return {};
}

void SfvSelectionScreen::DrawPreview(const PlayerPick& pick, const std::string& focus, const std::string& note)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const auto& colors = sf4e::ui::v2::Colors();
	const float s = sf4e::ui::Scale(), gap = 10 * s, line = ImGui::GetTextLineHeightWithSpacing();
	ImVec2 at = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	if (!note.empty()) {
		at.y += sf4e::ui::v2::Paragraph(draw, at, width, note, ImGui::GetFontSize(), colors.danger) + gap;
	}
	if (focus == kStage) {
		// Random has no picture: StageImage reports it missing.
		at.y += DrawPicture(draw, StageImage(pick.stage), at, width, kStageShape) + gap;
		draw->AddText(at, colors.text, sf4e::ui::StageLabel(pick.stage).c_str());
		return;
	}
	at.y += DrawPicture(draw, FighterImage(pick.pick.fighter, FighterPicture::Banner), at, width, kBannerShape) + gap;
	draw->AddText(at, colors.text, ember::Catalog().fighterName(pick.pick.fighter));
	at.y += line;
	draw->AddText(at, colors.muted, CostumeName(pick.pick.fighter, pick.pick.costume).c_str());
	at.y += line;
	// The colour by the costume menu's label, then its two swatch colours.
	const std::string colour = std::string(loc::T("selection.color_row")) + " " + ColourName(pick.pick.color);
	draw->AddText(at, colors.muted, colour.c_str());
	const auto swatch = Swatch(pick.pick.fighter, pick.pick.costume, pick.pick.color);
	if (!swatch) return;
	const float side = ImGui::GetTextLineHeight();
	float x = at.x + ImGui::CalcTextSize(colour.c_str()).x + gap;
	for (const std::uint32_t rgb : {swatch->first, swatch->second}) {
		draw->AddRectFilled(ImVec2(x, at.y), ImVec2(x + side, at.y + side), Rgb(rgb), 3 * s);
		draw->AddRect(ImVec2(x, at.y), ImVec2(x + side, at.y + side), colors.border, 3 * s, 0, s);
		x += side + 4 * s;
	}
}

} // namespace sfve::adapter
