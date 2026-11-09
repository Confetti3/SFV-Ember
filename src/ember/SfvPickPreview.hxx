#pragma once

// The pane beside SFV's pick screen rows (SfvSelectionScreen): the game's own
// pictures and words for the pick it shows, in Band's look (charcoal grounds,
// the one gold accent, square corners). EMBER_LIBRARY.md section 43.
// - The look (the Fighter, Costume and Colour rows): the fighter's banner, or
//   the shop's picture of the costume where SfvPictureSpec allows it; the
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
// While a choice is open the pane shows the pick with the focused option in
// it (SfvSelectionScreen's Previewed), so the player sees each costume,
// colour, part and stage before choosing it.
//
// Render thread, inside the pick screen's menu frame.

#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"

#include <string>

namespace sfve::adapter {

// What the pane shows, from the focused row (SfvSelectionScreen's FocusOf).
enum class PreviewFocus { Look, Loadout, Stage };

struct PickPreview {
	PlayerPick pick; // the stored pick, with an open choice's focused option in it
	PreviewFocus focus = PreviewFocus::Look;
	// Loadout: the focused part, whose line is brighter. Otherwise unused.
	picks::LoadoutPart part = picks::LoadoutPart::VSkill;
	// Why the focused option of an open choice cannot be chosen, drawn first
	// in the danger tone; empty for nothing to say.
	std::string note;
	// The stage shown is one both seated players have (not the stage row's
	// OpponentLacks).
	bool stageShared = true;
};

void DrawPickPreview(const PickPreview& preview);

} // namespace sfve::adapter
