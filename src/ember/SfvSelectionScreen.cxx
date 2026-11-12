#include "ember/SfvSelectionScreen.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvLoadoutNotes.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelection.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "ui/AppearanceGrid.hxx"
#include "ui/MenuRows.hxx"
#include "ui/RoomCards.hxx"
#include "ui/RowId.hxx"
#include "ui/SelectionLabels.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace sfve::adapter {

namespace {

using sf4e::ui::MenuAction;
using sf4e::ui::MenuChoice;
using sf4e::ui::MenuEntry;
using sf4e::ui::MenuNavigation;
using sf4e::ui::SelectionStart;
namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;

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

// The cards' ids: a prefix and the card's number (RowKey reads it back).
constexpr const char* kFighterCard = "fighter-";
constexpr const char* kRandomCard = "random-fighter";
constexpr const char* kElevenCard = "eleven-fighter";
constexpr const char* kCostumeCard = "costume-";
constexpr const char* kColourCard = "color-";
constexpr const char* kStageCard = "stage-";

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

// The board as the game's own select screen draws it at 1080 lines, measured
// on its pixels (the laptop's capture, docs/design/beta8/select.md): tiles 70
// px apart along a row, each a parallelogram frame 64 px wide and 142 tall
// leaning 0.2 px sideways a line (the left side's foot to the right, the
// right side's mirrored), rows 151 px apart. The tile's picture is the
// select grid face (CharaSelectIconLeft) at its own 116x144 on the frame's
// centre. The Random tile stands between the two sides from 60 px under the
// first row's top to 47 px above the last row's foot, 6 px clear of the
// tiles beside it. The board is scaled as a whole to the room it has.
constexpr float kTilePitch = 70;
constexpr float kTileWidth = 64;
constexpr float kTileHeight = 142;
constexpr float kRowPitch = 151;
constexpr float kTileLean = .2f;
constexpr float kPictureWidth = 116;
constexpr float kPictureHeight = 144;
constexpr float kRandomTop = 60;
constexpr float kRandomFoot = 47;
constexpr float kRandomClear = 6;
// A cell right of Random's column stands this many pitches further right:
// the room the Random tile takes beyond its own column (0.66 on the game's
// first row, 0.73 on its second).
constexpr float kRandomGap = .69f;
// The saved card's mark under its tile: Band's gold bar, in unscaled pixels.
constexpr float kSavedBar = 3;
// The pages with a body keep a pane on the right where the body is at least
// this wide (unscaled), as GameMenu splits its cards from their detail.
constexpr float kPaneFrom = 820;
constexpr float kPaneGap = 24;
// A dimmed card's picture.
constexpr float kLocked = .4f;
// The stage cards (StageSelectIcon_l, 674x238): their picture's height over
// width, and the narrowest and widest they go (unscaled).
constexpr sf4e::ui::CardShape kStageCardShape{238.f / 674.f, 120, 340};
// The colour cards: each the colour's swatch chip, square, as many as fit.
constexpr sf4e::ui::CardShape kColourCardShape{1.f, 48, 110};
// The lines of a loadout card's note: its 140 characters at the card's
// narrowest.
constexpr int kNoteLines = 3;

std::string OptionId(int id)
{
	return "#" + std::to_string(id);
}

std::optional<int> OptionOf(const std::string& option)
{
	if (option.size() < 2 || option[0] != '#') return std::nullopt;
	return std::atoi(option.c_str() + 1);
}

std::string CardId(const char* prefix, int number)
{
	return prefix + std::to_string(number);
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

bool FighterLocked(int fighter)
{
	return ember::ValidFighter(fighter) && ember::Catalog().fighterLocked(fighter, {});
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

// A loadout card's id prefix ("vskill-"), and the part and choice a card id
// names; none for another id.
std::string PartPrefix(picks::LoadoutPart part)
{
	return std::string(part == picks::LoadoutPart::VSkill ? kVSkill : kVTrigger) + "-";
}

struct PartChoice {
	picks::LoadoutPart part;
	int choice;
};
std::optional<PartChoice> PartCardOf(const std::string& id)
{
	for (const LoadoutRow& row : kLoadoutRows) {
		if (const std::optional<int> choice = sf4e::ui::RowKey<int>(id, PartPrefix(row.part))) return PartChoice{row.part, *choice};
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

// The pick with the part's choice `number` stored; true when the loadout
// changed. Only a choice the fighter has is taken.
bool SetPart(picks::LoadoutPart part, int number, sf4e::selection::Pick& pick)
{
	const picks::Loadout before = picks::LoadoutOf(pick);
	if (!Contains(PartNumbers(pick.fighter, part), number)) return false;
	picks::Loadout after = before;
	after.choice[part] = (std::uint8_t)number;
	picks::SetLoadout(pick, after);
	return after != before;
}

// The pick with the part's row's change made (Left and Right, or a choice);
// true when the loadout changed. Only a choice the fighter has is taken.
bool ApplyPart(const MenuAction& action, picks::LoadoutPart part, sf4e::selection::Pick& pick)
{
	const std::vector<int> numbers = PartNumbers(pick.fighter, part);
	int number = picks::LoadoutOf(pick).choice[part];
	if (action.kind == MenuAction::Adjust) sf4e::ui::Step(number, numbers, action.delta);
	else if (action.kind == MenuAction::Chosen) {
		const std::optional<int> option = OptionOf(action.text);
		if (option && Contains(numbers, *option)) number = *option;
	}
	return SetPart(part, number, pick);
}

// The colour a costume's card shows it in, and its colour cards open on: the
// colour worn where the costume has it and this player can ready it, else
// its first they can, else its first (sf4e::ui::GalleryColor).
int CardColour(const sf4e::selection::Pick& pick, int costume)
{
	const std::vector<int> ready = ColourIds(pick.fighter, costume, true);
	return sf4e::ui::GalleryColor(ready.empty() ? ColourIds(pick.fighter, costume) : ready, pick.color);
}

// The card on the board that is the stored pick: Eleven's while Eleven is
// the pick, Random's while Random is, else the fighter's.
std::string SavedCard(const PlayerPick& pick, bool random, bool eleven = false)
{
	if (eleven) return kElevenCard;
	return random ? std::string(kRandomCard) : CardId(kFighterCard, pick.pick.fighter);
}

const MenuEntry* EntryOf(const std::vector<MenuEntry>& entries, const std::string& id)
{
	const auto entry = std::find_if(entries.begin(), entries.end(), [&id](const MenuEntry& e) { return e.id == id; });
	return entry == entries.end() ? nullptr : &*entry;
}

// The width of a body page's pane beside its cards `width` wide; 0 where
// the cards keep the whole width (a narrow window), as GameMenu does.
float PaneWidth(float width)
{
	const float s = sf4e::ui::Scale();
	if (width < kPaneFrom * s) return 0;
	return std::floor((std::min)(kBannerWidth * s, width * .4f));
}

// The pane on its panel ground (GameMenu's detail pane), its contents inset
// as that pane insets them.
void DrawPane(v2::Box pane, const PickPreview& preview)
{
	const float s = sf4e::ui::Scale();
	if (!(pane.W() > 0)) return;
	ImGui::GetWindowDrawList()->AddRectFilled(pane.min, pane.max, v2::PanelGround(), v2::Corner());
	DrawPickPreviewIn(preview, v2::Inset(pane, 16 * s, 16 * s));
}

// The body's area from the cursor, `height` tall, and the pane cut from its
// right where there is room for one.
struct BodyArea {
	ImVec2 origin;
	float width = 0;
	v2::Box cards, pane;
};
BodyArea LayOutBody(float height, bool withPane)
{
	const float s = sf4e::ui::Scale();
	BodyArea area;
	area.origin = ImGui::GetCursorScreenPos();
	area.width = ImGui::GetContentRegionAvail().x;
	area.cards = {area.origin, ImVec2(area.origin.x + area.width, area.origin.y + height)};
	const float pane = withPane ? PaneWidth(area.width) : 0;
	if (pane > 0) area.pane = v2::CutRight(area.cards, pane, kPaneGap * s);
	return area;
}

// --- The board -------------------------------------------------------------

// The board as SFV's select screen lays it out (SfvSelectGrid, and the game's
// measures above): each fighter's slanted tile in its cell, the right side's
// mirrored, the Random tile between the two sides, the stored card with a
// gold foot, a fighter this player cannot play dimmed, the focused one edged
// in gold. The art pane beside it shows the focused card (BoardPick, or
// Random's own words). The pad moves by where each tile is drawn (PressEntry
// places it): along a row, and up and down to the tile nearest below or
// above; Random's Left and Right are the tiles beside it on its row.
struct BoardFrame {
	const grid::SelectGrid& table; // the grid table, fitted to the roster
	const PlayerPick& pick;
	bool random = false;     // Random is the stored pick
	bool remembered = false; // the player's screen: fighters in their remembered looks
	bool fighterKept = false;
	bool eleven = false;     // the stored pick is Eleven (and `random` is set as well)
};

// A tile's frame: a parallelogram around `centre`, `scale` times the game's
// 1080-line measures, leaning with `side` (1 the left side's lean, -1 the
// right side's).
struct Frame {
	ImVec2 topLeft, topRight, footRight, footLeft;
};
Frame FrameAt(ImVec2 centre, float scale, float side)
{
	const float w = kTileWidth * .5f * scale, h = kTileHeight * .5f * scale, lean = kTileLean * h * side;
	return {ImVec2(centre.x - w - lean, centre.y - h), ImVec2(centre.x + w - lean, centre.y - h),
		ImVec2(centre.x + w + lean, centre.y + h), ImVec2(centre.x - w + lean, centre.y + h)};
}

void DrawFrameEdge(ImDrawList* draw, const Frame& f, ImU32 colour, float thickness)
{
	draw->AddQuad(f.topLeft, f.topRight, f.footRight, f.footLeft, colour, thickness);
}

// `eleven`: the game's ELEVEN's tile (no fighter of the catalog; `fighter` is
// unused): its own pictures, and never dimmed.
void DrawTile(ImDrawList* draw, const MenuEntry& e, ImVec2 centre, float scale, float side, bool focused, bool hovered,
	bool saved, int fighter, bool eleven = false)
{
	const auto& colors = v2::Colors();
	const float s = sf4e::ui::Scale();
	const Frame frame = FrameAt(centre, scale, side);
	// The tile is the game's own card: no ground of Ember's at rest, the
	// focus's and the pointer's under it.
	if (focused || hovered) {
		draw->AddQuadFilled(frame.topLeft, frame.topRight, frame.footRight, frame.footLeft, focused ? colors.header : colors.hover);
	}
	const float alpha = !eleven && FighterLocked(fighter) ? kLocked : 1.f;
	const sf4e::ui::SelectionImage tile = eleven ? ElevenImage(FighterPicture::Tile) : FighterImage(fighter, FighterPicture::Tile);
	if (tile.texture && tile.width > 0 && tile.height > 0) {
		// At its own shape on the frame's centre; mirrored on the right side,
		// where the game draws the face's Right picture.
		const float tall = std::floor(kPictureHeight * scale), wide = std::floor(tall * (float)tile.width / (float)tile.height);
		ImVec2 uvA = tile.uvMin, uvB = tile.uvMax;
		if (side < 0) std::swap(uvA.x, uvB.x);
		const ImVec2 corner(std::floor(centre.x - wide * .5f), std::floor(centre.y - tall * .5f));
		draw->AddImage(tile.texture, corner, ImVec2(corner.x + wide, corner.y + tall), uvA, uvB,
			IM_COL32(255, 255, 255, (int)(alpha * 255 + .5f)));
	} else if (tile.missing) {
		// The head's square, else the fighter's name, upright in the frame.
		const v2::Box box{ImVec2(centre.x - kTileWidth * .5f * scale, centre.y - kTileHeight * .5f * scale),
			ImVec2(centre.x + kTileWidth * .5f * scale, centre.y + kTileHeight * .5f * scale)};
		if (!DrawFitted(draw, eleven ? ElevenImage(FighterPicture::Face) : FighterImage(fighter, FighterPicture::Face), box, alpha, false)) {
			std::string elided;
			sf4e::ui::DrawCardText(elided, ImVec2(box.min.x + 2 * s, centre.y - ImGui::GetFontSize() * .5f), box.W() - 4 * s, e.label,
				(std::min)(ImGui::GetFontSize(), 13 * s), colors.muted, true);
		}
	}
	// The frame's edge, as the game edges its tiles; the stored card's gold
	// foot; the focus's gold edge.
	DrawFrameEdge(draw, frame, v2::color::Alpha(colors.border, alpha), (std::max)(1.f, s));
	if (saved) draw->AddLine(frame.footLeft, frame.footRight, colors.accent, (std::max)(1.f, std::floor(kSavedBar * s + .5f)));
	if (focused) DrawFrameEdge(draw, frame, colors.accent, (std::max)(1.f, 2 * s));
}

// The Random tile: the game's "?" plate between the two sides.
void DrawRandomTile(ImDrawList* draw, const Frame& quad, bool focused, bool hovered, bool saved)
{
	const auto& colors = v2::Colors();
	const float s = sf4e::ui::Scale();
	draw->AddQuadFilled(quad.topLeft, quad.topRight, quad.footRight, quad.footLeft,
		focused ? colors.header : hovered ? colors.hover : colors.frame);
	DrawFrameEdge(draw, quad, colors.border, (std::max)(1.f, s));
	const float wide = ((quad.topRight.x - quad.topLeft.x) + (quad.footRight.x - quad.footLeft.x)) * .5f;
	const float top = quad.topLeft.y, foot = quad.footLeft.y;
	const float middle = (quad.topLeft.x + quad.topRight.x + quad.footLeft.x + quad.footRight.x) * .25f;
	DrawRandomMark(draw, {ImVec2(middle - wide * .5f, top), ImVec2(middle + wide * .5f, foot)}, saved ? colors.accent : colors.text);
	if (saved) draw->AddLine(quad.footLeft, quad.footRight, colors.accent, (std::max)(1.f, std::floor(kSavedBar * s + .5f)));
	if (focused) DrawFrameEdge(draw, quad, colors.accent, (std::max)(1.f, 2 * s));
}

void DrawBoard(const std::vector<MenuEntry>& entries, MenuNavigation& nav, MenuAction& action, float height, const BoardFrame& frame)
{
	const BodyArea area = LayOutBody(height, true);
	const grid::SelectGrid& table = frame.table;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const int rows = (std::max)(1, grid::RowCount(table));
	// The board in the game's 1080-line pixels: x from row 0's column 0's
	// centre, y from the first row's top.
	const auto across = [&table](grid::Cell cell) { return SfvSelectionScreen::BoardColumn(table, cell) * kTilePitch; };
	const auto middle = [](int row) { return (float)row * kRowPitch + kTileHeight * .5f; };
	const auto side = [&table](grid::Cell cell) { return table.random && cell.column > table.random->column ? -1.f : 1.f; };
	// Random's plate: its top between the tiles beside it on its own row, its
	// foot between those on the last row, each edge where those tiles' frames
	// are at that line, kept clear of them.
	std::optional<Frame> plate;
	if (table.random) {
		const int column = table.random->column;
		const auto edges = [&](int row, float y) {
			const std::optional<std::string> at = grid::CodeAt(table, {column, row});
			const int left = at && *at != grid::kRandomCellCode ? column : column - 1;
			const float leftEdge = across({left, row}) + kTileWidth * .5f + kTileLean * (y - middle(row)) + kRandomClear;
			const float rightEdge = across({column + 1, row}) - kTileWidth * .5f - kTileLean * (y - middle(row)) - kRandomClear;
			return std::pair<float, float>(leftEdge, (std::max)(leftEdge, rightEdge));
		};
		const float top = (float)table.random->row * kRowPitch + kRandomTop;
		const float foot = (float)(rows - 1) * kRowPitch + kTileHeight - kRandomFoot;
		const auto [topLeft, topRight] = edges(table.random->row, top);
		const float low = (std::max)(top, foot);
		const auto [footLeft, footRight] = edges(rows - 1, low);
		plate = Frame{ImVec2(topLeft, top), ImVec2(topRight, top), ImVec2(footRight, low), ImVec2(footLeft, low)};
	}
	// Scaled as a whole to the area, centred on the Random tile as the game
	// centres it (else on the tiles), every picture inside.
	float least = 0, most = 0;
	bool any = false;
	for (const grid::Entry& entry : table.fighters) {
		const float x = across(entry.cell);
		least = any ? (std::min)(least, x) : x;
		most = any ? (std::max)(most, x) : x;
		any = true;
	}
	if (table.eleven) {
		const float x = across(*table.eleven);
		least = any ? (std::min)(least, x) : x;
		most = any ? (std::max)(most, x) : x;
		any = true;
	}
	const float centre =
		plate ? (plate->topLeft.x + plate->topRight.x + plate->footLeft.x + plate->footRight.x) * .25f : (least + most) * .5f;
	const float reach = (std::max)(std::fabs(least - centre), std::fabs(most - centre)) + kPictureWidth * .5f;
	const float boardHeight = (float)(rows - 1) * kRowPitch + kPictureHeight;
	const float scale = (std::max)(.05f, (std::min)(area.cards.W() / (2 * reach), area.cards.H() / boardHeight));
	const float originX = std::floor(area.cards.min.x + area.cards.W() * .5f);
	const float originY =
		std::floor(area.cards.min.y + (area.cards.H() - boardHeight * scale) * .5f + (kPictureHeight - kTileHeight) * .5f * scale);
	const auto screen = [&](float x, float y) { return ImVec2(std::floor(originX + (x - centre) * scale), std::floor(originY + y * scale)); };
	for (const MenuEntry& e : entries) {
		if (e.id == kRandomCard) {
			if (!plate) continue;
			const Frame q{screen(plate->topLeft.x, plate->topLeft.y), screen(plate->topRight.x, plate->topRight.y),
				screen(plate->footRight.x, plate->footRight.y), screen(plate->footLeft.x, plate->footLeft.y)};
			// Its box is its narrower foot's width, so it never covers the
			// tiles beside it; at least a third of a tile wide.
			const float half = (std::max)((q.footRight.x - q.footLeft.x) * .5f, kTileWidth * scale / 6);
			const float mid = (q.footLeft.x + q.footRight.x) * .5f;
			const v2::Box b{ImVec2(mid - half, q.topLeft.y), ImVec2(mid + half, q.footLeft.y)};
			const bool hovered = sf4e::ui::PressEntry(nav, entries, e, b, action).hovered;
			DrawRandomTile(draw, q, e.id == nav.Focus(), hovered, frame.random && !frame.eleven);
			continue;
		}
		if (e.id == kElevenCard) {
			// The game's ELEVEN, in its own cell (the table's `eleven`).
			if (!table.eleven) continue;
			const ImVec2 centreAt = screen(across(*table.eleven), middle(table.eleven->row));
			const float w = std::floor(kTileWidth * .5f * scale), h = std::floor(kTileHeight * .5f * scale);
			const v2::Box b{ImVec2(centreAt.x - w, centreAt.y - h), ImVec2(centreAt.x + w, centreAt.y + h)};
			const bool hovered = sf4e::ui::PressEntry(nav, entries, e, b, action).hovered, focused = e.id == nav.Focus();
			DrawTile(draw, e, centreAt, scale, side(*table.eleven), focused, hovered, frame.eleven, -1, true);
			continue;
		}
		const std::optional<int> fighter = sf4e::ui::RowKey<int>(e.id, kFighterCard);
		const std::optional<grid::Cell> cell = fighter ? grid::CellOf(table, FighterCode(*fighter)) : std::nullopt;
		if (!cell) continue;
		const ImVec2 centreAt = screen(across(*cell), middle(cell->row));
		const float w = std::floor(kTileWidth * .5f * scale), h = std::floor(kTileHeight * .5f * scale);
		const v2::Box b{ImVec2(centreAt.x - w, centreAt.y - h), ImVec2(centreAt.x + w, centreAt.y + h)};
		const bool hovered = sf4e::ui::PressEntry(nav, entries, e, b, action).hovered, focused = e.id == nav.Focus();
		const bool saved = !frame.random && *fighter == frame.pick.pick.fighter;
		DrawTile(draw, e, centreAt, scale, side(*cell), focused, hovered, saved, *fighter);
	}
	// The art pane: the focused card.
	if (const MenuEntry* focused = EntryOf(entries, nav.Focus())) {
		PickPreview preview;
		preview.pick = SfvSelectionScreen::BoardPick(frame.pick, focused->id, frame.remembered);
		if (focused->id == kRandomCard) {
			preview.focus = PreviewFocus::Random;
			preview.randomSaved = frame.random && !frame.eleven;
		} else if (focused->id == kElevenCard) {
			preview.focus = PreviewFocus::Eleven;
			preview.randomSaved = frame.eleven;
		} else if (const std::optional<int> fighter = sf4e::ui::RowKey<int>(focused->id, kFighterCard)) {
			if (FighterLocked(*fighter)) preview.note = loc::T("selection.not_owned");
			else if (frame.fighterKept && *fighter != frame.pick.pick.fighter) preview.note = loc::T("selection.winner_keeps_fighter");
		}
		DrawPane(area.pane, preview);
	}
	ImGui::SetCursorScreenPos(area.origin);
	ImGui::Dummy(ImVec2(area.width, 0));
}

// --- Picture cards: costumes, colours, stages --------------------------------

// A grid of picture cards (the library's FitGallery, as SF4 Ember's
// galleries lay theirs out) in a scrolling child of its own, the pane beside
// it: each card its picture over its name, the saved one with the gold rule,
// one that cannot be chosen dimmed and its name muted.
struct CardGrid {
	const char* page; // the scrolling child's name: each page keeps its own scroll
	sf4e::ui::CardShape shape;
	std::function<void(ImDrawList*, const MenuEntry&, v2::Box, float alpha)> picture;
	std::function<bool(const MenuEntry&)> saved;
	std::function<PickPreview(const MenuEntry&)> preview;
};

void DrawCardGrid(const std::vector<MenuEntry>& entries, MenuNavigation& nav, MenuAction& action, float height, bool changed,
	const CardGrid& cards)
{
	const float s = sf4e::ui::Scale(), gap = 8 * s;
	const auto& colors = v2::Colors();
	const BodyArea area = LayOutBody(height, true);
	const float font = (std::min)(ImGui::GetFontSize(), 15 * s), caption = font + 8 * s;
	ImGui::SetCursorScreenPos(area.cards.min);
	ImGui::BeginChild(cards.page, ImVec2(area.cards.W(), area.cards.H()), 0, ImGuiWindowFlags_NoNavInputs);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float width = ImGui::GetContentRegionAvail().x;
	const sf4e::ui::GalleryGrid grid = sf4e::ui::FitGallery(entries.size(), width, area.cards.H(), gap, cards.shape.aspect, caption,
		cards.shape.narrowest * s, cards.shape.widest * s);
	const ImVec2 top = ImGui::GetCursorScreenPos();
	for (size_t i = 0; i < entries.size(); i++) {
		const MenuEntry& e = entries[i];
		const sf4e::ui::GalleryGrid::Offset at = grid.Cell(i, width, gap);
		const v2::Box b{ImVec2(top.x + at.x, top.y + at.y), ImVec2(top.x + at.x + grid.width, top.y + at.y + grid.height)};
		const bool hovered = sf4e::ui::PressEntry(nav, entries, e, b, action, changed).hovered, focused = e.id == nav.Focus();
		const bool saved = cards.saved(e);
		v2::CardGround(draw, b, focused, hovered, saved);
		cards.picture(draw, e, {ImVec2(b.min.x + 3 * s, b.min.y + 3 * s), ImVec2(b.max.x - 3 * s, b.max.y - caption)},
			e.enabled ? 1.f : kLocked);
		draw->AddRectFilled(ImVec2(b.min.x, b.max.y - caption), b.max, v2::color::Alpha(colors.ink, .85f));
		std::string elided;
		sf4e::ui::DrawCardText(elided, ImVec2(b.min.x + 4 * s, b.max.y - caption + 4 * s), grid.width - 8 * s, e.label, font,
			saved ? colors.accent : e.enabled ? colors.text : colors.muted, true);
		if (focused) v2::Focus(draw, b.min, b.max, false);
	}
	// The grid's whole height, so a grid taller than the area scrolls.
	ImGui::SetCursorScreenPos(top);
	ImGui::Dummy(ImVec2(width, (std::max)(0.f, (float)grid.Lines(entries.size()) * (grid.height + gap) - gap)));
	ImGui::EndChild();
	if (const MenuEntry* focused = EntryOf(entries, nav.Focus())) DrawPane(area.pane, cards.preview(*focused));
	ImGui::SetCursorScreenPos(area.origin);
	ImGui::Dummy(ImVec2(area.width, 0));
}

// --- The loadout cards -------------------------------------------------------

// Each V-Skill and V-Trigger choice the fighter has as a card in the
// library's Ultra card shape (16:9 picture, as wide as three across a 1080p
// screen let it be): the captured picture of the move (else the fighter in
// their look), the game's I or II mark top right, SAVED top left on the
// stored choice; under it the part and numeral, the game's name for the
// move, and what it does. V-Skills on the first line, V-Triggers on the
// second, I on the left and II on the right, so a choice a fighter lacks
// leaves its place empty.
void DrawLoadoutCards(const std::vector<MenuEntry>& entries, MenuNavigation& nav, MenuAction& action, float height, bool changed,
	const PlayerPick& pick)
{
	const float s = sf4e::ui::Scale(), gap = 14 * s, pad = 12 * s, corner = 6 * s;
	const auto& colors = v2::Colors();
	const float titleFont = (std::min)(ImGui::GetFontSize() * 1.1f, 20 * s), font = (std::min)(ImGui::GetFontSize(), 15 * s);
	const float line = font + 6 * s, titleLine = titleFont + 10 * s;
	const float caption = pad + line + titleLine + (float)kNoteLines * line + pad;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const sf4e::ui::CardShape shape = sf4e::ui::UltraCard;
	const float across = (width - gap) * .5f, down = (height - gap) * .5f;
	float cardWidth = (std::min)({shape.widest * s, across, (down - caption) / shape.aspect});
	if (cardWidth < shape.narrowest * s) cardWidth = (std::min)(shape.narrowest * s, across);
	cardWidth = std::floor(cardWidth);
	const float cardHeight = std::floor(cardWidth * shape.aspect + caption), whole = 2 * cardHeight + gap;
	const float left = std::floor(origin.x + (width - (2 * cardWidth + gap)) * .5f);
	const float drop = std::floor((std::max)(0.f, (height - whole) * .5f));
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const picks::Loadout loadout = picks::LoadoutOf(pick.pick);
	for (const MenuEntry& e : entries) {
		const std::optional<PartChoice> card = PartCardOf(e.id);
		if (!card) continue;
		const float x = left + (float)card->choice * (cardWidth + gap);
		const float y = origin.y + drop + (float)(int)card->part * (cardHeight + gap);
		const v2::Box b{ImVec2(x, y), ImVec2(x + cardWidth, y + cardHeight)};
		const bool hovered = sf4e::ui::PressEntry(nav, entries, e, b, action, changed).hovered, focused = e.id == nav.Focus();
		const bool saved = loadout.choice[card->part] == card->choice;
		v2::CardGround(draw, b, focused, hovered, saved);
		const v2::Box picture{ImVec2(b.min.x + 3 * s, b.min.y + 3 * s),
			ImVec2(b.max.x - 3 * s, b.min.y + 3 * s + std::floor((cardWidth - 6 * s) * shape.aspect))};
		draw->AddRectFilled(picture.min, picture.max, colors.ink);
		const sf4e::ui::SelectionImage shot = LoadoutPicture(sf4e::ui::MenuArt(), pick.pick.fighter, card->part, card->choice);
		if (!DrawFitted(draw, shot, picture, 1.f, false)) DrawFigure(draw, pick.pick, v2::Inset(picture, 0, 4 * s), 1.f);
		// The game's mark top right, as wide as half again its height.
		const float mark = std::floor((std::min)(64 * s, (std::max)(20 * s, picture.H() * .2f)));
		const ImVec2 markMax(picture.max.x - corner, picture.min.y + corner + mark);
		DrawLoadoutMark(draw, card->part, card->choice, {ImVec2(markMax.x - std::floor(mark * 1.5f), markMax.y - mark), markMax});
		if (saved) {
			sf4e::ui::DrawCardBadge(ImVec2(picture.min.x + corner, picture.min.y + corner), picture.W() * .5f, loc::T("selection.saved"),
				"saved-badge");
		}
		draw->AddRectFilled(ImVec2(b.min.x + 1, picture.max.y), ImVec2(b.max.x - 1, b.max.y - 1), v2::color::Alpha(colors.ink, .85f));
		const float room = cardWidth - 2 * pad;
		ImVec2 at(b.min.x + pad, picture.max.y + pad);
		std::string elided;
		const std::string part = std::string(PartLabel(card->part)) + " " + picks::Numeral(card->choice);
		sf4e::ui::DrawCardText(elided, at, room, part, font, colors.muted);
		at.y += line;
		const std::string move = MoveName(pick.pick.fighter, card->part, card->choice);
		v2::Text(draw, at, room, move.empty() ? e.label : move, titleFont, saved ? colors.accent : colors.text, sf4e::ui::HeadingFont());
		at.y += titleLine;
		// The note in the player's language: its glyphs load as it is drawn.
		if (!e.detail.empty()) {
			sf4e::ui::NoteUserText(e.detail);
			draw->PushClipRect(at, ImVec2(b.max.x - pad, b.max.y - pad), true);
			v2::Paragraph(draw, at, room, e.detail, font, colors.muted);
			draw->PopClipRect();
		}
		if (!e.enabled) draw->AddRectFilled(b.min, b.max, v2::color::Alpha(colors.ink, .55f));
		if (focused) v2::Focus(draw, b.min, b.max, false);
	}
	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy(ImVec2(width, drop + whole));
}

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

std::vector<MenuEntry> SfvSelectionScreen::LookRows(const PlayerPick& pick, bool random, bool editable, bool withStage,
	bool stageEditable, const std::vector<StageOption>& stageOptions, bool withSize, bool eleven)
{
	std::vector<MenuEntry> rows = Rows(pick, editable, withStage, stageEditable, stageOptions, withSize);
	for (MenuEntry& row : rows) {
		const bool part = PartOfRow(row.id).has_value(), stage = row.id == kStage;
		if (row.id != kFighter && row.id != kCostume && row.id != kColour && !part && !stage) continue;
		// Its page in place of its choices. Left and Right still step a row
		// whose value can change; a part with one choice, and the stage
		// while it is not this player's, open their cards all the same.
		row.adjustable = row.adjustable && row.enabled;
		if (part || stage) row.enabled = true;
		row.choices.clear();
		row.chosen.clear();
		row.opens = true;
		if (row.id == kFighter && random) {
			const std::string name = ember::Catalog().fighterName(pick.pick.fighter);
			row.value = eleven ? ElevenTitle(name) : loc::Tf("selection.random_fighter", name);
		}
	}
	return rows;
}

std::vector<MenuEntry> SfvSelectionScreen::BoardRows(const PlayerPick& pick, bool random, bool editable, bool fighterKept,
	bool withRandom, bool eleven)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	const char* saves = editable ? "selection.select_saves" : "selection.locked_detail";
	std::vector<MenuEntry> rows;
	// Every fighter keeps their place, as the game's grid does: one this
	// player cannot play is shown and cannot be chosen. The stored fighter
	// can always be selected: it opens their look page.
	for (int id : Roster()) {
		const bool locked = catalog.fighterLocked(id, {});
		const bool choosable = editable && !locked && !fighterKept;
		const char* detail = locked ? "selection.not_owned" : fighterKept && id != pick.pick.fighter ? "selection.winner_keeps_fighter" : saves;
		MenuEntry row = sf4e::ui::Row(CardId(kFighterCard, id), catalog.fighterName(id), loc::T(detail), choosable || id == pick.pick.fighter);
		row.key = (std::uint64_t)id;
		if (choosable) row.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(row));
	}
	// Random, in its own cell; stored, it opens the rolled fighter's page.
	if (withRandom) {
		const bool choosable = editable && !fighterKept;
		MenuEntry row = sf4e::ui::Row(kRandomCard, loc::T("selection.random_card"),
			loc::T(fighterKept ? "selection.winner_keeps_fighter" : editable ? "selection.random_card.detail" : "selection.locked_detail"),
			choosable || (random && !eleven));
		if (choosable) row.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(row));
		// The game's ELEVEN, in its own cell: Random's pick in the Eleven
		// look (SfvEleven); stored, it opens the rolled fighter's page.
		MenuEntry card = sf4e::ui::Row(kElevenCard, ElevenName(),
			loc::T(fighterKept ? "selection.winner_keeps_fighter" : editable ? "selection.random_card.detail" : "selection.locked_detail"),
			choosable || eleven);
		if (choosable) card.hint = loc::T("menu.hint.save_fighter");
		rows.push_back(std::move(card));
	}
	return rows;
}

float SfvSelectionScreen::BoardColumn(const grid::SelectGrid& table, grid::Cell cell)
{
	const bool pastRandom = table.random && cell.column > table.random->column;
	return (float)cell.column + grid::RowOffset(table, cell.row) + (pastRandom ? kRandomGap : 0.f);
}

void SfvSelectionScreen::LinkRandom(std::vector<MenuEntry>& rows, const grid::SelectGrid& table)
{
	const auto random = std::find_if(rows.begin(), rows.end(), [](const MenuEntry& row) { return row.id == kRandomCard; });
	if (!table.random || random == rows.end()) return;
	const auto card = [&table](grid::Cell cell) {
		const std::optional<std::string> code = grid::CodeAt(table, cell);
		if (!code || *code == grid::kRandomCellCode) return std::string();
		const std::optional<int> id = FighterId(std::wstring(code->begin(), code->end()));
		return id ? CardId(kFighterCard, *id) : std::string();
	};
	const auto link = [&rows](const std::string& id, std::string MenuEntry::*side) {
		for (MenuEntry& row : rows) {
			if (!id.empty() && row.id == id) row.*side = kRandomCard;
		}
	};
	const int column = table.random->column, top = table.random->row, last = (std::max)(top, grid::RowCount(table) - 1);
	// On Random's row, and on the last row, where a tile may stand in
	// Random's own column (the game's second row has one more on the left).
	random->left = card({column - 1, top});
	random->right = card({column + 1, top});
	link(random->left, &MenuEntry::right);
	link(random->right, &MenuEntry::left);
	if (last != top) {
		const std::string below = card({column, last});
		link(below.empty() ? card({column - 1, last}) : below, &MenuEntry::right);
		link(card({column + 1, last}), &MenuEntry::left);
	}
}

PlayerPick SfvSelectionScreen::BoardPick(const PlayerPick& pick, const std::string& cardId, bool remembered)
{
	PlayerPick shown = pick;
	const std::optional<int> fighter = sf4e::ui::RowKey<int>(cardId, kFighterCard);
	if (fighter && ember::ValidFighter(*fighter) && *fighter != pick.pick.fighter) shown.pick = LookFor(pick.pick, *fighter, remembered);
	return shown;
}

std::vector<MenuEntry> SfvSelectionScreen::CostumeCards(const PlayerPick& pick, bool editable)
{
	const int fighter = pick.pick.fighter;
	const std::vector<int> ready = CostumeIds(fighter, true);
	std::vector<MenuEntry> rows;
	for (int costume : CostumeIds(fighter, false)) {
		const bool can = Contains(ready, costume);
		MenuEntry row = sf4e::ui::Row(CardId(kCostumeCard, costume), CostumeName(fighter, costume),
			!can ? std::string(loc::T("selection.not_owned")) : editable ? std::string() : std::string(loc::T("selection.locked_detail")),
			editable && can);
		row.key = (std::uint64_t)costume;
		rows.push_back(std::move(row));
	}
	return rows;
}

std::vector<MenuEntry> SfvSelectionScreen::ColourCards(const PlayerPick& pick, int costume, bool editable)
{
	const int fighter = pick.pick.fighter;
	const std::vector<int> ready = ColourIds(fighter, costume, true);
	const std::string costumeName = CostumeName(fighter, costume);
	std::vector<MenuEntry> rows;
	for (int colour : ColourIds(fighter, costume)) {
		const bool can = Contains(ready, colour);
		MenuEntry row = sf4e::ui::Row(CardId(kColourCard, colour), ColourName(colour),
			!can ? std::string(loc::T("selection.not_owned")) : editable ? costumeName : std::string(loc::T("selection.locked_detail")),
			editable && can);
		row.key = (std::uint64_t)colour;
		if (row.enabled) row.hint = loc::T("menu.hint.save_color");
		rows.push_back(std::move(row));
	}
	if (rows.empty()) rows.push_back(sf4e::ui::Row("no-colors", loc::T("selection.no_colors"), costumeName, false));
	return rows;
}

std::vector<MenuEntry> SfvSelectionScreen::LoadoutCards(const PlayerPick& pick, bool editable)
{
	const int fighter = pick.pick.fighter;
	std::vector<MenuEntry> rows;
	for (const picks::LoadoutPart part : picks::kLoadoutParts) {
		for (int choice : PartNumbers(fighter, part)) {
			MenuEntry row = sf4e::ui::Row(CardId(PartPrefix(part).c_str(), choice), ChoiceText(choice, MoveName(fighter, part, choice)),
				LoadoutNote(fighter, part, choice), editable);
			row.key = (std::uint64_t)LoadoutPictureNumber(part, choice);
			// The note is in the player's language (SfvLoadoutNotes).
			row.detailText = sf4e::ui::DetailText::Name;
			if (editable) row.hint = loc::T("menu.hint.save_ultra");
			rows.push_back(std::move(row));
		}
	}
	return rows;
}

std::vector<MenuEntry> SfvSelectionScreen::StageCards(const PlayerPick& pick, bool editable, bool stageEditable,
	const std::vector<StageOption>& stageOptions)
{
	const bool choosable = editable && stageEditable;
	const char* refused = !editable ? "selection.locked_detail" : !stageEditable ? "selection.only_p1_stage" : nullptr;
	std::vector<MenuEntry> rows;
	for (const StageOption& option : stageOptions) {
		const bool lacking = option.offer == StageOffer::OpponentLacks;
		const char* detail = lacking ? kStageNotShared : refused ? refused :
			option.id == picks::kRandomStage ? "selection.random_stage.detail" : nullptr;
		MenuEntry row = sf4e::ui::Row(CardId(kStageCard, option.id), sf4e::ui::StageLabel(option.id),
			detail ? std::string(loc::T(detail)) : std::string(), choosable && !lacking);
		row.key = (std::uint64_t)option.id;
		if (row.enabled) row.hint = loc::T("menu.hint.save_stage");
		rows.push_back(std::move(row));
	}
	(void)pick;
	return rows;
}

SfvSelectionScreen::StageAnswer SfvSelectionScreen::ChooseStage(const std::string& cardId, PlayerPick& pick, bool editable,
	bool stageEditable, const std::vector<StageOption>& stageOptions)
{
	const std::optional<int> stage = sf4e::ui::RowKey<int>(cardId, kStageCard);
	const auto option = std::find_if(stageOptions.begin(), stageOptions.end(),
		[&stage](const StageOption& candidate) { return stage && candidate.id == *stage; });
	if (option == stageOptions.end()) return StageAnswer::NotOffered;
	if (!editable) return StageAnswer::Locked;
	if (!stageEditable) return StageAnswer::OnlyP1;
	if (option->offer == StageOffer::OpponentLacks) return StageAnswer::NotShared;
	if (pick.stage == option->id) return StageAnswer::Unchanged;
	pick.stage = option->id;
	return StageAnswer::Stored;
}

bool SfvSelectionScreen::ChooseLoadout(const std::string& cardId, PlayerPick& pick)
{
	const std::optional<PartChoice> card = PartCardOf(cardId);
	return card && SetPart(card->part, card->choice, pick.pick);
}

SfvSelectionScreen::Opening SfvSelectionScreen::OpeningFor(SelectionStart start, const PlayerPick& pick, bool random, bool dummy,
	bool eleven)
{
	Opening opening;
	opening.pages.push_back({kBoard, SavedCard(pick, random && !dummy, eleven && !dummy)});
	const std::string vSkill = CardId(PartPrefix(picks::LoadoutPart::VSkill).c_str(), picks::LoadoutOf(pick.pick).choice[picks::VSkill]);
	switch (start) {
	case SelectionStart::Board:
		break;
	case SelectionStart::Roster:
		// The room's Fighter: a fighter (or Random) chosen on the board is it.
		opening.closesOnChoice = true;
		break;
	case SelectionStart::LookUltra:
		// A winner who keeps the fighter: their look page, on the loadout.
		opening.pages.push_back({kLook, kVSkill});
		break;
	case SelectionStart::Options:
		// SFV's fighters have no options of SF4's kind: the look page.
		opening.pages.push_back({kLook, kCostume});
		break;
	case SelectionStart::Costumes:
		opening.pages.push_back({kLook, kCostume});
		opening.pages.push_back({kCostumes, CardId(kCostumeCard, pick.pick.costume)});
		opening.closesOnChoice = true;
		break;
	case SelectionStart::Ultras:
		opening.pages.push_back({kLook, kVSkill});
		opening.pages.push_back({kLoadout, vSkill});
		break;
	case SelectionStart::Stage:
		if (dummy) break;
		opening.pages.push_back({kLook, kStage});
		opening.pages.push_back({kStagePage, CardId(kStageCard, pick.stage)});
		opening.closesOnChoice = true;
		break;
	}
	return opening;
}

void SfvSelectionScreen::Open(const Opening& opening)
{
	auto& nav = menu_.navigation;
	nav.Home();
	for (size_t i = 0; i < opening.pages.size(); i++) {
		if (i) nav.Push(opening.pages[i].first);
		nav.Prefer(opening.pages[i].second);
	}
	startPage_ = opening.pages.size() > 1 ? opening.pages.back().first : std::string();
	closesOnChoice_ = opening.closesOnChoice;
}

void SfvSelectionScreen::FitGrid()
{
	if (gridFitted_ || !ember::Usable(ember::Catalog())) return;
	gridFitted_ = true;
	grid_ = grid::EmbeddedSelectGrid();
	if (grid_.columns <= 0) {
		// The table is checked by the tests; a build whose table does not
		// parse still shows every fighter, eight across.
		mod::Log("pick screen: the select grid table does not parse; the board is the roster, eight across");
		grid_ = {};
		grid_.columns = 8;
	}
	std::vector<std::string> roster;
	for (int id : Roster()) roster.push_back(FighterCode(id));
	const std::vector<std::string> added = grid::FitToRoster(grid_, roster);
	if (!added.empty()) {
		std::string codes;
		for (const std::string& code : added) codes += (codes.empty() ? "" : " ") + code;
		mod::Log("pick screen: the select grid table lacks %s; appended after its last cell", codes.c_str());
	}
	// Random after the last cell where the table gives it none.
	if (!grid_.random) {
		int last = -1;
		for (const grid::Entry& entry : grid_.fighters) last = (std::max)(last, entry.cell.row * grid_.columns + entry.cell.column);
		grid_.random = grid::Cell{(last + 1) % grid_.columns, (last + 1) / grid_.columns};
	}
}

void SfvSelectionScreen::Draw(bool editable, bool stageEditable, const std::string& error,
	const std::vector<std::int64_t>& unavailableStages, bool tinyAllowed, bool fighterKept)
{
	auto& nav = menu_.navigation;
	PlayerPick pick = dummy_ ? CurrentDummy() : CurrentPick();
	const bool random = !dummy_ && PickRandom();
	const bool eleven = random && PickEleven();
	const bool withStage = !dummy_, stageMine = stageEditable && withStage;
	// The shell says where Back goes and which of its shortcuts this screen
	// hands back; one it has just opened starts where the shell asked.
	const sf4e::ui::EmbeddedReturn& embedded = sf4e::ui::EmbeddedReturnContext();
	if (sf4e::ui::TakeEmbeddedFresh()) {
		openOn_ = embedded.openOn;
		Open(OpeningFor(openOn_, pick, random, dummy_, eleven));
		costume_ = pick.pick.costume;
		stageRefusal_.clear();
	}
	FitGrid();
	const std::vector<StageOption> stageOptions = StageOptions(unavailableStages);
	// A page this screen does not have (the stage page on the dummy's) is
	// the board.
	if (nav.Screen() == kStagePage && !withStage) nav.Home();
	const std::string screen = nav.Screen();
	if (screen != kStagePage) stageRefusal_.clear();
	menu_.rootName = loc::T(dummy_ ? "training.dummy" : "screen.selection");
	menu_.exitName = embedded.exitName;
	menu_.shortcutHints = embedded.shortcutHints;
	menu_.customDetail = false;
	menu_.paneWidth = 0;
	const std::string fighterName = ember::Catalog().fighterName(pick.pick.fighter);
	std::string title = menu_.rootName;
	std::vector<MenuEntry> rows;
	int columns = 1;
	sf4e::ui::GameMenu::Detail detail;
	sf4e::ui::GameMenu::Body body;
	// A focus the pad moved since the body last drew scrolls into view.
	const auto moved = [this](const MenuNavigation& navigation) {
		const bool changed = navigation.Focus() != bodyFocus_;
		bodyFocus_ = navigation.Focus();
		return changed;
	};
	const BoardFrame frame{grid_, pick, random, !dummy_, fighterKept, eleven};
	const char* status = editable ? "selection.status_editable" : "selection.locked_detail";
	stageCardClicked_ = false;
	if (screen == kLook) {
		title = eleven ? ElevenTitle(fighterName) : random ? loc::Tf("selection.random_fighter", fighterName) : fighterName;
		rows = LookRows(pick, random, editable, withStage, stageMine, stageOptions, tinyAllowed && withStage, eleven);
		if (editable) status = "selection.status_adjust";
		// The pane beside the rows is the pick's preview, as wide as the
		// fighter's banner; its stage card, shown while the stage row has the
		// focus, opens the stage cards when clicked.
		menu_.customDetail = true;
		menu_.paneWidth = kBannerWidth;
		detail = [this, &pick, &rows, &stageOptions, withStage](const std::string& focus) {
			std::optional<OpenChoice> choice;
			if (menu_.navigation.Choosing()) choice = OpenChoice{menu_.navigation.DialogId(), menu_.navigation.ChoiceIndex()};
			const ImVec2 at = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
			const v2::Box card =
				DrawPickPreviewIn(PreviewOf(pick, focus, choice, rows, stageOptions), {at, ImVec2(at.x + room.x, at.y + room.y)});
			if (!withStage || !(card.W() > 0 && card.H() > 0) || menu_.navigation.Choosing()) return;
			ImGui::SetCursorScreenPos(card.min);
			if (ImGui::InvisibleButton("##stage-card", ImVec2(card.W(), card.H()))) stageCardClicked_ = true;
		};
	} else if (screen == kCostumes || screen == kColours) {
		const bool costumes = screen == kCostumes;
		title = loc::T(costumes ? "selection.costume_gallery_title" : "selection.color_gallery_title");
		rows = costumes ? CostumeCards(pick, editable) : ColourCards(pick, costume_, editable);
		if (editable && costumes) status = "selection.status_browse";
		const int fighter = pick.pick.fighter, costume = costume_;
		CardGrid cards{costumes ? kCostumes : kColours, costumes ? sf4e::ui::FigureCard : kColourCardShape, {}, {}, {}};
		if (costumes) {
			// Each costume in the colour its picture is of (the package holds
			// each costume in its first colour), named by the costume alone.
			cards.picture = [&pick](ImDrawList* draw, const MenuEntry& e, v2::Box box, float alpha) {
				sf4e::selection::Pick look = pick.pick;
				look.costume = (int)e.key;
				look.color = PictureColour(look.fighter, look.costume);
				DrawFigure(draw, look, box, alpha);
			};
			cards.saved = [&pick](const MenuEntry& e) { return (int)e.key == pick.pick.costume; };
			cards.preview = [&pick](const MenuEntry& e) {
				PickPreview preview;
				preview.pick = Previewed(pick, kCostume, OptionId((int)e.key));
				if (!e.enabled) preview.note = e.detail;
				return preview;
			};
		} else {
			// Each colour as the game's costume menu shows it: its two-tone
			// swatch chip, as large a square as the card holds (its edge
			// alone while the swatches load); one this player cannot ready
			// dimmed and struck through, as the pane's chips are. The pane
			// beside the cards shows the costume in its picture's colour,
			// tagged with the focused colour.
			cards.picture = [fighter, costume](ImDrawList* draw, const MenuEntry& e, v2::Box box, float alpha) {
				if (!sf4e::ui::HasPrefix(e.id, kColourCard)) return;
				const auto& colors = v2::Colors();
				const float s = sf4e::ui::Scale();
				const float side = std::floor((std::min)(box.W(), box.H()) * .8f);
				const ImVec2 corner(std::floor(box.min.x + (box.W() - side) * .5f), std::floor(box.min.y + (box.H() - side) * .5f));
				const v2::Box chip{corner, ImVec2(corner.x + side, corner.y + side)};
				DrawSwatch(draw, fighter, costume, (int)e.key, chip, alpha);
				draw->AddRect(chip.min, chip.max, v2::color::Alpha(colors.border, alpha), 0, 0, (std::max)(1.f, s));
				if (!e.enabled) draw->AddLine(ImVec2(chip.min.x + s, chip.max.y - s), ImVec2(chip.max.x - s, chip.min.y + s), colors.muted, s);
			};
			cards.saved = [&pick, costume](const MenuEntry& e) {
				return sf4e::ui::HasPrefix(e.id, kColourCard) && costume == pick.pick.costume && (int)e.key == pick.pick.color;
			};
			cards.preview = [&pick, costume](const MenuEntry& e) {
				PlayerPick shown = pick;
				shown.pick.costume = costume;
				PickPreview preview;
				preview.pick = Previewed(shown, kColour, OptionId((int)e.key));
				if (!e.enabled) preview.note = e.detail;
				return preview;
			};
		}
		body = [cards, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawCardGrid(entries, navigation, action, height, moved(navigation), cards); };
	} else if (screen == kLoadout) {
		title = loc::T("selection.ultra_combo_title");
		rows = LoadoutCards(pick, editable);
		body = [&pick, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawLoadoutCards(entries, navigation, action, height, moved(navigation), pick); };
	} else if (screen == kStagePage) {
		title = loc::T("selection.stage_title");
		rows = StageCards(pick, editable, stageMine, stageOptions);
		if (editable && !stageMine) status = "selection.only_p1_stage";
		const auto lacking = [&stageOptions](int stage) {
			return std::any_of(stageOptions.begin(), stageOptions.end(),
				[stage](const StageOption& option) { return option.id == stage && option.offer == StageOffer::OpponentLacks; });
		};
		CardGrid cards{kStagePage, kStageCardShape, {}, {}, {}};
		cards.picture = [lacking](ImDrawList* draw, const MenuEntry& e, v2::Box box, float) {
			DrawFitted(draw, StageImage((int)e.key), box, lacking((int)e.key) ? kLocked : 1.f, false);
		};
		cards.saved = [&pick](const MenuEntry& e) { return (int)e.key == pick.stage; };
		cards.preview = [&pick, lacking, stageMine](const MenuEntry& e) {
			PickPreview preview;
			preview.pick = pick;
			preview.pick.stage = (int)e.key;
			preview.focus = PreviewFocus::Stage;
			preview.stageShared = !lacking((int)e.key);
			preview.note = !preview.stageShared ? std::string(loc::T(kStageNotShared)) :
				!stageMine ? std::string(loc::T("selection.only_p1_stage")) : std::string();
			return preview;
		};
		body = [cards, moved](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawCardGrid(entries, navigation, action, height, moved(navigation), cards); };
	} else {
		rows = BoardRows(pick, random, editable, fighterKept, !dummy_, eleven);
		// A table with no cell for ELEVEN draws no card, so none is offered.
		if (!grid_.eleven) {
			rows.erase(std::remove_if(rows.begin(), rows.end(), [](const MenuEntry& row) { return row.id == kElevenCard; }), rows.end());
		}
		LinkRandom(rows, grid_);
		columns = (std::max)(1, grid_.columns);
		if (editable) status = fighterKept ? "selection.winner_keeps_fighter" : "selection.status_browse";
		// A focus from no card of the board (a first visit) starts on the stored card.
		if (!EntryOf(rows, nav.Focus())) nav.Prefer(SavedCard(pick, random, eleven));
		body = [&frame](const std::vector<MenuEntry>& entries, MenuNavigation& navigation, MenuAction& action, float height,
				   const sf4e::ui::MenuVisualFeedback&) { DrawBoard(entries, navigation, action, height, frame); };
	}
	const bool refused = error.empty() && !stageRefusal_.empty();
	const std::string statusLine = !error.empty() ? error : refused ? stageRefusal_ : std::string(loc::T(status));
	const MenuAction action = menu_.Draw(title.c_str(), rows, statusLine.c_str(), detail, columns, {}, body, false, 100,
		error.empty() && !refused ? sf4e::ui::Tone::Neutral : sf4e::ui::Tone::Error);
	Answer(action, screen, pick, random, eleven, editable, stageMine, fighterKept, stageOptions);
}

void SfvSelectionScreen::Answer(const MenuAction& action, const std::string& screen, PlayerPick pick, bool random, bool eleven,
	bool editable, bool stageMine, bool fighterKept, const std::vector<StageOption>& stageOptions)
{
	auto& nav = menu_.navigation;
	const auto close = [] { sf4e::ui::ForwardMenuAction({MenuAction::Close}); };
	if (action.kind == MenuAction::Close || action.kind == MenuAction::Shortcut) sf4e::ui::ForwardMenuAction(action);
	// Opened for one change, Back from the page it opened on goes back to
	// where it came from.
	if (action.kind == MenuAction::Returned && !startPage_.empty() && screen == startPage_) close();
	// A fighter's look page, on its costume.
	const auto look = [&nav] {
		nav.Push(kLook);
		nav.Prefer(kCostume);
	};
	bool store = false;
	if (action.kind == MenuAction::Activate) {
		if (screen == kBoard) {
			if (action.id == kRandomCard && !dummy_) {
				// Chosen, Random rolls its fighter (the runtime's
				// RandomFighterPick) and the board stays; stored, it opens the
				// rolled fighter's page.
				// From Eleven, Random is a choice of its own.
				if ((!random || eleven) && editable && !fighterKept) {
					SetPickRandom(true);
					if (closesOnChoice_ && startPage_.empty()) close();
				} else if (random && !eleven) look();
			} else if (action.id == kElevenCard && !dummy_) {
				// Chosen, Eleven rolls its fighter as Random does, in the look
				// the game's Eleven wears; stored, it opens the rolled
				// fighter's page.
				if (!eleven && editable && !fighterKept) {
					SetPickEleven(true);
					if (closesOnChoice_ && startPage_.empty()) close();
				} else if (eleven) look();
			} else if (const std::optional<int> fighter = sf4e::ui::RowKey<int>(action.id, kFighterCard)) {
				const bool choosable = editable && !fighterKept && ember::ValidFighter(*fighter) && !FighterLocked(*fighter);
				if (choosable) {
					pick.pick = LookFor(pick.pick, *fighter, !dummy_);
					store = true;
					if (!dummy_) SetPickRandom(false);
					if (closesOnChoice_ && startPage_.empty()) close();
					else look();
				} else if (*fighter == pick.pick.fighter) look();
			}
		} else if (screen == kLook) {
			if (action.id == kDone) close();
			else if (action.id == kFighter) {
				nav.Home();
				nav.Prefer(SavedCard(pick, random, eleven));
			} else if (action.id == kCostume) {
				costume_ = pick.pick.costume;
				nav.Push(kCostumes);
				nav.Prefer(CardId(kCostumeCard, costume_));
			} else if (action.id == kColour) {
				costume_ = pick.pick.costume;
				nav.Push(kColours);
				nav.Prefer(CardId(kColourCard, pick.pick.color));
			} else if (const std::optional<picks::LoadoutPart> part = PartOfRow(action.id)) {
				nav.Push(kLoadout);
				nav.Prefer(CardId(PartPrefix(*part).c_str(), picks::LoadoutOf(pick.pick).choice[*part]));
			} else if (action.id == kStage) {
				nav.Push(kStagePage);
				nav.Prefer(CardId(kStageCard, pick.stage));
			}
		} else if (screen == kCostumes) {
			if (const std::optional<int> costume = sf4e::ui::RowKey<int>(action.id, kCostumeCard)) {
				costume_ = *costume;
				nav.Push(kColours);
				nav.Prefer(CardId(kColourCard, CardColour(pick.pick, costume_)));
			}
		} else if (screen == kColours) {
			const std::optional<int> colour = sf4e::ui::RowKey<int>(action.id, kColourCard);
			if (colour && editable && Contains(ColourIds(pick.pick.fighter, costume_, true), *colour)) {
				// A colour stores its costume with it; back to the look page,
				// or, opened for the look, back where it came from.
				pick.pick.costume = costume_;
				pick.pick.color = *colour;
				store = true;
				while (nav.Screen() == kColours || nav.Screen() == kCostumes) nav.Return();
				if (closesOnChoice_ && startPage_ == kCostumes) close();
			}
		} else if (screen == kLoadout) {
			if (editable && ChooseLoadout(action.id, pick)) store = true;
		} else if (screen == kStagePage) {
			switch (ChooseStage(action.id, pick, editable, stageMine, stageOptions)) {
			case StageAnswer::Stored:
				store = true;
				[[fallthrough]];
			case StageAnswer::Unchanged:
				stageRefusal_.clear();
				// Opened for this one change, the pick is it; from the look
				// page the stage page stays.
				if (closesOnChoice_ && startPage_ == kStagePage) close();
				break;
			case StageAnswer::Locked: stageRefusal_ = loc::T("selection.locked_detail"); break;
			case StageAnswer::OnlyP1: stageRefusal_ = loc::T("selection.only_p1_stage"); break;
			case StageAnswer::NotShared: stageRefusal_ = loc::T(kStageNotShared); break;
			case StageAnswer::NotOffered: break;
			}
		}
	} else if (screen == kLook && editable && (action.kind == MenuAction::Adjust || action.kind == MenuAction::Chosen)) {
		// The value rows step and choose in place; the stage only where it is
		// this player's.
		if ((action.id != kStage || stageMine) && Apply(action, pick, stageOptions)) store = true;
	}
	// The look page's stage card, clicked.
	if (stageCardClicked_ && screen == kLook && nav.Screen() == kLook) {
		nav.Push(kStagePage);
		nav.Prefer(CardId(kStageCard, pick.stage));
	}
	stageCardClicked_ = false;
	if (!store) return;
	if (dummy_) SetDummy(pick);
	else SetPick(pick);
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
