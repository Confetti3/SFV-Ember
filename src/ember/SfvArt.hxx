#pragma once

// SFV's pictures on Ember's screens: the game's own UI textures for its
// fighters, stages, V-Skill and V-Trigger marks and leagues, drawn from the
// game's Direct3D 11 textures, and the colour swatches its costume menu
// shows. No pixel is kept or leaves the game: a face's alpha is read once,
// through a staging copy let go at once, to find its square (SfvFaceCrop,
// EMBER_LIBRARY.md section 37). Which picture is which, and how each is cut,
// is SfvPictureSpec's. What exists in the game (EMBER_LIBRARY.md sections 15
// and 43):
// - per fighter: its HUD pictures, of which Ember uses the select grid's
//   face (CharaSelectIconLeft, 116x144), drawn as the square of the head in
//   its slanted card, and the select screen's banner (CharaSelectIcon_l,
//   380x258); its HUD FaceIconLeft (256x256), drawn as it is, is a blue
//   silhouette of the same shape for every fighter seen;
// - per stage, Random (RND) included: five stage select pictures, of which
//   Ember draws the 674x238 landscape card (StageSelectIcon_l), or where it
//   does not load the background (StageSelectBG, 2048x512) cut to its shape;
// - generic V-Skill and V-Trigger marks for I and II (the replay log's);
// - per costume colour: two colours, the costume menu's swatch;
// - per costume the shop sells: a 420x190 picture (not shown: which costume
//   each is is unproven; SfvPictureSpec's kCostumePicture).
// The game has no picture of a fighter in any other costume or colour: its
// select screens show the live 3D model.
//
// A screen asks on the render thread and gets the picture once it is ready:
// the request is loaded on the game thread at the main menu (never during a
// battle), two a frame, the texture's view is made on the render thread, and
// both are kept for the session.

#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSwatchStrip.hxx"

#include "ui/SelectionArt.hxx"

#include <string>
#include <vector>

namespace sfve::adapter {

enum class FighterPicture {
	Face,   // square, the head from the select grid's face
	Banner, // wide, the select screen's large card
};

// Render thread. Not missing and no texture while it loads; missing when the
// game has none.
sf4e::ui::SelectionImage FighterImage(int fighter, FighterPicture picture);
// A stage's card (SfvPictureSpec's StageCard), or its cut background
// (StageBackground) where the card does not load; Random's
// (picks::kRandomStage) is the game's own Random stage picture.
sf4e::ui::SelectionImage StageImage(int stage);
// The game's mark for a loadout part's choice (0 is I); missing for a choice
// past II, or where the game's mark does not load as a texture.
sf4e::ui::SelectionImage LoadoutImage(picks::LoadoutPart part, int choice);
// The shop's picture of the costume; missing where the shop has none, and
// for every costume while SfvPictureSpec's kCostumePicture is Banner (then
// nothing is loaded).
sf4e::ui::SelectionImage CostumeImage(int fighter, int costume);
// A league's icon (/Game/CommonAsset/LeagueIcon/<name>, as "LI_gold"), the
// art SFV Ember's ranks are drawn with (SfvProduct's ranking classes,
// EMBER_LIBRARY.md section 28). Missing for a name that is not one of those
// ranks', so nothing else is ever loaded by this path.
sf4e::ui::SelectionImage LeagueImage(const std::string& name);

// A costume's colours as the pick screen's strip shows them (SfvSwatchStrip),
// one entry each, made once when the costume is first asked for: every
// colour in the game's order, whether this player can ready it (SfvCatalog's
// pickable ColourIds), and its two swatch colours once the game thread has
// read them all together (`read`). A colour the game has no preview data for
// has no swatch then either.
struct CostumePalette {
	bool read = false;
	std::vector<StripColour> colours;
};
// Any thread: the costume's palette, asked for once and then read; empty for
// a costume the catalog does not have, and, without being kept, while the
// catalog is not filled (ember::Usable).
CostumePalette CostumeSwatches(int fighter, int costume);

// Game thread, once: the loader that answers the requests.
void InstallSfvArt();

} // namespace sfve::adapter
