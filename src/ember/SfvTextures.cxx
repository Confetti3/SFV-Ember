#include "ember/SfvTextures.hxx"

#include "mod/GameScene.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::adapter::textures {

bool AtMainMenu()
{
	return mod::SceneStateKind(mod::CurrentSceneState()) == mod::sfv::scene::kMainMenuVtable;
}

DXGI_FORMAT ViewFormat(unsigned format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
	case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
	case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
	case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
	default: return (DXGI_FORMAT)format;
	}
}

HRESULT MakeView(const mod::GameTexture& texture, ID3D11ShaderResourceView** view)
{
	*view = nullptr;
	ID3D11Device* device = nullptr;
	texture.texture->GetDevice(&device);
	D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
	desc.Format = ViewFormat(texture.format);
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipLevels = 1;
	const HRESULT made = device ? device->CreateShaderResourceView(texture.texture, &desc, view) : E_FAIL;
	if (device) device->Release();
	return made;
}

} // namespace sfve::adapter::textures
