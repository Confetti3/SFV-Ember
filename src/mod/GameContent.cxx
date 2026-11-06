#include "mod/GameContent.hxx"

#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <wchar.h>

#include <algorithm>

namespace sfve::mod {

namespace {

// UE4's TArray: {elements, count, capacity}.
template <typename T>
struct GameArray {
	T* data = nullptr;
	int32_t count = 0;
	int32_t capacity = 0;
};

using GameDataFn = uint8_t* (*)();
using SpecDataFn = uint8_t* (*)(uint8_t* singleton);
using CodeListFn = bool (*)(uint8_t** spec, GameArray<GameString>* out, bool includeRandom);
using FighterIdsFn = bool (*)(uint8_t** spec, GameArray<int32_t>* out);
using CostumeIdsFn = bool (*)(uint8_t** spec, int32_t uniqueId, GameArray<int32_t>* out);
using ColourIdsFn = bool (*)(uint8_t** spec, int32_t uniqueId, int32_t costume, GameArray<int32_t>* out);
using CostumeNameFn = GameString* (*)(void* unused, GameString* out, const GameString* code, int32_t costume);
using TextToStringFn = const GameString* (*)(const void* text);
using FreeFn = void (*)(void* block);
using StageNameFn = GameString* (*)(uint8_t* stageAsset, GameString* out, const GameString* code);

template <typename Fn>
Fn At(const CodeSite& site)
{
	return reinterpret_cast<Fn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + site.rva);
}

void FreeBlock(const void* block)
{
	if (block) At<FreeFn>(sfv::kGameFree)(const_cast<void*>(block));
}

std::string Narrow(const GameString& s)
{
	std::string out;
	for (int i = 0; s.data && i < s.count && s.data[i]; i++) out += (char)s.data[i];
	return out;
}

std::string Utf8(const GameString& s)
{
	if (!s.data || s.count <= 0) return {};
	const int length = (int)wcsnlen(s.data, (size_t)s.count);
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, s.data, length, nullptr, 0, nullptr, nullptr);
	std::string out((size_t)(bytes > 0 ? bytes : 0), '\0');
	if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, s.data, length, out.data(), bytes, nullptr, nullptr);
	return out;
}

// A view of an ASCII code as the game's FString (the game only reads it).
struct CodeView {
	explicit CodeView(const std::string& code) : wide(code.begin(), code.end()) {}
	GameString Get() const { return {wide.c_str(), (int32_t)wide.size() + 1, (int32_t)wide.size() + 1}; }
	std::wstring wide;
};

// One of the game's code lists; the game's array is freed after.
std::vector<std::string> ReadCodes(const CodeSite& lister, uint8_t* spec)
{
	GameArray<GameString> list;
	std::vector<std::string> codes;
	if (At<CodeListFn>(lister)(&spec, &list, false)) {
		for (int i = 0; i < list.count; i++) codes.push_back(Narrow(list.data[i]));
	}
	for (int i = 0; i < list.count; i++) FreeBlock(list.data[i].data);
	FreeBlock(list.data);
	return codes;
}

// One of the spec's number lists, in the lister's order; the array is freed
// after.
template <typename List>
std::vector<int> ReadNumbers(List&& list)
{
	GameArray<int32_t> numbers;
	std::vector<int> out;
	if (list(&numbers)) out.assign(numbers.data, numbers.data + numbers.count);
	FreeBlock(numbers.data);
	return out;
}

// A string the game allocated for its caller, copied and freed.
std::string TakeUtf8(GameString& owned)
{
	std::string text = Utf8(owned);
	FreeBlock(owned.data);
	owned = {};
	return text;
}

// A stage's name as the game's menus show it; empty when it has none. The
// game finds, or loads, the stage's data asset for it.
std::string StageName(uint8_t* stageAsset, const std::string& code)
{
	const CodeView view(code);
	const GameString game = view.Get();
	GameString name{};
	At<StageNameFn>(sfv::kStageName)(stageAsset, &name, &game);
	return TakeUtf8(name);
}

bool Unplayable(const std::string& stage)
{
	for (const wchar_t* code : sfv::content::kUnplayableStages) {
		std::wstring wide(stage.begin(), stage.end());
		if (_wcsicmp(wide.c_str(), code) == 0) return true;
	}
	return stage.empty();
}

// The fighter table's entry with the spec's id for it; null for none.
uint8_t* EntryWithId(uint8_t* charaAsset, int32_t uniqueId)
{
	uint8_t* entries = Field<uint8_t*>(charaAsset, sfv::content::kCharaEntries);
	const int count = Field<int32_t>(charaAsset, sfv::content::kCharaCount);
	for (int i = 0; entries && i < count; i++) {
		uint8_t* entry = entries + (size_t)sfv::content::kCharaStride * i;
		if (Field<int32_t>(entry, sfv::content::kCharaUniqueId) == uniqueId) return entry;
	}
	return nullptr;
}

// The fighter's name in the game's language, from its PersonalData (loaded
// with the fighter table); empty where it has none.
std::string FighterName(uint8_t* entry)
{
	uint8_t* personal = Field<uint8_t*>(entry, sfv::content::kCharaPersonal);
	if (!personal) return {};
	const GameString* name = At<TextToStringFn>(sfv::kTextToString)(personal + sfv::content::kPersonalName);
	return name ? Utf8(*name) : std::string();
}

FighterContent ReadFighter(uint8_t* spec, uint8_t* entry, const std::vector<std::string>& installedFighters)
{
	FighterContent fighter;
	fighter.code = Narrow(Field<GameString>(entry, 0));
	fighter.name = FighterName(entry);
	fighter.installed = std::find(installedFighters.begin(), installedFighters.end(), fighter.code) != installedFighters.end();
	const int32_t id = Field<int32_t>(entry, sfv::content::kCharaUniqueId);
	const std::vector<int> playable =
		ReadNumbers([&](GameArray<int32_t>* out) { return At<CostumeIdsFn>(sfv::kPlayableCostumes)(&spec, id, out); });
	const CodeView code(fighter.code);
	const GameString gameCode = code.Get();
	for (int costumeId :
		ReadNumbers([&](GameArray<int32_t>* out) { return At<CostumeIdsFn>(sfv::kSpecCostumes)(&spec, id, out); })) {
		CostumeContent costume;
		costume.id = costumeId;
		costume.colours = ReadNumbers(
			[&](GameArray<int32_t>* out) { return At<ColourIdsFn>(sfv::kSpecColours)(&spec, id, costumeId, out); });
		GameString name{};
		At<CostumeNameFn>(sfv::kCostumeName)(nullptr, &name, &gameCode, costumeId);
		costume.name = TakeUtf8(name);
		costume.installed = std::find(playable.begin(), playable.end(), costumeId) != playable.end();
		fighter.costumes.push_back(std::move(costume));
	}
	return fighter;
}

// The spec's selectable fighters in its order (the select screen's), each
// with its costumes and colours. A fighter the fighter table does not have,
// or the Random entry, is not one a player can play.
std::vector<FighterContent> ReadFighters(uint8_t* singleton, uint8_t* spec)
{
	uint8_t* charaAsset = Field<uint8_t*>(singleton, sfv::content::kCharaAsset);
	if (!charaAsset) return {};
	const std::vector<std::string> installed = ReadCodes(sfv::kSelectableFighters, spec);
	std::vector<FighterContent> fighters;
	for (int id : ReadNumbers([&](GameArray<int32_t>* out) { return At<FighterIdsFn>(sfv::kSpecFighters)(&spec, out); })) {
		uint8_t* entry = EntryWithId(charaAsset, id);
		if (!entry) continue;
		FighterContent fighter = ReadFighter(spec, entry, installed);
		const std::wstring wide(fighter.code.begin(), fighter.code.end());
		if (fighter.code.empty() || _wcsicmp(wide.c_str(), sfv::content::kRandomFighter) == 0) continue;
		fighters.push_back(std::move(fighter));
	}
	return fighters;
}

} // namespace

ContentLists ReadContentLists()
{
	ContentLists lists;
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* spec = singleton ? At<SpecDataFn>(sfv::kSpecData)(singleton) : nullptr;
	if (!spec) return lists;
	lists.fighters = ReadFighters(singleton, spec);
	for (std::string& stage : ReadCodes(sfv::kSpecStages, spec)) {
		if (!Unplayable(stage)) lists.stages.push_back(std::move(stage));
	}
	// The names only once both lists are there: each may load a stage's data.
	uint8_t* stageAsset = Field<uint8_t*>(singleton, sfv::content::kStageAsset);
	if (lists.fighters.empty() || lists.stages.empty() || !stageAsset) return lists;
	for (const std::string& stage : lists.stages) lists.stageNames.push_back(StageName(stageAsset, stage));
	return lists;
}

} // namespace sfve::mod
