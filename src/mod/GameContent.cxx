#include "mod/GameContent.hxx"

#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <wchar.h>

namespace sfve::mod {

namespace {

// UE4's TArray<FString>: {elements, count, capacity}.
struct GameStringArray {
	GameString* data = nullptr;
	int32_t count = 0;
	int32_t capacity = 0;
};

using GameDataFn = uint8_t* (*)();
using SpecDataFn = uint8_t* (*)(uint8_t* singleton);
using ListFn = bool (*)(uint8_t** spec, GameStringArray* out, bool includeRandom);
using FreeFn = void (*)(void* block);

template <typename Fn>
Fn At(const CodeSite& site)
{
	return reinterpret_cast<Fn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + site.rva);
}

std::string Narrow(const GameString& s)
{
	std::string out;
	for (int i = 0; s.data && i < s.count && s.data[i]; i++) out += (char)s.data[i];
	return out;
}

// One of the game's lists, as codes; the game's array is freed after.
std::vector<std::string> ReadList(const CodeSite& lister, uint8_t* spec)
{
	GameStringArray list;
	std::vector<std::string> codes;
	if (At<ListFn>(lister)(&spec, &list, false)) {
		for (int i = 0; i < list.count; i++) codes.push_back(Narrow(list.data[i]));
	}
	const FreeFn free = At<FreeFn>(sfv::kGameFree);
	for (int i = 0; i < list.count; i++) {
		if (list.data[i].data) free(const_cast<wchar_t*>(list.data[i].data));
	}
	if (list.data) free(list.data);
	return codes;
}

bool Unplayable(const std::string& stage)
{
	for (const wchar_t* code : sfv::content::kUnplayableStages) {
		std::wstring wide(stage.begin(), stage.end());
		if (_wcsicmp(wide.c_str(), code) == 0) return true;
	}
	return stage.empty();
}

} // namespace

ContentLists ReadContentLists()
{
	ContentLists lists;
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* spec = singleton ? At<SpecDataFn>(sfv::kSpecData)(singleton) : nullptr;
	if (!spec) return lists;
	lists.fighters = ReadList(sfv::kSelectableFighters, spec);
	for (std::string& stage : ReadList(sfv::kSpecStages, spec)) {
		if (!Unplayable(stage)) lists.stages.push_back(std::move(stage));
	}
	return lists;
}

} // namespace sfve::mod
