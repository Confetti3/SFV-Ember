#pragma once

// The game's own pictures, read from the player's install at run time: a
// UI texture by its asset path, as the Direct3D 11 texture the game's
// renderer made for it, and a costume colour's swatch. Nothing is copied out
// of the game: the overlay draws the game's texture itself (SfvArt), and
// reads a face's alpha only to find where to crop it.
//
// Game thread only, and only at the main menu: loading is synchronous, so
// no load may run while a battle does.

#include <cstdint>
#include <optional>
#include <string>

struct ID3D11Texture2D;

namespace sfve::mod {

// Starts a UI texture (the game's UTexture2D at `path`, as
// "/Game/Chara/RYU/Hud/FaceIconLeft.FaceIconLeft"): the game finds it or
// loads it. Null when the game has no texture there.
uint8_t* LoadTexture(const std::wstring& path);

// A loaded texture's Direct3D 11 texture, with a reference the caller owns,
// and its size. The game's render thread makes it a little after the load,
// so until then the result is empty with `pending` true; empty with `pending`
// false (and why in `failure`) when what the texture holds is not a whole
// Direct3D 11 texture of the texture's own size, which is never drawn.
struct GameTexture {
	ID3D11Texture2D* texture = nullptr; // AddRef'd for the caller
	int width = 0, height = 0;
	unsigned format = 0; // DXGI_FORMAT
};
std::optional<GameTexture> TakeGameTexture(uint8_t* texture, bool* pending, std::string* failure);

// The two colours (0xRRGGBB) the costume settings menu shows for a colour
// of a fighter's costume; empty when the game has no preview data for that
// costume. May load the costume's preview data asset.
std::optional<std::pair<uint32_t, uint32_t>> ColourSwatch(const std::string& fighterCode, int costume, int colour);

} // namespace sfve::mod
