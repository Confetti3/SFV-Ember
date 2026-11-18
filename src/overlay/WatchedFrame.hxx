#pragma once

// The watched game's picture on SFV (ShellView::watchedPicture, asked through
// ember::ShellGame::WatchedPicture; docs/design/WATCH_PICTURE.md): the frame
// the game presents, as it is before Ember draws on it, scaled to the size
// the shell asks, as SF4 Ember's sf4e__WatchedPicture takes it from its
// Direct3D 9 render target.
//
// Two textures, made at the first shapes seen and again only when one
// changes, and let go with the overlay:
//   - the frame's copy, at the back buffer's size and format, which can be
//     sampled (the back buffer itself may not be): the back buffer is copied
//     into it, or resolved into it when multisampled;
//   - the picture, at the size asked, B8G8R8X8, into which the copy is drawn
//     linearly filtered with blending off, so it samples opaque whatever
//     alpha the game left in its back buffer (ImGui blends images by their
//     alpha), as SF4's X8R8G8B8 picture does.
// One copy and one small draw per frame that asks; nothing in a frame that
// does not.

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <wrl/client.h>

namespace sfve::overlay {

// A texture's shape, as the picture's textures follow the back buffer's.
struct TextureShape {
	unsigned width = 0, height = 0, format = 0, samples = 1;
	bool operator==(const TextureShape&) const = default;
};

// What one frame's picture takes.
struct PicturePlan {
	bool take = false;          // false: nothing is taken (an empty back buffer or size asked)
	bool remakeFrame = false;   // the frame's copy is made again, at the back buffer's size and format
	bool remakePicture = false; // the picture is made again, at the size asked
	bool resolve = false;       // the back buffer is multisampled: resolved into the copy, not copied
};

// Pure. The plan for a back buffer of shape `back`, with the copy and the
// picture as they are made now (`frame`, `picture`; empty before the first)
// and the picture asked at `width` x `height`.
PicturePlan PlanPicture(const TextureShape& back, const TextureShape& frame, const TextureShape& picture, unsigned width,
	unsigned height);

// Draws an ImGui frame's data into a render target, leaving the engine's
// pipeline as it was (the overlay's own Render).
using RenderInto = void (*)(ImDrawData* data, ID3D11RenderTargetView* target);

class WatchedFrame {
public:
	// Render thread, while the overlay builds its frame: the picture of the
	// frame `swap` is about to present, at `width` x `height`, or an empty id
	// when it cannot be taken (a texture the device refused: logged once, and
	// not tried again until Release).
	ImTextureID Take(IDXGISwapChain* swap, ID3D11Device* device, ID3D11DeviceContext* context, unsigned width,
		unsigned height, RenderInto render);
	// The textures go, with the overlay's device.
	void Release();

private:
	bool MakeFrame(ID3D11Device* device, const TextureShape& back);
	bool MakePicture(ID3D11Device* device, unsigned width, unsigned height);

	Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frameView_;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> picture_;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pictureTarget_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> pictureView_;
	TextureShape frameShape_, pictureShape_;
	bool failed_ = false, logged_ = false;
};

} // namespace sfve::overlay
