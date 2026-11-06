#include "overlay/TextureUploaderD3D11.hxx"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>

namespace sfve::overlay {

namespace {

using Microsoft::WRL::ComPtr;

std::string Hex(HRESULT value)
{
	char text[16];
	std::snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(value));
	return text;
}

class D3D11Uploader final : public sf4e::ui::TextureUploader {
public:
	explicit D3D11Uploader(ID3D11Device* device) : device_(device) {}

	ImTextureID Create(unsigned width, unsigned height, const unsigned char* bgra, std::string& failure) override
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // SelectionArt's pixels, as they come
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA pixels{};
		pixels.pSysMem = bgra;
		pixels.SysMemPitch = width * 4; // rows packed with no padding
		ComPtr<ID3D11Texture2D> texture;
		HRESULT hr = device_->CreateTexture2D(&desc, &pixels, &texture);
		if (FAILED(hr)) {
			failure = "CreateTexture2D " + Hex(hr);
			return 0;
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC view{};
		view.Format = desc.Format;
		view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		view.Texture2D.MipLevels = 1;
		ID3D11ShaderResourceView* shown = nullptr;
		// The view keeps the texture alive; the texture's own reference goes here.
		if (FAILED(hr = device_->CreateShaderResourceView(texture.Get(), &view, &shown))) {
			failure = "CreateShaderResourceView " + Hex(hr);
			return 0;
		}
		return reinterpret_cast<ImTextureID>(shown);
	}

	void Release(ImTextureID texture) override
	{
		if (texture) reinterpret_cast<ID3D11ShaderResourceView*>(static_cast<std::uintptr_t>(texture))->Release();
	}

private:
	ComPtr<ID3D11Device> device_;
};

} // namespace

std::unique_ptr<sf4e::ui::TextureUploader> MakeTextureUploaderD3D11(ID3D11Device* device)
{
	return std::make_unique<D3D11Uploader>(device);
}

} // namespace sfve::overlay
