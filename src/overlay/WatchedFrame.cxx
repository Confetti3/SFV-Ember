#include "overlay/WatchedFrame.hxx"

#include "mod/Log.hxx"

#include <imgui_impl_dx11.h>

namespace sfve::overlay {

namespace {

using mod::Log;

// Turns blending off for the draw after it: the frame's colours are written
// as they are, and its alpha is not used (the picture keeps none).
void BlendOff(const ImDrawList*, const ImDrawCmd*)
{
	auto* state = static_cast<ImGui_ImplDX11_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
	state->DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffffu);
}

// `from` drawn over the whole of `to` (`width` x `height`), linearly
// filtered, by ImGui's own renderer: a draw list of one image, after the
// callback that turns blending off.
void DrawScaled(ID3D11ShaderResourceView* from, ID3D11RenderTargetView* to, float width, float height, RenderInto render)
{
	ImDrawList list(ImGui::GetDrawListSharedData());
	list._ResetForNewFrame();
	list.PushClipRect(ImVec2(0, 0), ImVec2(width, height));
	list.AddCallback(BlendOff, nullptr);
	list.AddImage(reinterpret_cast<ImTextureID>(from), ImVec2(0, 0), ImVec2(width, height));
	ImDrawData data;
	data.Valid = true;
	data.DisplayPos = ImVec2(0, 0);
	data.DisplaySize = ImVec2(width, height);
	data.FramebufferScale = ImVec2(1, 1);
	data.AddDrawList(&list);
	render(&data, to);
}

} // namespace

PicturePlan PlanPicture(const TextureShape& back, const TextureShape& frame, const TextureShape& picture, unsigned width,
	unsigned height)
{
	PicturePlan plan;
	if (!back.width || !back.height || !width || !height) return plan;
	plan.take = true;
	plan.remakeFrame = !(frame == TextureShape{back.width, back.height, back.format, 1});
	plan.remakePicture = picture.width != width || picture.height != height;
	plan.resolve = back.samples > 1;
	return plan;
}

ImTextureID WatchedFrame::Take(IDXGISwapChain* swap, ID3D11Device* device, ID3D11DeviceContext* context, unsigned width,
	unsigned height, RenderInto render)
{
	if (failed_) return {};
	Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
	if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.GetAddressOf())))) return {};
	D3D11_TEXTURE2D_DESC desc{};
	back->GetDesc(&desc);
	const TextureShape shape{desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.SampleDesc.Count};
	const PicturePlan plan = PlanPicture(shape, frameShape_, pictureShape_, width, height);
	if (!plan.take) return {};
	if ((plan.remakeFrame && !MakeFrame(device, shape)) || (plan.remakePicture && !MakePicture(device, width, height))) {
		Log("overlay: no %ux%u picture of the watched game's %ux%u frame in format %u (%u samples); Ember's menu shows the game "
			"through itself",
			width, height, shape.width, shape.height, shape.format, shape.samples);
		Release();
		failed_ = true;
		return {};
	}
	if (plan.resolve) context->ResolveSubresource(frame_.Get(), 0, back.Get(), 0, desc.Format);
	else context->CopyResource(frame_.Get(), back.Get());
	DrawScaled(frameView_.Get(), pictureTarget_.Get(), static_cast<float>(width), static_cast<float>(height), render);
	// Once per overlay: the format names the back buffer the picture came from.
	if (!logged_) {
		logged_ = true;
		Log("overlay: the watched game's picture %ux%u from its %ux%u frame in format %u (%u samples)", width, height, shape.width,
			shape.height, shape.format, shape.samples);
	}
	return reinterpret_cast<ImTextureID>(pictureView_.Get());
}

void WatchedFrame::Release()
{
	frame_.Reset();
	frameView_.Reset();
	picture_.Reset();
	pictureTarget_.Reset();
	pictureView_.Reset();
	frameShape_ = {};
	pictureShape_ = {};
	failed_ = false;
	logged_ = false;
}

bool WatchedFrame::MakeFrame(ID3D11Device* device, const TextureShape& back)
{
	frame_.Reset();
	frameView_.Reset();
	frameShape_ = {};
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = back.width;
	desc.Height = back.height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = static_cast<DXGI_FORMAT>(back.format); // its view decodes an sRGB frame, which Ember's frame encodes again
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &frame_)) ||
		FAILED(device->CreateShaderResourceView(frame_.Get(), nullptr, &frameView_)))
		return false;
	frameShape_ = {back.width, back.height, back.format, 1};
	return true;
}

bool WatchedFrame::MakePicture(ID3D11Device* device, unsigned width, unsigned height)
{
	picture_.Reset();
	pictureTarget_.Reset();
	pictureView_.Reset();
	pictureShape_ = {};
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM; // samples opaque
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&desc, nullptr, &picture_)) ||
		FAILED(device->CreateRenderTargetView(picture_.Get(), nullptr, &pictureTarget_)) ||
		FAILED(device->CreateShaderResourceView(picture_.Get(), nullptr, &pictureView_)))
		return false;
	pictureShape_ = {width, height, static_cast<unsigned>(desc.Format), 1};
	return true;
}

} // namespace sfve::overlay
