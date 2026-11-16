#pragma once

// The pick screen's drawing (SfvSelectionScreen): the board's slanted tiles,
// the picture cards of the costume, colour and stage pages, the loadout
// cards, and the pane beside them. What each page lists and does is
// SfvPickPages. Render thread, inside the shell's menu frame.

#include "ember/SfvPick.hxx"
#include "ember/SfvPickPreview.hxx"
#include "ember/SfvSelectGrid.hxx"

#include "ui/AppearanceGrid.hxx"
#include "ui/GameMenu.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <functional>
#include <vector>

namespace sfve::adapter::pickdraw {

// The pane is as wide as the fighter's banner (CharaSelectIcon_l, 380x258).
constexpr float kBannerWidth = 380;
// A dimmed card's picture.
constexpr float kLocked = .4f;
// The stage cards (StageSelectIcon_l, 674x238): their picture's height over
// width, and the narrowest and widest they go (unscaled).
constexpr sf4e::ui::CardShape kStageCardShape{238.f / 674.f, 120, 340};
// The colour cards: each the colour's swatch chip, square, as many as fit.
constexpr sf4e::ui::CardShape kColourCardShape{1.f, 48, 110};

// The body's area from the cursor, `height` tall, and the pane cut from its
// right where there is room for one.
struct BodyArea {
	ImVec2 origin;
	float width = 0;
	sf4e::ui::v2::Box cards, pane;
};
BodyArea LayOutBody(float height, bool withPane);
// The pane on its panel ground (GameMenu's detail pane), its contents inset
// as that pane insets them.
void DrawPane(sf4e::ui::v2::Box pane, const PickPreview& preview);

// The board as SFV's select screen lays it out (SfvSelectGrid, and the game's
// measures): each fighter's slanted tile in its cell, the right side's
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
void DrawBoard(const std::vector<sf4e::ui::MenuEntry>& entries, sf4e::ui::MenuNavigation& nav, sf4e::ui::MenuAction& action,
	float height, const BoardFrame& frame);

// A grid of picture cards (the library's FitGallery, as SF4 Ember's
// galleries lay theirs out) in a scrolling child of its own, the pane beside
// it: each card its picture over its name, the saved one with the gold rule,
// one that cannot be chosen dimmed and its name muted.
struct CardGrid {
	const char* page; // the scrolling child's name: each page keeps its own scroll
	sf4e::ui::CardShape shape;
	std::function<void(ImDrawList*, const sf4e::ui::MenuEntry&, sf4e::ui::v2::Box, float alpha)> picture;
	std::function<bool(const sf4e::ui::MenuEntry&)> saved;
	std::function<PickPreview(const sf4e::ui::MenuEntry&)> preview;
};
void DrawCardGrid(const std::vector<sf4e::ui::MenuEntry>& entries, sf4e::ui::MenuNavigation& nav, sf4e::ui::MenuAction& action,
	float height, bool changed, const CardGrid& cards);
// The loadout cards: each V-Skill and V-Trigger choice the fighter has.
void DrawLoadoutCards(const std::vector<sf4e::ui::MenuEntry>& entries, sf4e::ui::MenuNavigation& nav, sf4e::ui::MenuAction& action,
	float height, bool changed, const PlayerPick& pick);

} // namespace sfve::adapter::pickdraw
