#pragma once

// What SFV's two picture loaders (SfvArt's selection pictures, SfvBackdrop's
// menu artwork) share in taking one of the game's textures: where loading is
// allowed, how long a texture's resource is waited for, and the view that
// draws it. Each loader keeps its own pictures; a picture still waiting for
// its resource when the main menu is left is let go by both, as the game may
// collect its object on the way to a battle.

#include "mod/GameArt.hxx"

#include <d3d11.h>

namespace sfve::adapter::textures {

// The render thread makes a loaded texture's resource within a frame or two;
// one still without it after this many frames is given up on.
inline constexpr int kResourceFrames = 120;

// Game thread: the game is at its main menu, the only place a texture is
// loaded (loading is synchronous, so never in or on the way to a battle).
bool AtMainMenu();

// The game's textures are typeless where it views them as sRGB; Ember draws
// their stored values as they are, as the game's own menus show them.
DXGI_FORMAT ViewFormat(unsigned format);

// Render thread: the shader view of a taken texture, made on the texture's own
// device (the game's); `view` is the caller's to release. The failure's
// HRESULT, `view` null, when it cannot be made.
HRESULT MakeView(const mod::GameTexture& texture, ID3D11ShaderResourceView** view);

} // namespace sfve::adapter::textures
