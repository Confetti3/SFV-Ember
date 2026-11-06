#pragma once

// SFV's pictures on Ember's screens: the game's own UI textures for its
// fighters and stages, drawn from the game's Direct3D 11 textures (no pixel
// leaves the game), and the colour swatches its costume menu shows. What
// exists in the game (EMBER_LIBRARY.md section 15):
// - per fighter: its HUD pictures, of which Ember uses the select grid's
//   face (CharaSelectIconLeft, 116x144) and the select screen's banner
//   (CharaSelectIcon_l, 380x258); its HUD FaceIconLeft (256x256), drawn as
//   it is, is a blue silhouette of the same shape for every fighter seen;
// - per stage: the stage select's picture (StageSelectBG, 2048x512);
// - per costume colour: two colours, the costume menu's swatch.
// The game has no picture of a fighter in a given costume or colour: its
// select screens show the live 3D model.
//
// A screen asks on the render thread and gets the picture once it is ready:
// the request is loaded on the game thread at the main menu (never during a
// battle), the texture's view is made on the render thread, and both are
// kept for the session.

#include "ui/SelectionArt.hxx"

#include <cstdint>
#include <optional>
#include <utility>

namespace sfve::adapter {

enum class FighterPicture {
	Face,   // upright, the select grid's face
	Banner, // wide, the select screen's large card
};

// Render thread. Not missing and no texture while it loads; missing when the
// game has none.
sf4e::ui::SelectionImage FighterImage(int fighter, FighterPicture picture);
sf4e::ui::SelectionImage StageImage(int stage);

// Render thread: a colour's two swatch colours (0xRRGGBB), once read; empty
// while they load or where the game has none.
std::optional<std::pair<std::uint32_t, std::uint32_t>> Swatch(int fighter, int costume, int colour);

// Game thread, once: the loader that answers the requests.
void InstallSfvArt();

} // namespace sfve::adapter
