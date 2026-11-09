#include "ember/SfvArt.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvFaceCrop.hxx"
#include "ember/SfvRankNames.hxx"
#include "mod/GameArt.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include "common/ProductIdentity.hxx"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cwchar>
#include <deque>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Loads are synchronous on the game thread: a few a frame keep the menu
// moving while a screen full of faces comes in.
constexpr int kLoadsPerFrame = 2;
// The render thread makes a loaded texture's resource within a frame or two;
// one still without it after this long is given up on.
constexpr int kResourceFrames = 120;

// How a picture is shown: whole, or (a select-grid face) the square of its
// face (SfvFaceCrop).
enum class Framing { Whole, FaceSquare };

struct Picture {
	enum class State { Queued, WaitingForResource, Ready, Missing } state = State::Queued;
	Framing framing = Framing::Whole; // as its first request asked
	uint8_t* object = nullptr; // the game's texture, until its Direct3D texture is taken
	int frames = 0;
	mod::GameTexture texture;                  // Ready: the game's texture, with our reference
	ID3D11ShaderResourceView* view = nullptr;  // made on the render thread
	std::optional<SquareCrop> square;          // FaceSquare: found with the view; empty draws it whole
};

struct CostumeSwatches {
	bool read = false;
	std::vector<int> colours;
	std::vector<std::optional<std::pair<std::uint32_t, std::uint32_t>>> swatches; // one per colour
};

std::mutex s_mutex;
std::map<std::wstring, Picture> s_pictures; // by asset path
std::deque<std::wstring> s_queue;
std::map<std::pair<int, int>, CostumeSwatches> s_swatches; // by fighter and costume
std::deque<std::pair<int, int>> s_swatchQueue;

std::wstring Wide(const std::string& text)
{
	return std::wstring(text.begin(), text.end());
}

// A UI texture's object path: "/Game/<folder>/<name>.<name>".
std::wstring AssetPath(const std::wstring& folder, const wchar_t* name)
{
	return L"/Game/" + folder + L"/" + name + L"." + name;
}

// The game's textures are typeless where it views them as sRGB; Ember draws
// their stored values as they are, as the game's own menus show them.
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

// Render thread, under the lock: the view of a ready texture, made on the
// texture's own device (the game's), whose reference it holds.
void MakeView(const std::wstring& path, Picture& picture)
{
	ID3D11Device* device = nullptr;
	picture.texture.texture->GetDevice(&device);
	D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
	desc.Format = ViewFormat(picture.texture.format);
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipLevels = 1;
	const HRESULT made = device ? device->CreateShaderResourceView(picture.texture.texture, &desc, &picture.view) : E_FAIL;
	if (device) device->Release();
	if (FAILED(made)) {
		mod::Log("art: %ls: no view of format %u (0x%08lX)", path.c_str(), picture.texture.format, (unsigned long)made);
		picture.state = Picture::State::Missing;
	}
}

// Formats whose pixels are four bytes with alpha in the fourth.
bool AlphaInFourthByte(unsigned format)
{
	switch (ViewFormat(format)) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return true;
	default: return false;
	}
}

// Render thread, under the lock, once per face: its square (SfvFaceCrop),
// found from the texture's alpha through a staging copy on the game's own
// device, read on its immediate context (the render thread's) and let go
// here. The read waits for the copy, a stall of about a frame, once a face.
// Empty, and the face drawn whole, where the format keeps no alpha in a
// pixel's fourth byte, the copy cannot be read or no square is found.
std::optional<SquareCrop> ReadFaceSquare(const std::wstring& path, const mod::GameTexture& texture)
{
	using Microsoft::WRL::ComPtr;
	if (!AlphaInFourthByte(texture.format)) {
		mod::Log("art: %ls: format %u has no alpha to find the face by; drawn whole", path.c_str(), texture.format);
		return std::nullopt;
	}
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	texture.texture->GetDevice(&device);
	device->GetImmediateContext(&context);
	D3D11_TEXTURE2D_DESC desc{};
	texture.texture->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	ComPtr<ID3D11Texture2D> staging;
	HRESULT result = device->CreateTexture2D(&desc, nullptr, &staging);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (SUCCEEDED(result)) {
		context->CopyResource(staging.Get(), texture.texture);
		result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
	}
	if (FAILED(result)) {
		mod::Log("art: %ls: its alpha could not be read (0x%08lX); drawn whole", path.c_str(), (unsigned long)result);
		return std::nullopt;
	}
	const std::optional<SquareCrop> square =
		FaceSquare(static_cast<const std::uint8_t*>(mapped.pData), texture.width, texture.height, mapped.RowPitch);
	context->Unmap(staging.Get(), 0);
	if (square) {
		mod::Log("art: %ls: face %dx%d at (%d, %d) of %dx%d", path.c_str(), square->side, square->side, square->x, square->y,
			texture.width, texture.height);
	} else {
		mod::Log("art: %ls: no square in its painted area; drawn whole", path.c_str());
	}
	return square;
}

sf4e::ui::SelectionImage Image(const std::wstring& path, Framing framing = Framing::Whole)
{
	sf4e::ui::SelectionImage image;
	if (path.empty()) return image.missing = true, image;
	std::lock_guard lock(s_mutex);
	auto [at, added] = s_pictures.try_emplace(path);
	Picture& picture = at->second;
	if (added) {
		picture.framing = framing;
		s_queue.push_back(path);
	}
	if (picture.state == Picture::State::Ready && !picture.view) {
		MakeView(path, picture);
		if (picture.view && picture.framing == Framing::FaceSquare) picture.square = ReadFaceSquare(path, picture.texture);
	}
	if (picture.state == Picture::State::Missing) return image.missing = true, image;
	if (picture.state != Picture::State::Ready) return image;
	image.texture = reinterpret_cast<ImTextureID>(picture.view);
	image.width = picture.texture.width;
	image.height = picture.texture.height;
	// A crop is its uv rect and its own size, as the shared art reports one
	// (SelectionArt::Request), so every shared screen fits it as it is.
	if (const std::optional<SquareCrop>& square = picture.square) {
		image.uvMin = ImVec2((float)square->x / image.width, (float)square->y / image.height);
		image.uvMax = ImVec2((float)(square->x + square->side) / image.width, (float)(square->y + square->side) / image.height);
		image.width = image.height = square->side;
	}
	return image;
}

bool AtMainMenu()
{
	return mod::SceneStateKind(mod::CurrentSceneState()) == mod::sfv::scene::kMainMenuVtable;
}

// Game thread: the textures loaded before waiting for their resources, then
// the queue's next few.
void PumpPictures()
{
	std::vector<std::wstring> load;
	{
		std::lock_guard lock(s_mutex);
		for (auto& [path, picture] : s_pictures) {
			if (picture.state != Picture::State::WaitingForResource) continue;
			bool pending = false;
			std::string why;
			if (const std::optional<mod::GameTexture> texture = mod::TakeGameTexture(picture.object, &pending, &why)) {
				picture.texture = *texture;
				picture.state = Picture::State::Ready;
				mod::Log("art: %ls: %dx%d, format %u", path.c_str(), texture->width, texture->height, texture->format);
			} else if (!pending || ++picture.frames >= kResourceFrames) {
				mod::Log("art: %ls: not drawn: %s", path.c_str(), pending ? "its resource never came" : why.c_str());
				picture.state = Picture::State::Missing;
			}
			if (picture.state != Picture::State::WaitingForResource) picture.object = nullptr;
		}
		while (!s_queue.empty() && (int)load.size() < kLoadsPerFrame) {
			load.push_back(std::move(s_queue.front()));
			s_queue.pop_front();
		}
	}
	for (const std::wstring& path : load) {
		uint8_t* object = mod::LoadTexture(path);
		std::lock_guard lock(s_mutex);
		Picture& picture = s_pictures[path];
		picture.object = object;
		picture.state = object ? Picture::State::WaitingForResource : Picture::State::Missing;
		if (!object) mod::Log("art: %ls: the game has no texture there", path.c_str());
	}
}

// Game thread: one costume's swatches a frame, every colour of it at once.
void PumpSwatches()
{
	std::pair<int, int> costume;
	{
		std::lock_guard lock(s_mutex);
		if (s_swatchQueue.empty()) return;
		costume = s_swatchQueue.front();
		s_swatchQueue.pop_front();
	}
	CostumeSwatches read;
	read.read = true;
	read.colours = ColourIds(costume.first, costume.second);
	for (int colour : read.colours) read.swatches.push_back(mod::ColourSwatch(FighterCode(costume.first), costume.second, colour));
	std::lock_guard lock(s_mutex);
	s_swatches[costume] = std::move(read);
}

} // namespace

sf4e::ui::SelectionImage FighterImage(int fighter, FighterPicture picture)
{
	const std::string code = FighterCode(fighter);
	if (code.empty()) return Image({});
	const std::wstring folder = L"Chara/" + Wide(code) + L"/Hud";
	if (picture == FighterPicture::Face) return Image(AssetPath(folder, L"CharaSelectIconLeft"), Framing::FaceSquare);
	return Image(AssetPath(folder, L"CharaSelectIcon_l"));
}

sf4e::ui::SelectionImage StageImage(int stage)
{
	const std::string code = StageCode(stage);
	return Image(code.empty() ? std::wstring() : AssetPath(L"Stage/" + Wide(code) + L"/UI", L"StageSelectBG"));
}

sf4e::ui::SelectionImage LeagueImage(const std::string& name)
{
	const ember::RankingClass* ranks = ember::Product().branding.rankingClasses;
	bool known = false;
	for (int band = 0; ranks && band < ember::RankingClassCount; band++) known = known || name == ranks[band].emblem;
	return Image(known ? AssetPath(L"CommonAsset/LeagueIcon", Wide(name).c_str()) : std::wstring());
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> Swatch(int fighter, int costume, int colour)
{
	std::lock_guard lock(s_mutex);
	auto [at, added] = s_swatches.try_emplace({fighter, costume});
	if (added) s_swatchQueue.push_back({fighter, costume});
	const CostumeSwatches& swatches = at->second;
	for (size_t i = 0; swatches.read && i < swatches.colours.size(); i++) {
		if (swatches.colours[i] == colour) return swatches.swatches[i];
	}
	return std::nullopt;
}

// Game thread at the main menu, once: SFV's league names in the game's
// language (ID_CMN_LEAGUE_0000 to _0019, namespace "KW", as the game builds
// them, rva 0x6BDE20), logged, and the ten bands' published for the shell
// (ranks::Name). A league the game has no text for keeps its English name.
void ReadLeagueNames()
{
	static bool read = false;
	if (read) return;
	read = true;
	std::array<std::string, ranks::kLeagues> leagues;
	std::string logged;
	for (int league = 0; league < ranks::kLeagues; league++) {
		wchar_t key[32];
		swprintf(key, std::size(key), L"ID_CMN_LEAGUE_%04d", league);
		std::string& name = leagues[(size_t)league];
		name = mod::LocalizedText(L"KW", key);
		logged += (league ? ", " : "") + std::to_string(league) + " " + (name.empty() ? std::string("(none)") : name);
	}
	mod::Log("ranks: SFV's leagues as the game names them: %s", logged.c_str());
	std::array<std::string, 10> bands;
	for (size_t band = 0; band < bands.size(); band++) bands[band] = leagues[(size_t)ranks::kBandLeague[band]];
	ranks::Publish(bands);
}

void InstallSfvArt()
{
	mod::RunEachGameFrame([] {
		// Loading is synchronous: never while a battle runs.
		if (!AtMainMenu()) return true;
		ReadLeagueNames();
		PumpPictures();
		PumpSwatches();
		return true;
	});
}

} // namespace sfve::adapter
