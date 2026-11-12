#pragma once

// What SfvSelection.cxx offers beyond the library's SelectionArt members
// (ui/SelectionArt.hxx), which the shared screens call by the library's own
// names: the typed form of the loadout picture the beta.8 loadout cards draw.
//
// The library's SelectionArt::Ultra(fighter, ultra) takes one number; SFV's
// loadout has two parts of two choices each, and its `ultra` slot (the pick's
// V-Skill * 2 + V-Trigger, picks::LoadoutIndex) is not the number a picture is
// asked by: a picture is of ONE part's choice. SFV Ember asks Ultra with
// n = part * 2 + choice (V-Skill I 0, V-Skill II 1, V-Trigger I 2, V-Trigger
// II 3: LoadoutPictureNumber), and SfvSelection.cxx's Ultra answers it from
// the package's `assets\selection\<CODE>\vs<k>.png` / `vt<k>.png`
// (SfvSelectionAssetPath.hxx). LoadoutPicture is the one typed way for the
// screens to ask.
//
// Stream B fills the bodies in SfvSelection.cxx; this header is final.

#include "ember/SfvPickSpace.hxx"

#include "ui/SelectionArt.hxx"

namespace sfve::adapter {

// The number SelectionArt::Ultra is asked with for a part's choice: part * 2 +
// choice (0 to 3); -1 for a part or choice outside the loadout's.
constexpr int LoadoutPictureNumber(picks::LoadoutPart part, int choice)
{
	return choice < 0 || choice >= picks::kPartChoices ? -1 : (int)part * picks::kPartChoices + choice;
}

// Render thread. The picture of fighter `fighter`'s (the catalog's id)
// V-Skill or V-Trigger number `choice` (0 is I), through `art`
// (SelectionArt::Ultra with LoadoutPictureNumber). Not missing and no texture
// while it loads; missing for a null `art`, a choice outside the loadout's, a
// fighter without that choice, or a package without the picture.
sf4e::ui::SelectionImage LoadoutPicture(sf4e::ui::SelectionArt* art, int fighter, picks::LoadoutPart part, int choice);

} // namespace sfve::adapter
