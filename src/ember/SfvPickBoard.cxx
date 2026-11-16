#include "ember/SfvPickDraw.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
#include "ember/SfvPickPages.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvPickSpace.hxx"

#include "common/Localization.hxx"
#include "ui/MenuRows.hxx"
#include "ui/RoomCards.hxx"
#include "ui/RowId.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace sfve::adapter::pickdraw {

namespace {

using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;
using sf4e::ui::MenuNavigation;
using pickpages::EntryOf;
using pickpages::FighterLocked;
using pickpages::kElevenCard;
using pickpages::kFighterCard;
using pickpages::kRandomCard;
namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;

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
// The saved card's mark under its tile: Band's gold bar, in unscaled pixels.
constexpr float kSavedBar = 3;

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

} // namespace

void DrawBoard(const std::vector<MenuEntry>& entries, MenuNavigation& nav, MenuAction& action, float height, const BoardFrame& frame)
{
	const BodyArea area = LayOutBody(height, true);
	const grid::SelectGrid& table = frame.table;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const int rows = (std::max)(1, grid::RowCount(table));
	// The board in the game's 1080-line pixels: x from row 0's column 0's
	// centre, y from the first row's top.
	const auto across = [&table](grid::Cell cell) { return pickpages::BoardColumn(table, cell) * kTilePitch; };
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
		preview.pick = pickpages::BoardPick(frame.pick, focused->id, frame.remembered);
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

} // namespace sfve::adapter::pickdraw
