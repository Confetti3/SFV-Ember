#pragma once

// The pane beside SFV's pick screen (SfvSelectionScreen): the game's own
// pictures and words for the pick it shows, in Band's look (charcoal grounds,
// the one gold accent, square corners). EMBER_LIBRARY.md section 43; the
// beta.8 pages, docs/design/beta8/select.md.
// - The look (the board's fighters, the look page's Fighter, Costume and
//   Colour rows, the costume and colour cards): the fighter in that look,
//   the captured cutout where the package has it (SelectionArt::Character),
//   else the fighter's large HUD figure (PartCharacter), else the banner,
//   or the shop's picture of the costume where SfvPictureSpec allows it; the
//   fighter's and the costume's names; every colour of the costume as
//   two-tone chips (SfvSwatchStrip), EX colours on their own row, the chosen
//   one with the gold bar under it; and the colour's label. A look this
//   player cannot ready (NOT OWNED) is dimmed, its chips struck through.
// - The loadout (the V-Skill and V-Trigger rows, and under the look): one
//   line per part, the game's I or II mark (Ember's own plate where the mark
//   does not load), the part's label and the choice in the game's words, the
//   focused part's line brighter.
// - The stage: its card (SfvPictureSpec's StageCard), its name, and for
//   Random the game's Random picture and what Random does. A stage the
//   opponent lacks is dimmed.
// - The board's Random card: its mark, its name and what Random does, and the
//   fighter it rolled while Random is the pick.
// While a choice is open the pane shows the pick with the focused option in
// it (SfvSelectionScreen's Previewed), so the player sees each costume,
// colour, part and stage before choosing it.
//
// Every line shows at the smallest window the shell allows (960x540): the
// words keep their room and the picture above them gives way, narrower and
// shorter at its own shape.
//
// Render thread, inside the pick screen's menu frame.

#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"

#include "common/SelectionPick.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/V2.hxx"

#include <imgui.h>

#include <string>

namespace sfve::adapter {

// What the pane shows, from the focused row (SfvSelectionScreen's FocusOf),
// or the board's Random or Eleven card.
enum class PreviewFocus { Look, Loadout, Stage, Random, Eleven };

struct PickPreview {
	PlayerPick pick; // the stored pick, with an open choice's focused option in it
	PreviewFocus focus = PreviewFocus::Look;
	// Loadout: the focused part, whose line is brighter. Otherwise unused.
	picks::LoadoutPart part = picks::LoadoutPart::VSkill;
	// Why the focused option of an open choice cannot be chosen, or that the
	// stage shown is one the opponent lacks, drawn first in the danger tone;
	// empty for nothing to say.
	std::string note;
	// The stage shown is one both seated players have (not the stage row's
	// OpponentLacks).
	bool stageShared = true;
	// Random, and Eleven: it is the stored pick, whose fighter (`pick`) it
	// rolled.
	bool randomSaved = false;
};

// In the window's room from the cursor.
void DrawPickPreview(const PickPreview& preview);
// In `box` of the current window. Where it drew the stage's card (the
// stage's picture), for the pick screen to answer a click on it; an empty
// box where it drew none.
sf4e::ui::v2::Box DrawPickPreviewIn(const PickPreview& preview, sf4e::ui::v2::Box box);

// The pane's arithmetic, in its pixels, apart from ImGui so that the tests
// check it at the smallest window. The tallest a picture may be drawn with
// `room` left in the pane, `words` under it (measured by drawing them with no
// draw list: the look's lines and chips and the loadout's lines, or the
// stage's name and sentence) and `gap` between the two: what is left once the
// words have theirs, and none when nothing is.
float PictureRoom(float room, float words, float gap);

// The pictures the pick screen's cards share with the pane. Each is fitted
// whole in `box` at its own shape, centred, at `alpha`; while it loads, the
// box's edge alone.
//
// Any picture so, standing on the box's foot with `bottom` (a figure). False,
// drawing nothing, where the game or the package has none.
bool DrawFitted(ImDrawList* draw, const sf4e::ui::SelectionImage& image, sf4e::ui::v2::Box box, float alpha, bool bottom);
// The fighter in a look: the captured cutout (SelectionArt::Character,
// through the menu's art), else the large HUD figure (PartCharacter), else
// the banner. `exact`: only the cutout of exactly this look
// (SelectionArt::Appearance), and false, drawing nothing, where the package
// lacks it (the colour cards then show the colour's swatch). True when it
// drew a picture or its frame.
bool DrawFigure(ImDrawList* draw, const sf4e::selection::Pick& look, sf4e::ui::v2::Box box, float alpha, bool exact = false);
// The colour's two-tone swatch (SfvArt's CostumeSwatches) filling `box`, the
// first colour on top as the game's costume menu splits it. False, drawing
// nothing, while the swatches load or where the game has none for it.
bool DrawSwatch(ImDrawList* draw, int fighter, int costume, int colour, sf4e::ui::v2::Box box, float alpha);
// The game's mark for the part's choice in `box`: the mark fitted on whole
// pixels, never larger than it is; Ember's plate with the numeral where the
// mark does not load; nothing while it loads.
void DrawLoadoutMark(ImDrawList* draw, picks::LoadoutPart part, int number, sf4e::ui::v2::Box box);
// Random's mark, a question mark as large as `box` lets it be, in `colour`.
void DrawRandomMark(ImDrawList* draw, sf4e::ui::v2::Box box, ImU32 colour);

} // namespace sfve::adapter
