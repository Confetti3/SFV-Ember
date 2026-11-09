#include "mod/GameContent.hxx"

#include "ember/SfvPickSpace.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <wchar.h>

#include <algorithm>
#include <string_view>
#include <utility>

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
using StageIdsFn = bool (*)(uint8_t** spec, GameArray<int32_t>* out);
using CostumeIdsFn = bool (*)(uint8_t** spec, int32_t uniqueId, GameArray<int32_t>* out);
using ColourIdsFn = bool (*)(uint8_t** spec, int32_t uniqueId, int32_t costume, GameArray<int32_t>* out);
using CostumeNameFn = GameString* (*)(void* unused, GameString* out, const GameString* code, int32_t costume);
using TextToStringFn = const GameString* (*)(const void* text);
using FreeFn = void (*)(void* block);
using StageNameFn = GameString* (*)(uint8_t* stageAsset, GameString* out, const GameString* code);
using ClassFn = uint8_t* (*)(const wchar_t* package);
using LoadObjectFn = uint8_t* (*)(uint8_t* objectClass, uint8_t* outer, const wchar_t* name, const wchar_t* filename,
	uint32_t flags, void* packageMap, bool reconcile);
using OwnedFightersFn = void (*)(uint8_t* setup, GameArray<GameString>* out, bool random, bool eleven);
using OwnedCostumesFn = bool (*)(uint8_t* setup, const GameString* code, GameArray<int32_t>* out);
using OwnedColoursFn = void (*)(uint8_t* setup, const GameString* code, int32_t costume, GameArray<int32_t>* out);
using OwnedStagesFn = void (*)(uint8_t* setup, GameArray<GameString>* out, bool random);
using LoggedInFn = bool (*)();

// The battle setup data asset (sfv::kOwnedFighters and the rest): the one the
// battle jump's check loads (rva 0x551520).
constexpr const wchar_t* kBattleSetupPath =
	L"/Game/GameFlow/CommonSceneStates/BattleSetup/BattleSetupDataAsset.BattleSetupDataAsset";
// The fighters' V-Trigger and V-Skill names (sfv::choices), the assets the
// game's own readers load (rva 0x76AFD0, 0x76AB40).
constexpr const wchar_t* kVTriggerNamesPath = L"/Game/Chara/DA_VTriggerNameAsset.DA_VTriggerNameAsset";
constexpr const wchar_t* kVSkillNamesPath = L"/Game/Chara/DA_VSkillNameAsset.DA_VSkillNameAsset";
// UKWBattleSetupDataAsset's fields (sfv::kBattleSetupClass).
constexpr uint32_t kSetupRestricted = 96;
constexpr uint32_t kSetupRestrictionCount = 128;
constexpr uint32_t kSetupFilter = 280;

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

// A code list the game fills, freed after.
template <typename List>
std::vector<std::string> TakeCodes(List&& list)
{
	GameArray<GameString> codes;
	list(&codes);
	std::vector<std::string> out;
	for (int i = 0; i < codes.count; i++) out.push_back(Narrow(codes.data[i]));
	for (int i = 0; i < codes.count; i++) FreeBlock(codes.data[i].data);
	FreeBlock(codes.data);
	return out;
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

// The characters of one of the game's FStrings, without its terminator.
std::wstring_view TextOf(const GameString& s)
{
	return s.data && s.count > 0 ? std::wstring_view(s.data, wcsnlen(s.data, (size_t)s.count)) : std::wstring_view();
}

// Two codes are the same as the game compares its FStrings: ignoring case.
bool SameCode(std::wstring_view a, std::wstring_view b)
{
	return a.size() == b.size() && _wcsnicmp(a.data(), b.data(), a.size()) == 0;
}

// The code a record starts with (its +0 FString) is `code`.
bool HasCode(uint8_t* record, std::wstring_view code)
{
	return SameCode(TextOf(Field<GameString>(record, 0)), code);
}

// One of the game's arrays of fixed-size records: a TArray's data and count,
// read from the object that holds it, the records `stride` bytes apart.
struct Records {
	uint8_t* data = nullptr;
	int count = 0;
	uint32_t stride = 0;

	Records(uint8_t* object, uint32_t dataOffset, uint32_t countOffset, uint32_t recordStride)
		: data(Field<uint8_t*>(object, dataOffset)), count(Field<int32_t>(object, countOffset)), stride(recordStride)
	{
	}
	// Each record, in order; none for a null array or a count below one.
	template <typename Visit>
	void ForEach(Visit&& visit) const
	{
		for (int i = 0; data && i < count; i++) visit(data + (size_t)stride * (size_t)i);
	}
};

// The first record `matches` accepts; null for none.
template <typename Match>
uint8_t* FindEntry(const Records& records, Match&& matches)
{
	uint8_t* found = nullptr;
	records.ForEach([&](uint8_t* record) {
		if (!found && matches(record)) found = record;
	});
	return found;
}

// The fighter table (sfv::content::kCharaAsset's records).
Records CharaRecords(uint8_t* charaAsset)
{
	return {charaAsset, sfv::content::kCharaEntries, sfv::content::kCharaCount, sfv::content::kCharaStride};
}

// The fighter table's entry with the spec's id for it; null for none.
uint8_t* EntryWithId(uint8_t* charaAsset, int32_t uniqueId)
{
	return FindEntry(CharaRecords(charaAsset),
		[uniqueId](uint8_t* entry) { return Field<int32_t>(entry, sfv::content::kCharaUniqueId) == uniqueId; });
}

// The fighter table's entry with the code; null for none.
uint8_t* EntryWithCode(uint8_t* charaAsset, std::wstring_view code)
{
	return FindEntry(CharaRecords(charaAsset), [code](uint8_t* entry) { return HasCode(entry, code); });
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
// with its costumes and colours: those a battle can load (BattleFighter, the
// rule a launch asks too), so Random and ELEVEN are not among them. The rule
// reads only the exe's own fighter table, so every copy keeps the same
// fighters.
std::vector<FighterContent> ReadFighters(uint8_t* singleton, uint8_t* spec)
{
	uint8_t* charaAsset = Field<uint8_t*>(singleton, sfv::content::kCharaAsset);
	if (!charaAsset) return {};
	const std::vector<std::string> installed = ReadCodes(sfv::kSelectableFighters, spec);
	std::vector<FighterContent> fighters;
	for (int id : ReadNumbers([&](GameArray<int32_t>* out) { return At<FighterIdsFn>(sfv::kSpecFighters)(&spec, out); })) {
		uint8_t* entry = EntryWithId(charaAsset, id);
		if (!entry || !BattleFighter(charaAsset, TextOf(Field<GameString>(entry, 0)))) continue;
		fighters.push_back(ReadFighter(spec, entry, installed));
	}
	return fighters;
}

bool Contains(const std::vector<int>& list, int value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

bool Contains(const std::vector<std::string>& list, const std::string& value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

// The code of the StageAsset table's entry `index` (content::kStageEntries);
// empty for an index outside the table.
std::string StageCodeAt(uint8_t* stageAsset, int32_t index)
{
	uint8_t* entries = Field<uint8_t*>(stageAsset, sfv::content::kStageEntries);
	const int count = Field<int32_t>(stageAsset, sfv::content::kStageCount);
	if (!entries || index < 0 || index >= count) return {};
	return Narrow(Field<GameString>(entries + (size_t)sfv::content::kStageStride * index, 0));
}

// The spec's stage universe (sfv::kSpecStageIds) in its order, less Random
// and the stages without stage data, each marked installed when this copy's
// kSpecStages lists it. A code kSpecStages gives outside the universe cannot
// come from the game's code (it filters the same map); it is logged once and
// left out.
std::vector<StageContent> ReadStages(uint8_t* singleton, uint8_t* spec)
{
	uint8_t* stageAsset = Field<uint8_t*>(singleton, sfv::content::kStageAsset);
	if (!stageAsset) return {};
	std::vector<StageContent> stages;
	for (int index : ReadNumbers([&](GameArray<int32_t>* out) { return At<StageIdsFn>(sfv::kSpecStageIds)(&spec, out); })) {
		std::string code = StageCodeAt(stageAsset, index);
		if (Unplayable(code)) continue;
		stages.push_back({std::move(code)});
	}
	static bool loggedStray = false;
	for (const std::string& installed : ReadCodes(sfv::kSpecStages, spec)) {
		const auto stage = std::find_if(stages.begin(), stages.end(), [&](const StageContent& s) { return s.code == installed; });
		if (stage != stages.end()) stage->installed = true;
		else if (!Unplayable(installed) && !loggedStray) {
			Log("content: installed stage %s is not in the spec's stage list; left out", installed.c_str());
			loggedStray = true;
		}
	}
	return stages;
}

// A data asset of the class `getter` returns, found or loaded; null when the
// game has no such object (the class or the asset is missing).
uint8_t* LoadAsset(const CodeSite& getter, const wchar_t* path)
{
	uint8_t* assetClass = At<ClassFn>(getter)(L"/Script/KiwiGame");
	if (!assetClass) return nullptr;
	return At<LoadObjectFn>(sfv::kStaticLoadObject)(assetClass, nullptr, path, nullptr, 0, nullptr, true);
}

uint8_t* BattleSetup()
{
	return LoadAsset(sfv::kBattleSetupClass, kBattleSetupPath);
}

// The names a choice name asset (`part`, "V-Skill", for the log) gives the
// fighter `code`, in order: those of every entry with that code (sfv::choices
// says how much of the game's lookup is known). Read in place: the game's
// lookup would copy the texts and free the code it is given. The layout is
// known only from reading the exe, so an asset with more entries than
// sfv::choices::kMaxEntries, or an entry with more names than kMaxNames,
// reads as not there (none), logged once, rather than being walked.
std::optional<std::vector<std::string>> ChoiceNames(uint8_t* asset, const char* part, std::wstring_view code)
{
	static bool logged = false;
	const auto refuse = [part](const char* what, int count) -> std::optional<std::vector<std::string>> {
		if (!logged) Log("content: the %s name asset has %d %s, past what Ember reads; read as not there", part, count, what);
		logged = true;
		return std::nullopt;
	};
	const Records entries(asset, sfv::choices::kEntries, sfv::choices::kCount, sfv::choices::kEntryStride);
	if (entries.count > sfv::choices::kMaxEntries) return refuse("entries", entries.count);
	std::vector<std::string> names;
	int pastLimit = 0; // an entry's name count past kMaxNames, once one is found
	entries.ForEach([&](uint8_t* entry) {
		if (pastLimit || !HasCode(entry, code)) return;
		const Records texts(entry, sfv::choices::kNames, sfv::choices::kNameCount, sfv::choices::kNameStride);
		if (texts.count > sfv::choices::kMaxNames) {
			pastLimit = texts.count;
			return;
		}
		texts.ForEach([&](uint8_t* text) {
			const GameString* name = At<TextToStringFn>(sfv::kTextToString)(text);
			names.push_back(name ? Utf8(*name) : std::string());
		});
	});
	if (pastLimit) return refuse("names for one fighter", pastLimit);
	return names;
}

// The two name assets, V-Skills first; either null while it is not there.
std::pair<uint8_t*, uint8_t*> ChoiceAssets()
{
	return {LoadAsset(sfv::kVersionDataClass, kVSkillNamesPath), LoadAsset(sfv::kVersionDataClass, kVTriggerNamesPath)};
}

// How many choices Ember offers for a part the asset names `given` of: one
// to picks::kPartChoices (FitChoices).
size_t FittedCount(size_t given)
{
	return std::clamp<size_t>(given, 1, (size_t)picks::kPartChoices);
}

// Each fighter's V-Skills and V-Triggers from the game's name assets
// (FitChoices). False, with nothing changed, when either asset is not there:
// the lists wait for it rather than give every fighter one of each.
bool ReadChoices(std::vector<FighterContent>& fighters)
{
	const auto [vSkillAsset, vTriggerAsset] = ChoiceAssets();
	if (!vSkillAsset || !vTriggerAsset) return false;
	std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> read;
	for (const FighterContent& fighter : fighters) {
		const CodeView code(fighter.code);
		std::optional<std::vector<std::string>> vSkills = ChoiceNames(vSkillAsset, "V-Skill", code.wide);
		std::optional<std::vector<std::string>> vTriggers = ChoiceNames(vTriggerAsset, "V-Trigger", code.wide);
		if (!vSkills || !vTriggers) return false;
		read.emplace_back(FitChoices(std::move(*vSkills), "V-Skill", fighter.code),
			FitChoices(std::move(*vTriggers), "V-Trigger", fighter.code));
	}
	for (size_t i = 0; i < fighters.size(); i++) {
		fighters[i].vSkills = std::move(read[i].first);
		fighters[i].vTriggers = std::move(read[i].second);
	}
	return true;
}

ChoiceCounts CountsOf(const FighterContent& fighter)
{
	return {(int)fighter.vSkills.size(), (int)fighter.vTriggers.size()};
}

// Game thread: each fighter's counts by its code, as ReadContentLists last
// read them (FighterChoiceCounts); empty before any read.
std::vector<std::pair<std::wstring, ChoiceCounts>> s_choiceCounts;

// What the game lets this copy's player pick, as its menus and the battle
// jump's check ask it (sfv::kOwnedFighters and the rest), into `lists`.
// False, with nothing changed, when the battle setup asset is not there.
bool ReadOwnership(ContentLists& lists)
{
	uint8_t* setup = BattleSetup();
	if (!setup) return false;
	OwnershipSource& source = lists.ownership;
	source.read = true;
	source.restricted = Field<uint8_t>(setup, kSetupRestricted) != 0;
	source.restrictionEntries = Field<int32_t>(setup, kSetupRestrictionCount);
	source.filter = Field<void*>(setup, kSetupFilter) != nullptr;
	source.loggedIn = At<LoggedInFn>(sfv::kLoggedIn)();
	const std::vector<std::string> fighters =
		TakeCodes([&](GameArray<GameString>* out) { At<OwnedFightersFn>(sfv::kOwnedFighters)(setup, out, false, false); });
	for (FighterContent& fighter : lists.fighters) {
		fighter.owned = Contains(fighters, fighter.code);
		const CodeView view(fighter.code);
		const GameString code = view.Get();
		const std::vector<int> costumes = ReadNumbers(
			[&](GameArray<int32_t>* out) { return At<OwnedCostumesFn>(sfv::kOwnedCostumes)(setup, &code, out); });
		for (CostumeContent& costume : fighter.costumes) {
			costume.owned = Contains(costumes, costume.id);
			const std::vector<int> colours = ReadNumbers([&](GameArray<int32_t>* out) {
				At<OwnedColoursFn>(sfv::kOwnedColours)(setup, &code, costume.id, out);
				return true;
			});
			costume.ownedColours.clear();
			for (int colour : costume.colours) {
				if (Contains(colours, colour)) costume.ownedColours.push_back(colour);
			}
		}
	}
	const std::vector<std::string> stages =
		TakeCodes([&](GameArray<GameString>* out) { At<OwnedStagesFn>(sfv::kOwnedStages)(setup, out, false); });
	for (StageContent& stage : lists.stages) stage.owned = Contains(stages, stage.code);
	return true;
}

} // namespace

bool BattleFighter(uint8_t* charaAsset, std::wstring_view code)
{
	if (!charaAsset || code.empty()) return false;
	uint8_t* entry = EntryWithCode(charaAsset, code);
	// A setup named, not just the empty FString's terminator.
	return entry && Field<GameString>(entry, sfv::content::kCharaSetupData).count > 1;
}

ContentLists ReadContentLists()
{
	ContentLists lists;
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	uint8_t* spec = singleton ? At<SpecDataFn>(sfv::kSpecData)(singleton) : nullptr;
	if (!spec) return lists;
	lists.fighters = ReadFighters(singleton, spec);
	lists.stages = ReadStages(singleton, spec);
	const bool anyInstalled =
		std::any_of(lists.stages.begin(), lists.stages.end(), [](const StageContent& stage) { return stage.installed; });
	if (lists.fighters.empty() || !anyInstalled) return {};
	// Without the battle setup asset there is no answer to what this copy's
	// player may pick: no lists at all, rather than lists that say nothing is
	// owned or everything is.
	if (!ReadOwnership(lists)) {
		static bool logged = false;
		if (!logged) Log("content: the battle setup asset is not there yet; the lists wait for it");
		logged = true;
		return {};
	}
	if (!ReadChoices(lists.fighters)) {
		static bool logged = false;
		if (!logged) Log("content: the V-Skill or V-Trigger name asset is not there yet; the lists wait for it");
		logged = true;
		return {};
	}
	s_choiceCounts.clear();
	for (const FighterContent& fighter : lists.fighters) s_choiceCounts.emplace_back(CodeView(fighter.code).wide, CountsOf(fighter));

	// The names only once both lists are there: each may load a stage's data,
	// so only an installed stage's is asked for (one this copy lacks is never
	// shown, and its data asset may be in the package it lacks).
	uint8_t* stageAsset = Field<uint8_t*>(singleton, sfv::content::kStageAsset);
	if (!stageAsset) return lists;
	for (StageContent& stage : lists.stages) {
		if (stage.installed) stage.name = StageName(stageAsset, stage.code);
	}
	return lists;
}

std::vector<std::string> FitChoices(std::vector<std::string> names, const char* part, const std::string& fighter)
{
	const size_t given = names.size();
	names.resize(FittedCount(given));
	if (names.size() != given) {
		Log("content: %s has %zu %s names in the game's asset; Ember offers %zu", fighter.c_str(), given, part, names.size());
	}
	return names;
}

std::string ChoicesText(const FighterContent& fighter)
{
	const auto list = [](const char* part, const std::vector<std::string>& names) {
		std::string text = std::string(part) + " " + std::to_string(names.size()) + " (";
		for (size_t i = 0; i < names.size(); i++) text += (i ? ", \"" : "\"") + names[i] + "\"";
		return text + ")";
	};
	return list("V-Skills", fighter.vSkills) + ", " + list("V-Triggers", fighter.vTriggers);
}

std::optional<ChoiceCounts> FighterChoiceCounts(std::wstring_view code)
{
	if (!s_choiceCounts.empty()) {
		for (const auto& [own, counts] : s_choiceCounts) {
			if (SameCode(own, code)) return counts;
		}
		return std::nullopt;
	}
	std::vector<FighterContent> fighter(1);
	for (wchar_t c : code) fighter[0].code += (char)c;
	if (!ReadChoices(fighter)) return std::nullopt;
	return CountsOf(fighter[0]);
}

int MarkUninstalled(ContentLists& lists, const std::vector<std::string>& codes)
{
	int cleared = 0;
	for (const std::string& code : codes) {
		const auto installed = [](const StageContent& stage) { return stage.installed; };
		const auto stage = std::find_if(lists.stages.begin(), lists.stages.end(),
			[&](const StageContent& s) { return s.code == code && s.installed; });
		if (stage == lists.stages.end() || std::count_if(lists.stages.begin(), lists.stages.end(), installed) < 2) continue;
		stage->installed = false;
		cleared++;
	}
	return cleared;
}

void MarkUnowned(ContentLists& lists, const std::vector<UnownedContent>& marks)
{
	for (const UnownedContent& mark : marks) {
		for (FighterContent& fighter : lists.fighters) {
			if (fighter.code != mark.fighter) continue;
			lists.ownership.markedUnowned++;
			if (mark.costume < 0) {
				fighter.owned = false;
				continue;
			}
			for (CostumeContent& costume : fighter.costumes) {
				if (costume.id != mark.costume) continue;
				if (mark.colour < 0) costume.owned = false;
				else costume.ownedColours.erase(std::remove(costume.ownedColours.begin(), costume.ownedColours.end(), mark.colour),
					costume.ownedColours.end());
			}
		}
	}
}

std::string LocalizedText(const wchar_t* textNamespace, const wchar_t* key)
{
	if (!textNamespace || !key || !*key) return {};
	// An FText is 24 bytes (its shared data's reference and flags); the
	// buffer is zeroed and larger. It is never released: the game keeps the
	// texts it builds this way for good (rva 0x6BDE20), and the few Ember
	// asks for are asked once.
	auto* text = new uint8_t[64]{};
	using CreateTextFn = void* (*)(void* out, const wchar_t* source, const wchar_t* textNamespace, const wchar_t* key);
	At<CreateTextFn>(sfv::kCreateText)(text, key, textNamespace, key);
	const GameString* shown = At<TextToStringFn>(sfv::kTextToString)(text);
	const std::string out = shown ? Utf8(*shown) : std::string();
	// Without the key in the data the text is its source, the key itself.
	std::string asKey;
	for (const wchar_t* c = key; *c; c++) asKey += static_cast<char>(*c & 0x7F);
	return out == asKey ? std::string() : out;
}

} // namespace sfve::mod
