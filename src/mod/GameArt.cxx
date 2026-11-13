#include "mod/GameArt.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>
#include <d3d11.h>

namespace sfve::mod {

namespace {

using ClassFn = uint8_t* (*)(const wchar_t* package);
using LoadFn = uint8_t* (*)(uint8_t* objectClass, uint8_t* outer, const wchar_t* name, const wchar_t* filename,
	uint32_t flags, void* sandbox, bool reconcile);
using GameDataFn = uint8_t* (*)();
using SwatchFn = bool (*)(uint8_t* charaAsset, const GameString* code, int32_t costume, int32_t colour, uint32_t* out);

template <typename Fn>
Fn At(const CodeSite& site)
{
	return reinterpret_cast<Fn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + site.rva);
}

// The vtable every ID3D11Texture2D of this process shares: d3d11.dll's own
// class, whichever device made the texture. A pointer read from the game's
// RHI object is taken for a texture only when its vtable is this one. Read
// once from a texture made on a device of our own, which is then let go.
const void* TextureVtable()
{
	static const void* vtable = [] {
		const void* found = nullptr;
		ID3D11Device* device = nullptr;
		if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
				&device, nullptr, nullptr))) {
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			ID3D11Texture2D* texture = nullptr;
			if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture))) {
				found = *reinterpret_cast<void* const*>(texture);
				texture->Release();
			}
			device->Release();
		}
		if (!found) Log("art: no Direct3D 11 device to tell the game's textures by; no game pictures");
		return found;
	}();
	return vtable;
}

// A pointer field of a game object, read only if the memory is there.
uint8_t* PointerAt(const uint8_t* object, uint32_t offset)
{
	uint8_t* value = nullptr;
	return object && TryRead(object + offset, &value) ? value : nullptr;
}

} // namespace

uint8_t* LoadTexture(const std::wstring& path)
{
	uint8_t* textureClass = At<ClassFn>(sfv::kTexture2DClass)(L"/Script/Engine");
	if (!textureClass) return nullptr;
	return At<LoadFn>(sfv::kStaticLoadObject)(textureClass, nullptr, path.c_str(), nullptr, 0, nullptr, true);
}

std::optional<GameTexture> TakeGameTexture(uint8_t* texture, bool* pending, std::string* failure)
{
	*pending = false;
	const void* vtable = TextureVtable();
	if (!vtable) return *failure = "no Direct3D 11 texture to compare with", std::nullopt;
	uint8_t* resource = PointerAt(texture, sfv::texture::kResource);
	uint8_t* rhi = PointerAt(resource, sfv::texture::kTextureRhi);
	if (!resource || !rhi) return *pending = true, std::nullopt; // the render thread has not made it yet
	if (PointerAt(resource, sfv::texture::kResourceOwner) != texture) return *failure = "its resource is not its own", std::nullopt;
	uint8_t* platform = PointerAt(texture, sfv::texture::kPlatformData);
	struct { int32_t width, height; } size{};
	if (!platform || !TryRead(platform, &size)) return *failure = "it has no platform data", std::nullopt;
	uint8_t* d3d = PointerAt(rhi, sfv::texture::kD3DTexture);
	const void* d3dVtable = PointerAt(d3d, 0);
	if (!d3d || d3dVtable != vtable) return *failure = "its RHI texture holds no Direct3D 11 texture", std::nullopt;
	auto* game = reinterpret_cast<ID3D11Texture2D*>(d3d);
	D3D11_TEXTURE2D_DESC desc{};
	game->GetDesc(&desc);
	// A streamed texture's Direct3D texture holds only its resident mips;
	// one smaller than the texture is not its whole picture.
	if ((int)desc.Width != size.width || (int)desc.Height != size.height) {
		*failure = "only " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " of " +
			std::to_string(size.width) + "x" + std::to_string(size.height) + " is resident";
		return std::nullopt;
	}
	game->AddRef();
	return GameTexture{game, (int)desc.Width, (int)desc.Height, (unsigned)desc.Format};
}

std::optional<std::pair<uint32_t, uint32_t>> ColourSwatch(const std::string& fighterCode, int costume, int colour)
{
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* charaAsset = singleton ? Field<uint8_t*>(singleton, sfv::content::kCharaAsset) : nullptr;
	if (!charaAsset) return std::nullopt;
	const std::wstring wide(fighterCode.begin(), fighterCode.end());
	const GameString code{wide.c_str(), (int32_t)wide.size() + 1, (int32_t)wide.size() + 1};
	uint32_t colours[2] = {};
	if (!At<SwatchFn>(sfv::kColourSwatch)(charaAsset, &code, costume, colour, colours)) return std::nullopt;
	return std::make_pair(colours[0], colours[1]);
}

bool GameIsLoading()
{
	return At<bool (*)()>(sfv::kIsAsyncLoading)();
}

} // namespace sfve::mod
