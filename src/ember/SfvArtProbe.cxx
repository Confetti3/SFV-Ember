#include "ember/SfvArtProbe.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvPictureSpec.hxx"
#include "mod/BuildCheck.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include "ui/V2.hxx"

#include <imgui.h>
#include <windows.h>

#include <wchar.h>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::adapter {

namespace {

namespace sfv = mod::sfv;
using mod::Field;
using mod::GameString;

// What the probe reads beyond GameContent's walk, from the exe (build
// 13173604). They are verified here, before the first call, and never
// enter the build's own site list: the probe is development only.
//
// The shop's DataTable is the spec data's +64 (rva 0x561B60 finds a shop
// item so). UDataTable's GetRowNames (rva 0x1A9C820) fills a TArray of the
// rows' FNames (8 bytes each), freed with the game's free; the shop's row
// lookup (rva 0x53C890) takes the table and one FName by value and returns
// the row's data, or null for none or a table of another row struct.
constexpr mod::CodeSite kRowNames{"DataTableRowNames", 0x1A9C820,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xC0, 0x48, 0x83, 0xC1, 0x40, 0x48, 0x8B, 0xDA, 0x48}};
constexpr mod::CodeSite kFindShopRow{"FindShopRow", 0x53C890,
	{0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x28, 0x00, 0x48}};
constexpr mod::CodeSite kProbeSites[] = {kRowNames, kFindShopRow};
constexpr uint32_t kSpecShopTable = 64;
// FKWShopItemSpec (registered at rva 0xD04630): +0 Id (FString), +16 Type
// (int; 1 is Costume), +24 TradeName (FText), +104 Thumbnail (FString).
constexpr uint32_t kShopId = 0;
constexpr uint32_t kShopType = 16;
constexpr uint32_t kShopTradeName = 24;
constexpr uint32_t kShopThumbnail = 104;
// FKWCharaSpec (rva 0xD00860), a spec costume's linked row: +48
// RefCostumeAvailableCode (FString), +64 CostumeText (FText).
constexpr uint32_t kCharaSpecCostumeCode = 48;
constexpr uint32_t kCharaSpecCostumeText = 64;

template <typename T>
struct GameArray {
	T* data = nullptr;
	int32_t count = 0;
	int32_t capacity = 0;
};

using GameDataFn = uint8_t* (*)();
using SpecDataFn = uint8_t* (*)(uint8_t* singleton);
using FindFn = int32_t* (*)(uint8_t* set, int32_t* outIndex, int32_t key);
using TextToStringFn = const GameString* (*)(const void* text);
using FreeFn = void (*)(void* block);
using RowNamesFn = GameArray<uint64_t>* (*)(uint8_t* table, GameArray<uint64_t>* out);
using FindRowFn = uint8_t* (*)(uint8_t* table, uint64_t name);

template <typename Fn>
Fn At(const mod::CodeSite& site)
{
	return reinterpret_cast<Fn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + site.rva);
}

std::mutex s_mutex;
std::wstring s_shown; // the picture `art show` named; empty for none

std::wstring Wide(std::string_view text)
{
	return std::wstring(text.begin(), text.end());
}

std::string Narrow(std::wstring_view text)
{
	std::string out;
	for (wchar_t c : text) out += c < 0x80 ? (char)c : '?';
	return out;
}

std::wstring_view TextOf(const GameString& s)
{
	return s.data && s.count > 0 ? std::wstring_view(s.data, wcsnlen(s.data, (size_t)s.count)) : std::wstring_view();
}

std::string Utf8(std::wstring_view text)
{
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string out((size_t)(bytes > 0 ? bytes : 0), '\0');
	if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), bytes, nullptr, nullptr);
	return out;
}

// An FText as shown, in the game's language.
std::string Shown(uint8_t* text)
{
	const GameString* shown = At<TextToStringFn>(sfv::kTextToString)(text);
	return shown ? Utf8(TextOf(*shown)) : std::string();
}

// An object path the probe may ask for: "/Game/" and then letters, digits,
// '_', '/' and '.' only.
bool IsObjectPath(const std::string& path)
{
	if (path.rfind("/Game/", 0) != 0 || path.size() > 200) return false;
	return std::all_of(path.begin(), path.end(),
		[](unsigned char c) { return std::isalnum(c) || c == '_' || c == '/' || c == '.'; });
}

// A fighter code as the game writes one: two to four capitals or digits.
bool IsFighterCode(const std::string& code)
{
	return code.size() >= 2 && code.size() <= 4 &&
		std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isupper(c) || std::isdigit(c); });
}

bool SameCode(std::wstring_view a, std::wstring_view b)
{
	return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

// The value of a spec set's element keyed `key` when the builder linked a
// row to it, as GameContent's LinkedValue reads it; null otherwise.
uint8_t* LinkedValue(const mod::CodeSite& find, uint8_t* set, uint32_t stride, int32_t key)
{
	int32_t index = -1;
	At<FindFn>(find)(set, &index, key);
	uint8_t* elements = Field<uint8_t*>(set, sfv::spec::kSetElements);
	if (index < 0 || !elements) return nullptr;
	uint8_t* value = elements + (size_t)stride * (size_t)index + sfv::spec::kValue;
	return Field<void*>(value, sfv::spec::kRow) ? value : nullptr;
}

// Each spec costume of the fighter: its number, its CostumeDatas folder, its
// name and the entitlement code its row names.
void LogCostumes(uint8_t* charaAsset, uint8_t* spec, const std::string& code)
{
	const std::wstring wide = Wide(code);
	uint8_t* entries = Field<uint8_t*>(charaAsset, sfv::content::kCharaEntries);
	const int count = Field<int32_t>(charaAsset, sfv::content::kCharaCount);
	uint8_t* entry = nullptr;
	for (int i = 0; entries && i < count && !entry; i++) {
		uint8_t* at = entries + (size_t)sfv::content::kCharaStride * (size_t)i;
		if (SameCode(TextOf(Field<GameString>(at, 0)), wide)) entry = at;
	}
	if (!entry) return mod::Log("art: data %s: no such fighter in the fighter table", code.c_str());
	uint8_t* fighter = LinkedValue(sfv::kFindSpecFighter, spec + sfv::spec::kFighters, sfv::spec::kFighterStride,
		Field<int32_t>(entry, sfv::content::kCharaUniqueId));
	uint8_t* datas = Field<uint8_t*>(entry, sfv::content::kCharaCostumeDatas);
	const int costumes = Field<int32_t>(entry, sfv::content::kCharaCostumeCount);
	for (int number = 0; datas && number < costumes; number++) {
		const GameString& folder = Field<GameString>(datas + (size_t)sfv::content::kGameStringStride * (size_t)number, 0);
		uint8_t* value = fighter ? LinkedValue(sfv::kFindSpecCostume, fighter + sfv::spec::kFighterCostumes,
			sfv::spec::kCostumeStride, number) : nullptr;
		uint8_t* row = value ? Field<uint8_t*>(value, sfv::spec::kRow) : nullptr;
		mod::Log("art: data %s costume %d folder %s \"%s\" ref %s", code.c_str(), number, Narrow(TextOf(folder)).c_str(),
			row ? Shown(row + kCharaSpecCostumeText).c_str() : "", row ? Narrow(TextOf(Field<GameString>(row, kCharaSpecCostumeCode))).c_str() : "(no row)");
	}
}

// The shop's rows whose thumbnail is the fighter's: shp_<CODE>_*.
void LogShopRows(uint8_t* spec, const std::string& code)
{
	std::string detail;
	const mod::GameBuild probe{"the art probe's sites", sfv::kExeTimestamp, kProbeSites, std::size(kProbeSites)};
	if (mod::CheckBuild(GetModuleHandleW(nullptr), &probe, &detail) != mod::BuildState::Verified) {
		return mod::Log("art: data %s: the shop is not read: %s", code.c_str(), detail.c_str());
	}
	uint8_t* table = Field<uint8_t*>(spec, kSpecShopTable);
	if (!table) return mod::Log("art: data %s: the spec has no shop table", code.c_str());
	GameArray<uint64_t> names;
	At<RowNamesFn>(kRowNames)(table, &names);
	const std::wstring prefix = L"shp_" + Wide(code) + L"_";
	int rows = 0, matched = 0;
	for (int i = 0; names.data && i < names.count; i++) {
		uint8_t* row = At<FindRowFn>(kFindShopRow)(table, names.data[i]);
		if (!row) continue;
		rows++;
		const std::wstring_view thumbnail = TextOf(Field<GameString>(row, kShopThumbnail));
		if (thumbnail.size() < prefix.size() || !SameCode(thumbnail.substr(0, prefix.size()), prefix)) continue;
		matched++;
		mod::Log("art: data %s shop %s type %d thumb %s \"%s\"", code.c_str(), Narrow(TextOf(Field<GameString>(row, kShopId))).c_str(),
			Field<int32_t>(row, kShopType), Narrow(thumbnail).c_str(), Shown(row + kShopTradeName).c_str());
	}
	if (names.data) At<FreeFn>(sfv::kGameFree)(names.data);
	mod::Log("art: data %s: %d of the shop's %d rows are the fighter's", code.c_str(), matched, rows);
}

// Game thread.
void LogArtData(const std::string& code)
{
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* charaAsset = singleton ? Field<uint8_t*>(singleton, sfv::content::kCharaAsset) : nullptr;
	uint8_t* spec = singleton ? At<SpecDataFn>(sfv::kSpecData)(singleton) : nullptr;
	if (!charaAsset || !spec) return mod::Log("art: data %s: the game's tables are not there yet", code.c_str());
	LogCostumes(charaAsset, spec, code);
	LogShopRows(spec, code);
}

std::string ArtCommand(const std::string& args)
{
	std::istringstream in(args);
	std::string verb;
	in >> verb;
	std::vector<std::string> words;
	for (std::string word; in >> word;) words.push_back(word);
	if (verb == "load" && !words.empty()) {
		for (const std::string& path : words) {
			if (!IsObjectPath(path)) return "error not an object path: " + path;
		}
		for (const std::string& path : words) RequestPicture({Wide(path)});
		return "ok queued " + std::to_string(words.size()) + " (loaded at the main menu; see the log)";
	}
	if (verb == "show" && words.size() <= 1) {
		if (!words.empty() && !IsObjectPath(words[0])) return "error not an object path: " + words[0];
		const std::wstring path = words.empty() ? std::wstring() : Wide(words[0]);
		RequestPicture({path});
		std::lock_guard lock(s_mutex);
		s_shown = path;
		return words.empty() ? "ok hidden" : "ok showing " + words[0];
	}
	if (verb == "data" && words.size() == 1 && IsFighterCode(words[0])) {
		const std::string code = words[0];
		mod::RunOnGameThread([code] { LogArtData(code); });
		return "ok queued (see the log)";
	}
	return "error usage: art load <path> [<path> ...] | art show [<path>] | art data <CODE>";
}

} // namespace

void RegisterArtProbe()
{
	mod::RegisterCommand("art", ArtCommand);
}

void DrawArtProbe()
{
	std::wstring path;
	{
		std::lock_guard lock(s_mutex);
		path = s_shown;
	}
	if (path.empty()) return;
	const auto& colors = sf4e::ui::v2::Colors();
	const sf4e::ui::SelectionImage image = PictureImage({path});
	const std::string caption = Narrow(path) + (image.missing ? ": missing (see the log)" : !image.texture ? ": loading" :
		": " + std::to_string(image.width) + "x" + std::to_string(image.height));
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	const ImVec2 at(8, 8), text = ImGui::CalcTextSize(caption.c_str());
	const float width = (std::max)(text.x, (float)image.width), height = text.y + 4 + (image.texture ? image.height : 0);
	draw->AddRectFilled(ImVec2(at.x - 4, at.y - 4), ImVec2(at.x + width + 4, at.y + height + 4), colors.ink);
	draw->AddText(at, colors.text, caption.c_str());
	if (!image.texture) return;
	const ImVec2 min(at.x, at.y + text.y + 4);
	draw->AddImage(image.texture, min, ImVec2(min.x + image.width, min.y + image.height), image.uvMin, image.uvMax);
}

} // namespace sfve::adapter
