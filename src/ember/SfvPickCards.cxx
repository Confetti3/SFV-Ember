#include "ember/SfvPickDraw.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvPickPages.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelection.hxx"

#include "common/Localization.hxx"
#include "ui/AppearanceGrid.hxx"
#include "ui/RoomCards.hxx"
#include "ui/Theme.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter::pickdraw {

namespace {

using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;
using sf4e::ui::MenuNavigation;
using pickpages::EntryOf;
using pickpages::PartCardOf;
using pickpages::PartChoice;
namespace loc = sf4e::loc;
namespace v2 = sf4e::ui::v2;

// The pages with a body keep a pane on the right where the body is at least
// this wide (unscaled), as GameMenu splits its cards from their detail.
constexpr float kPaneFrom = 820;
constexpr float kPaneGap = 24;
// The lines of a loadout card's note: its 140 characters at the card's
// narrowest.
constexpr int kNoteLines = 3;

// The width of a body page's pane beside its cards `width` wide; 0 where
// the cards keep the whole width (a narrow window), as GameMenu does.
float PaneWidth(float width)
{
	const float s = sf4e::ui::Scale();
	if (width < kPaneFrom * s) return 0;
	return std::floor((std::min)(kBannerWidth * s, width * .4f));
}

} // namespace

// The pane on its panel ground (GameMenu's detail pane), its contents inset
// as that pane insets them.
void DrawPane(v2::Box pane, const PickPreview& preview)
{
	const float s = sf4e::ui::Scale();
	if (!(pane.W() > 0)) return;
	ImGui::GetWindowDrawList()->AddRectFilled(pane.min, pane.max, v2::PanelGround(), v2::Corner());
	DrawPickPreviewIn(preview, v2::Inset(pane, 16 * s, 16 * s));
}

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

} // namespace sfve::adapter::pickdraw
