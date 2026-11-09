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
//   Ember cuts the background (StageSelectBG, 2048x512) to the banner's
//   shape, or (once the probe allows) draws the large card;
// - generic V-Skill and V-Trigger marks for I and II (the replay log's);
// - per costume colour: two colours, the costume menu's swatch;
// - per costume the shop sells: a 420x190 picture (shown only once the
//   probe settles which costume each is; SfvPictureSpec's kCostumePicture).
// The game has no picture of a fighter in any other costume or colour: its
// select screens show the live 3D model.
//
// A screen asks on the render thread and gets the picture once it is ready:
// the request is loaded on the game thread at the main menu (never during a
// battle), two a frame, the texture's view is made on the render thread, and
// both are kept for the session.

#include "ember/SfvPictureSpec.hxx"
#include "ember/SfvPickSpace.hxx"

#include "ui/SelectionArt.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfve::adapter {

enum class FighterPicture {
	Face,   // square, the head from the select grid's face
	Banner, // wide, the select screen's large card
};

// Render thread. Not missing and no texture while it loads; missing when the
// game has none.
sf4e::ui::SelectionImage FighterImage(int fighter, FighterPicture picture);
// A stage's card (SfvPictureSpec's StageCard); Random's (picks::kRandomStage)
// is the game's own Random stage picture.
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
// Any picture by its Spec: the one path every picture above takes.
sf4e::ui::SelectionImage PictureImage(const pictures::Spec& spec);

// Any thread: puts the picture in the loading queue, as a first PictureImage
// does, without making its view (which only the render thread may). The
// development probe's `art load` (SfvArtProbe) asks so.
void RequestPicture(const pictures::Spec& spec);

// A costume's colours with their two swatch colours (0xRRGGBB each), read
// together by the game thread. Not `read`, and empty, while they load; a
// colour the game has no preview data for has no swatch.
using SwatchColours = std::pair<std::uint32_t, std::uint32_t>;
struct CostumePalette {
	bool read = false;
	std::vector<int> colours;                         // the costume's colours, in the game's order
	std::vector<std::optional<SwatchColours>> swatches; // one per colour
};
// Any thread: the costume's palette, asked for once and then read.
CostumePalette CostumeSwatches(int fighter, int costume);

// Game thread, once: the loader that answers the requests.
void InstallSfvArt();

} // namespace sfve::adapter
