#pragma once

// Ember's pictures (the library's ui/SelectionArt.hxx) as Direct3D 11
// textures on the game's device: each an immutable BGRA texture, handed to
// ImGui as its shader resource view, the texture id imgui_impl_dx11 draws.

#include "ui/SelectionArt.hxx"

#include <memory>

struct ID3D11Device;

namespace sfve::overlay {

std::unique_ptr<sf4e::ui::TextureUploader> MakeTextureUploaderD3D11(ID3D11Device* device);

} // namespace sfve::overlay
