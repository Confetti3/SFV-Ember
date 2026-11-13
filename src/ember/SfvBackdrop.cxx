#include "ember/SfvBackdrop.hxx"

#include "ember/SfvBackdropRules.hxx"
#include "mod/BuildKind.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameArt.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"
#include "overlay/FrameGrab.hxx"

#include "netplay/JsonFileStore.hxx"
#include "netplay/SettingsStore.hxx"
#include "ui/GameMenu.hxx"
#include "ui/Theme.hxx"

#include <windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

namespace json_file = sf4e::netplay::json_file;
using backdrop::Cycle;

// A loaded picture's resource comes within a frame or two; one still without
// it after this long is given up on (as SfvArt does).
constexpr int kResourceFrames = 120;
// Smaller than this is not a full-screen picture: skipped.
constexpr int kLeastWidth = 640, kLeastHeight = 360;
// The backdrop counts as on screen this long after the shell last drew it:
// the loader loads only then.
constexpr ULONGLONG kWantedMs = 1000;

// --- The player's choice (interface.json) ---

constexpr const wchar_t* kChoiceFile = L"interface.json";
constexpr const char* kChoiceKey = "backgroundArtwork";

std::filesystem::path Folder()
{
	return std::filesystem::path(sf4e::netplay::SettingsStore::DefaultDirectory());
}

std::mutex s_choiceMutex;
bool s_choiceLoaded = false;
std::atomic<bool> s_enabled{true};

// interface.json as it is now: its object, or an empty one when there is none
// or it does not read as one (logged; the next save writes a fresh one).
nlohmann::json ReadChoices()
{
	std::string bytes, error;
	bool missing = false;
	nlohmann::json choices;
	if (!json_file::ReadBytes(Folder() / kChoiceFile, bytes, missing, error) || missing) {
		if (!missing) mod::Log("backdrop: interface.json does not read (%s)", error.c_str());
		return nlohmann::json::object();
	}
	if (!json_file::Parse(bytes, choices, error) || !choices.is_object()) {
		mod::Log("backdrop: interface.json is no settings object (%s); the next save writes a new one", error.c_str());
		return nlohmann::json::object();
	}
	return choices;
}

void LoadChoiceLocked()
{
	if (s_choiceLoaded) return;
	s_choiceLoaded = true;
	const nlohmann::json choices = ReadChoices();
	const auto saved = choices.find(kChoiceKey);
	s_enabled = saved == choices.end() || !saved->is_boolean() || saved->get<bool>();
	mod::Log("backdrop: background artwork %s (interface.json)", s_enabled ? "on" : "off");
}

// --- The pictures ---

struct Picture {
	enum class State { Loading, Taken, Ready } state = State::Loading;
	uint8_t* object = nullptr; // the game's texture, until its Direct3D texture is taken
	int frames = 0;
	mod::GameTexture texture;                 // Taken and Ready: with our reference
	ID3D11ShaderResourceView* view = nullptr; // Ready: made on the render thread
};

// The cycle, its pictures (by entry) and what the shell said this frame.
std::mutex s_mutex;
std::vector<std::string> s_paths; // the cycle's entries: the art set's paths in use
Cycle s_cycle;
std::map<int, Picture> s_pictures;
bool s_allowed = false; // render thread: the shell allows the backdrop this frame
std::string s_screen;   // render thread: the shell's screen this frame
int s_advancedFrame = -1;
std::atomic<ULONGLONG> s_wantedAt{0};
double s_lastLoadMs = 0;
// The scrim, easing from one kind of page's to another's (render thread).
backdrop::Scrim s_scrim, s_scrimFrom, s_scrimTo;
float s_scrimProgress = 1;
bool s_scrimSet = false;
// The setting's last turn off has let its pictures go.
bool s_letGo = false;

// The game's textures are typeless where it views them as sRGB; Ember draws
// their stored values as they are, as SfvArt's ViewFormat does.
DXGI_FORMAT ViewFormat(unsigned format)
{
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
	case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
	case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
	case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
	default: return (DXGI_FORMAT)format;
	}
}

// Render thread: the view of a taken texture, on the texture's own device.
bool MakeView(Picture& picture)
{
	ID3D11Device* device = nullptr;
	picture.texture.texture->GetDevice(&device);
	D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
	desc.Format = ViewFormat(picture.texture.format);
	desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	desc.Texture2D.MipLevels = 1;
	const HRESULT made = device ? device->CreateShaderResourceView(picture.texture.texture, &desc, &picture.view) : E_FAIL;
	if (device) device->Release();
	if (FAILED(made)) mod::Log("backdrop: no view of format %u (0x%08lX)", picture.texture.format, (unsigned long)made);
	return SUCCEEDED(made);
}

void Release(Picture& picture)
{
	if (picture.view) picture.view->Release();
	if (picture.texture.texture) picture.texture.texture->Release();
	picture.view = nullptr;
	picture.texture = {};
}

const std::string& PathOf(int entry)
{
	static const std::string none;
	return entry >= 0 && entry < (int)s_paths.size() ? s_paths[(size_t)entry] : none;
}

// Render thread, under the lock, before anything is drawn this frame: the
// cycle's pictures let go, everything let go while the setting is off, and
// the views of the textures taken since.
void SettleLocked()
{
	if (s_enabled) {
		s_letGo = false;
	} else if (!s_letGo) {
		s_letGo = true;
		s_cycle.Reset();
		mod::Log("backdrop: off; its pictures let go");
	}
	for (int entry : s_cycle.TakeReleases()) {
		const auto found = s_pictures.find(entry);
		if (found == s_pictures.end()) continue;
		Release(found->second);
		s_pictures.erase(found);
	}
	for (auto at = s_pictures.begin(); at != s_pictures.end();) {
		Picture& picture = at->second;
		if (picture.state != Picture::State::Taken) {
			++at;
			continue;
		}
		const bool viewed = MakeView(picture);
		if (viewed) picture.state = Picture::State::Ready;
		// The cycle refuses a picture it no longer wants (turned off since).
		if (s_cycle.Loaded(at->first, viewed)) {
			++at;
			continue;
		}
		Release(picture);
		at = s_pictures.erase(at);
	}
}

void EaseScrim(float seconds)
{
	const backdrop::Scrim target = backdrop::ScrimOf(backdrop::PageKindOf(s_screen));
	if (!s_scrimSet) {
		s_scrim = s_scrimFrom = s_scrimTo = target;
		s_scrimProgress = 1;
		s_scrimSet = true;
		return;
	}
	if (!(target == s_scrimTo)) {
		s_scrimFrom = s_scrim;
		s_scrimTo = target;
		s_scrimProgress = 0;
	}
	s_scrimProgress = (std::min)(1.f, s_scrimProgress + seconds / (float)backdrop::kScrimSeconds);
	s_scrim = backdrop::Mix(s_scrimFrom, s_scrimTo, s_scrimProgress);
}

ImU32 Ink(float alpha)
{
	return sf4e::ui::palette::InkAt((int)std::lround((std::max)(0.f, (std::min)(1.f, alpha)) * 255));
}

// Render thread, inside a page's DrawMenuBackdrop: the pictures shown and the
// page's scrim over the whole screen; false (the shell's plain ground) while
// the shell does not allow it, the setting is off or nothing has loaded.
bool DrawBackdrop(ImDrawList* draw, ImVec2 min, ImVec2 max)
{
	std::lock_guard lock(s_mutex);
	if (!s_allowed || !s_enabled || s_cycle.Exhausted()) return false;
	// The clock runs once a frame, while the backdrop is on screen.
	if (ImGui::GetFrameCount() != s_advancedFrame) {
		s_advancedFrame = ImGui::GetFrameCount();
		s_wantedAt = GetTickCount64();
		const float seconds = ImGui::GetIO().DeltaTime;
		s_cycle.Advance(seconds);
		// What the step let go was not drawn this frame: the first draw is this one.
		SettleLocked();
		EaseScrim(seconds);
	}
	const Cycle::Frame frame = s_cycle.Shown();
	if (frame.Empty()) return false;
	draw->AddRectFilled(min, max, Ink(1));
	for (const Cycle::Layer& layer : {frame.under, frame.over}) {
		const auto found = s_pictures.find(layer.entry);
		if (layer.entry < 0 || found == s_pictures.end() || !found->second.view) continue;
		const Picture& picture = found->second;
		const backdrop::UvRect uv = backdrop::CoverFit(picture.texture.width, picture.texture.height, max.x - min.x, max.y - min.y);
		const backdrop::Tint tint = backdrop::kArtTint;
		draw->AddImage(reinterpret_cast<ImTextureID>(picture.view), min, max, ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1),
			IM_COL32(tint.r, tint.g, tint.b, (int)std::lround(layer.alpha * 255)));
	}
	draw->AddRectFilledMultiColor(min, max, Ink(s_scrim.topLeft), Ink(s_scrim.topRight), Ink(s_scrim.bottomRight),
		Ink(s_scrim.bottomLeft));
	return true;
}

bool AtMainMenu()
{
	return mod::SceneStateKind(mod::CurrentSceneState()) == mod::sfv::scene::kMainMenuVtable;
}

// --- The development build's load report and contact sheets ---

constexpr int kTileWidth = 128, kTileHeight = 72, kSheetColumns = 12, kSheetRows = 12;
constexpr int kSheetWidth = kTileWidth * kSheetColumns, kSheetHeight = kTileHeight * kSheetRows;

struct ReportRow {
	int index = -1;
	std::string result;
	int width = 0, height = 0;
	unsigned format = 0;
	double loadMs = 0;
	int sheet = -1, cell = -1;
};

struct Report {
	bool running = false, sheets = false, loadingNow = false;
	std::filesystem::path folder;
	std::vector<backdrop::ListedPath> paths;
	size_t next = 0;
	int done = 0, loaded = 0, fullSize = 0, gap = 0;
	// The picture loading, until its texture is taken (game thread).
	uint8_t* object = nullptr;
	int frames = 0;
	ReportRow row;
	// A texture for the render thread to sample into the sheet, and its row.
	std::optional<mod::GameTexture> tile;
	// The sheet being filled (render thread), BGRA, top-down.
	std::vector<uint8_t> sheet;
	int sheetNumber = 0, cells = 0;
	std::string last;
};
std::mutex s_reportMutex;
Report s_report;

std::filesystem::path ReportFile()
{
	return s_report.folder / L"backdrop-report.tsv";
}

// Under the report's lock: the row appended to the report.
void FinishRowLocked(const ReportRow& row)
{
	const backdrop::ListedPath& listed = s_report.paths[(size_t)row.index];
	std::ofstream file(ReportFile(), std::ios::app | std::ios::binary);
	char ms[32];
	snprintf(ms, sizeof ms, "%.1f", row.loadMs);
	file << row.index << '\t' << listed.family << '\t' << (listed.used ? "used" : "off") << '\t' << listed.path << '\t'
		 << row.result << '\t' << row.width << '\t' << row.height << '\t' << row.format << '\t' << ms << '\t'
		 << (row.sheet >= 0 ? std::to_string(row.sheet) : std::string()) << '\t'
		 << (row.cell >= 0 ? std::to_string(row.cell / kSheetColumns) + "," + std::to_string(row.cell % kSheetColumns) : std::string())
		 << "\n";
	s_report.done++;
	s_report.last = listed.path + ": " + row.result;
}

// Under the report's lock (render thread): the sheet being filled, written.
void WriteSheetLocked()
{
	if (!s_report.cells) return;
	const std::vector<std::uint8_t> bmp = overlay::grab::EncodeBmp(s_report.sheet.data(), kSheetWidth, kSheetHeight,
		kSheetWidth * 4, overlay::grab::PixelFormat::B8G8R8A8);
	wchar_t name[64];
	swprintf(name, std::size(name), L"backdrop-sheet-%02d.bmp", s_report.sheetNumber);
	std::ofstream file(s_report.folder / name, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char*>(bmp.data()), (std::streamsize)bmp.size());
	mod::Log("backdrop: report: sheet %d written (%d pictures)", s_report.sheetNumber, s_report.cells);
	s_report.sheetNumber++;
	s_report.cells = 0;
	std::fill(s_report.sheet.begin(), s_report.sheet.end(), (uint8_t)0);
}

void CloseReportLocked()
{
	if (s_report.sheets) WriteSheetLocked();
	s_report.running = false;
	mod::Log("backdrop: report: %d paths tried, %d loaded (%d at 1920x1080), in %ls", s_report.done, s_report.loaded,
		s_report.fullSize, ReportFile().c_str());
}

// Render thread: one picture as the main menu shows it (cover fit, the art's
// tint, the column page's scrim over the theme's ink), averaged down into its
// tile. False (the tile left dark) for a format other than 8-bit RGBA or
// BGRA, or a copy that cannot be read.
bool SampleTile(const mod::GameTexture& texture, std::vector<uint8_t>& sheet, int cell, std::string& why)
{
	const overlay::grab::PixelFormat layout = overlay::grab::PixelFormatOf(texture.format);
	if (layout != overlay::grab::PixelFormat::B8G8R8A8 && layout != overlay::grab::PixelFormat::R8G8B8A8) {
		why = "not sampled (format " + std::to_string(texture.format) + ")";
		return false;
	}
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	texture.texture->GetDevice(&device);
	device->GetImmediateContext(&context);
	D3D11_TEXTURE2D_DESC desc{};
	texture.texture->GetDesc(&desc);
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	ID3D11Texture2D* staging = nullptr;
	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT result = device->CreateTexture2D(&desc, nullptr, &staging);
	if (SUCCEEDED(result)) {
		context->CopyResource(staging, texture.texture);
		result = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
	}
	if (SUCCEEDED(result)) {
		const auto* pixels = static_cast<const uint8_t*>(mapped.pData);
		const bool bgra = layout == overlay::grab::PixelFormat::B8G8R8A8;
		const backdrop::UvRect uv = backdrop::CoverFit(texture.width, texture.height, (float)kTileWidth, (float)kTileHeight);
		const backdrop::Scrim scrim = backdrop::ScrimOf(backdrop::PageKind::Column);
		const backdrop::Tint tint = backdrop::kArtTint;
		const unsigned ink = ::ember::ProductTheme().ink;
		const float inkRgb[3] = {(float)(ink >> 16 & 255), (float)(ink >> 8 & 255), (float)(ink & 255)};
		const float tintRgb[3] = {tint.r / 255.f, tint.g / 255.f, tint.b / 255.f};
		const int cellX = cell % kSheetColumns * kTileWidth, cellY = cell / kSheetColumns * kTileHeight;
		for (int ty = 0; ty < kTileHeight; ty++) {
			const float v0 = uv.v0 + (uv.v1 - uv.v0) * ty / kTileHeight, v1 = uv.v0 + (uv.v1 - uv.v0) * (ty + 1) / kTileHeight;
			const int y0 = (std::min)(texture.height - 1, (int)(v0 * texture.height));
			const int y1 = (std::max)(y0 + 1, (std::min)(texture.height, (int)std::ceil(v1 * texture.height)));
			for (int tx = 0; tx < kTileWidth; tx++) {
				const float u0 = uv.u0 + (uv.u1 - uv.u0) * tx / kTileWidth, u1 = uv.u0 + (uv.u1 - uv.u0) * (tx + 1) / kTileWidth;
				const int x0 = (std::min)(texture.width - 1, (int)(u0 * texture.width));
				const int x1 = (std::max)(x0 + 1, (std::min)(texture.width, (int)std::ceil(u1 * texture.width)));
				double sum[3] = {};
				for (int y = y0; y < y1; y++) {
					const uint8_t* row = pixels + (size_t)y * mapped.RowPitch;
					for (int x = x0; x < x1; x++) {
						const uint8_t* p = row + (size_t)x * 4;
						sum[0] += bgra ? p[2] : p[0];
						sum[1] += p[1];
						sum[2] += bgra ? p[0] : p[2];
					}
				}
				const double count = (double)(y1 - y0) * (x1 - x0);
				const float a = backdrop::ScrimAt(scrim, (tx + .5f) / kTileWidth, (ty + .5f) / kTileHeight);
				uint8_t* out = &sheet[((size_t)(cellY + ty) * kSheetWidth + (size_t)(cellX + tx)) * 4];
				for (int c = 0; c < 3; c++) {
					const float art = (float)(sum[c] / count) * tintRgb[c];
					const float value = art * (1 - a) + inkRgb[c] * a;
					out[2 - c] = (uint8_t)std::lround((std::max)(0.f, (std::min)(255.f, value))); // B, G, R
				}
				out[3] = 255;
			}
		}
		context->Unmap(staging, 0);
	} else {
		why = "not sampled (its copy could not be read, 0x" + std::to_string((unsigned long)result) + ")";
	}
	if (staging) staging->Release();
	context->Release();
	device->Release();
	return SUCCEEDED(result);
}

// Render thread, every frame: a texture the report handed over, sampled into
// the sheet, let go, and its row written.
void ReportRenderWork()
{
	std::optional<mod::GameTexture> tile;
	ReportRow row;
	int cell = 0;
	{
		std::lock_guard lock(s_reportMutex);
		if (!s_report.running || !s_report.tile) return;
		tile = s_report.tile;
		row = s_report.row;
		cell = s_report.cells;
	}
	std::string why;
	const bool sampled = SampleTile(*tile, s_report.sheet, cell, why);
	tile->texture->Release();
	std::lock_guard lock(s_reportMutex);
	if (sampled) {
		row.sheet = s_report.sheetNumber;
		row.cell = cell;
		s_report.cells++;
	} else {
		row.result += " (" + why + ")";
	}
	FinishRowLocked(row);
	s_report.tile.reset();
	if (s_report.cells == kSheetColumns * kSheetRows) WriteSheetLocked();
}

// Game thread, at the main menu: the report's next step. One picture at a
// time, a load every other frame while the game loads nothing.
void PumpReport(bool gameLoading)
{
	if constexpr (kBuildKind != BuildKind::Development) return;
	std::wstring path;
	{
		std::lock_guard lock(s_reportMutex);
		if (!s_report.running || s_report.tile || s_report.loadingNow) return;
		if (s_report.object) {
			bool pending = false;
			std::string why;
			const std::optional<mod::GameTexture> texture = mod::TakeGameTexture(s_report.object, &pending, &why);
			if (!texture && pending && ++s_report.frames < kResourceFrames) return;
			s_report.object = nullptr;
			ReportRow& row = s_report.row;
			if (!texture) {
				row.result = pending ? "its resource never came" : why;
				FinishRowLocked(row);
				return;
			}
			row.result = "loaded";
			row.width = texture->width;
			row.height = texture->height;
			row.format = texture->format;
			s_report.loaded++;
			if (texture->width == 1920 && texture->height == 1080) s_report.fullSize++;
			if (s_report.sheets) {
				s_report.tile = *texture; // the render thread samples it, lets it go and writes the row
				return;
			}
			texture->texture->Release(); // never drawn: let go here
			FinishRowLocked(row);
			return;
		}
		if (s_report.next >= s_report.paths.size()) {
			CloseReportLocked();
			return;
		}
		if (gameLoading || ++s_report.gap < 2) return;
		s_report.gap = 0;
		s_report.row = {};
		s_report.row.index = (int)s_report.next;
		path = backdrop::ObjectPathOf(s_report.paths[s_report.next++].path);
		s_report.loadingNow = true;
	}
	const auto started = std::chrono::steady_clock::now();
	uint8_t* object = path.empty() ? nullptr : mod::LoadTexture(path);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	std::lock_guard lock(s_reportMutex);
	s_report.loadingNow = false;
	s_report.row.loadMs = ms;
	if (!object) {
		s_report.row.result = "the game has no texture there";
		FinishRowLocked(s_report.row);
		return;
	}
	s_report.object = object;
	s_report.frames = 0;
}

bool ReportRunning()
{
	std::lock_guard lock(s_reportMutex);
	return s_report.running;
}

std::string StartReport(const std::string& args)
{
	if constexpr (kBuildKind != BuildKind::Development) {
		return "error the load report is for development builds";
	} else {
		std::string rest = args;
		bool sheets = false;
		if (rest.rfind("sheets ", 0) == 0) {
			sheets = true;
			rest = rest.substr(7);
		}
		while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
		if (rest.empty()) return "error usage: backdrop report [sheets] <folder>";
		std::lock_guard lock(s_reportMutex);
		if (s_report.running) return "error a report is running";
		std::wstring wide((size_t)MultiByteToWideChar(CP_UTF8, 0, rest.data(), (int)rest.size(), nullptr, 0), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, rest.data(), (int)rest.size(), wide.data(), (int)wide.size());
		const std::filesystem::path folder(wide);
		std::error_code error;
		std::filesystem::create_directories(folder, error);
		Report fresh;
		fresh.folder = folder;
		fresh.sheets = sheets;
		fresh.paths = backdrop::AllPaths(backdrop::EmbeddedArtSet());
		if (sheets) fresh.sheet.assign((size_t)kSheetWidth * kSheetHeight * 4, 0);
		std::ofstream file(folder / L"backdrop-report.tsv", std::ios::binary | std::ios::trunc);
		if (!file) return "error cannot write " + rest;
		file << "index\tfamily\tuse\tpath\tresult\twidth\theight\tformat\tload_ms\tsheet\tcell\n";
		file.close();
		fresh.running = true;
		s_report = std::move(fresh);
		mod::Log("backdrop: report of %zu paths into %ls%s", s_report.paths.size(), folder.c_str(), sheets ? ", with contact sheets" : "");
		return "ok " + std::to_string(s_report.paths.size()) + " paths";
	}
}

// --- The game thread's loader ---

// Game thread, away from the main menu: a picture loaded just before the
// game left it is not taken later (the game may collect its object on the
// way): it is dropped, skipped for the session as a failure is.
void DropLoading()
{
	{
		std::lock_guard lock(s_mutex);
		for (auto at = s_pictures.begin(); at != s_pictures.end();) {
			if (at->second.state != Picture::State::Loading) {
				++at;
				continue;
			}
			mod::Log("backdrop: %s: dropped: the main menu was left while it loaded", PathOf(at->first).c_str());
			s_cycle.Loaded(at->first, false);
			at = s_pictures.erase(at);
		}
	}
	std::lock_guard lock(s_reportMutex);
	if (s_report.running && s_report.object) {
		s_report.object = nullptr;
		s_report.row.result = "dropped: the main menu was left while it loaded";
		FinishRowLocked(s_report.row);
	}
}

void Pump()
{
	if (!AtMainMenu()) return DropLoading(); // never in a battle or on its way to one
	const bool gameLoading = mod::GameIsLoading();
	PumpReport(gameLoading);
	const bool reporting = ReportRunning();
	std::wstring path;
	int entry = -1;
	{
		std::lock_guard lock(s_mutex);
		bool inFlight = false;
		for (auto at = s_pictures.begin(); at != s_pictures.end();) {
			Picture& picture = at->second;
			if (picture.state != Picture::State::Loading) {
				inFlight = inFlight || picture.state == Picture::State::Taken;
				++at;
				continue;
			}
			bool pending = false;
			std::string why;
			const std::optional<mod::GameTexture> texture = mod::TakeGameTexture(picture.object, &pending, &why);
			if (!texture && pending && ++picture.frames < kResourceFrames) {
				inFlight = true;
				++at;
				continue;
			}
			picture.object = nullptr;
			if (texture && (texture->width < kLeastWidth || texture->height < kLeastHeight)) {
				why = "only " + std::to_string(texture->width) + "x" + std::to_string(texture->height);
				texture->texture->Release(); // never drawn: let go here
			} else if (texture) {
				picture.texture = *texture;
				picture.state = Picture::State::Taken; // the render thread makes its view
				inFlight = true;
				mod::Log("backdrop: %s: %dx%d, format %u", PathOf(at->first).c_str(), texture->width, texture->height, texture->format);
				++at;
				continue;
			}
			mod::Log("backdrop: %s: skipped: %s", PathOf(at->first).c_str(), pending ? "its resource never came" : why.c_str());
			s_cycle.Loaded(at->first, false);
			at = s_pictures.erase(at);
		}
		// One load at a time, only while the backdrop is on screen and the
		// game loads nothing, and none while the report runs.
		if (inFlight || !s_enabled || gameLoading || GetTickCount64() - s_wantedAt.load() > kWantedMs || reporting) return;
		const std::optional<int> wanted = s_cycle.Wanted();
		if (!wanted) return;
		entry = *wanted;
		s_cycle.Started(entry);
		path = backdrop::ObjectPathOf(PathOf(entry));
	}
	const auto started = std::chrono::steady_clock::now();
	uint8_t* object = path.empty() ? nullptr : mod::LoadTexture(path);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	std::lock_guard lock(s_mutex);
	s_lastLoadMs = ms;
	if (!object) {
		mod::Log("backdrop: %s: skipped: the game has no texture there (%.1f ms)", PathOf(entry).c_str(), ms);
		s_cycle.Loaded(entry, false);
		return;
	}
	mod::Log("backdrop: %s: loaded in %.1f ms", PathOf(entry).c_str(), ms);
	Picture& picture = s_pictures[entry];
	picture = {};
	picture.object = object;
}

// --- The pipe ---

std::string Command(const std::string& args)
{
	if (args == "step") {
		std::lock_guard lock(s_mutex);
		s_cycle.Step();
		return "ok next " + PathOf(s_cycle.Next());
	}
	if (args == "on" || args == "off") {
		return ShowSfvBackdrop(args == "on") ? "ok " + args : "error the choice could not be saved";
	}
	if (args.rfind("report", 0) == 0) return StartReport(args.size() > 6 ? args.substr(7) : std::string());
	return "error usage: backdrop step | on | off | report [sheets] <folder>";
}

std::string StatusJson()
{
	std::string json;
	{
		std::lock_guard lock(s_mutex);
		const Cycle::Frame frame = s_cycle.Shown();
		char numbers[160];
		snprintf(numbers, sizeof numbers, ",\"held\":%.1f,\"alive\":%d,\"failed\":%d,\"entries\":%d,\"lastLoadMs\":%.1f", s_cycle.Held(),
			s_cycle.Alive(), s_cycle.Failed(), s_cycle.Entries(), s_lastLoadMs);
		json = std::string("{\"enabled\":") + (s_enabled ? "true" : "false") + ",\"allowed\":" + (s_allowed ? "true" : "false") +
			",\"drawn\":" + (frame.Empty() ? "false" : "true") + ",\"current\":" + mod::JsonString(PathOf(s_cycle.Current())) +
			",\"next\":" + mod::JsonString(PathOf(s_cycle.Next())) + ",\"nextReady\":" + (s_cycle.NextReady() ? "true" : "false") +
			",\"fading\":" + (s_cycle.Fading() ? "true" : "false") + numbers + ",\"exhausted\":" + (s_cycle.Exhausted() ? "true" : "false") +
			",\"page\":" + mod::JsonString(backdrop::PageKindName(backdrop::PageKindOf(s_screen)));
	}
	std::lock_guard lock(s_reportMutex);
	json += ",\"report\":{\"running\":" + std::string(s_report.running ? "true" : "false") + ",\"done\":" + std::to_string(s_report.done) +
		",\"total\":" + std::to_string(s_report.paths.size()) + ",\"loaded\":" + std::to_string(s_report.loaded) +
		",\"sheets\":" + std::to_string(s_report.sheetNumber) + ",\"last\":" + mod::JsonString(s_report.last) + "}}";
	return json;
}

} // namespace

bool SfvBackdropShown()
{
	std::lock_guard lock(s_choiceMutex);
	LoadChoiceLocked();
	return s_enabled;
}

bool ShowSfvBackdrop(bool shown)
{
	std::lock_guard lock(s_choiceMutex);
	LoadChoiceLocked();
	// The file's other choices stay as they are.
	nlohmann::json choices = ReadChoices();
	choices[kChoiceKey] = shown;
	std::string error;
	if (!json_file::Publish(Folder(), kChoiceFile, choices, error)) {
		mod::Log("backdrop: the choice could not be saved (%s); it stays %s", error.c_str(), s_enabled ? "on" : "off");
		return false;
	}
	// The render thread lets the pictures go at its next frame (SettleLocked).
	s_enabled = shown;
	mod::Log("backdrop: background artwork %s, saved", shown ? "on" : "off");
	return true;
}

void BackdropFrame(bool allowed, const std::string& screen)
{
	{
		std::lock_guard lock(s_mutex);
		s_allowed = allowed;
		s_screen = screen;
		SettleLocked();
	}
	ReportRenderWork();
}

void InstallSfvBackdrop()
{
	SfvBackdropShown(); // read the choice once, at start
	{
		std::lock_guard lock(s_mutex);
		s_paths = backdrop::UsedPaths(backdrop::EmbeddedArtSet());
		// A new order each session.
		const auto seed = (std::uint32_t)(GetTickCount64() ^ (GetCurrentProcessId() * 2654435761u));
		s_cycle = Cycle((int)s_paths.size(), seed);
	}
	mod::Log("backdrop: %zu pictures in the cycle, %zu listed", s_paths.size(), backdrop::AllPaths(backdrop::EmbeddedArtSet()).size());
	mod::RunEachGameFrame([] {
		Pump();
		return true;
	});
	sf4e::ui::SetMenuBackdropDrawer(DrawBackdrop);
	mod::RegisterCommand("backdrop", Command);
	mod::RegisterStatus("backdrop", StatusJson);
}

} // namespace sfve::adapter
