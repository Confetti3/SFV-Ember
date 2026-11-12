#include "mod/GameContent.hxx"

#include "mod/EntitlementGrant.hxx"
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
using SpecReadyFn = bool (*)(uint8_t* spec);
using FindFn = int32_t* (*)(uint8_t* set, int32_t* outIndex, int32_t key);
using FighterInstalledFn = bool (*)(uint8_t* charaAsset, const GameString* code);
using CostumePackageFn = bool (*)(uint8_t* charaAsset, const GameString* code, int32_t costume);
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
	explicit CodeView(std::wstring_view code) : wide(code) {}
	GameString Get() const { return {wide.c_str(), (int32_t)wide.size() + 1, (int32_t)wide.size() + 1}; }
	std::wstring wide;
};

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

// One of the game's number lists, in its order; the array is freed after.
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
	// Record `i`, which ForEach would visit; null for any other.
	uint8_t* Nth(int i) const { return data && i >= 0 && i < count ? data + (size_t)stride * (size_t)i : nullptr; }
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

// The fighter table's entry with the code; null for none.
uint8_t* EntryWithCode(uint8_t* charaAsset, std::wstring_view code)
{
	return FindEntry(CharaRecords(charaAsset), [code](uint8_t* entry) { return HasCode(entry, code); });
}

// The entry names a battle setup of its own, not just the empty FString's
// terminator (BattleFighter's rule).
bool NamesSetup(uint8_t* entry)
{
	return Field<GameString>(entry, sfv::content::kCharaSetupData).count > 1;
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

bool Contains(const std::vector<int>& list, int value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

bool Contains(const std::vector<std::string>& list, const std::string& value)
{
	return std::find(list.begin(), list.end(), value) != list.end();
}

// The game's tables the lists are read from: its singleton, the fighter
// table (content::kCharaAsset), the stage table (content::kStageAsset) and
// the spec data (kSpecData); the others null while the singleton is not
// there, each null while it is not loaded.
struct SpecTables {
	uint8_t* singleton = nullptr;
	uint8_t* charaAsset = nullptr;
	uint8_t* stageAsset = nullptr;
	uint8_t* spec = nullptr;

	bool Loaded() const { return charaAsset && stageAsset && spec; }
};

SpecTables Tables()
{
	uint8_t* singleton = At<GameDataFn>(sfv::kGameData)();
	if (!singleton) return {};
	return {singleton, Field<uint8_t*>(singleton, sfv::content::kCharaAsset),
		Field<uint8_t*>(singleton, sfv::content::kStageAsset), At<SpecDataFn>(sfv::kSpecData)(singleton)};
}

// The value of the element keyed `key` in one of the spec's sets of the pick
// space (`kind`, whose set is at its offset in `parent`), found with the
// game's own lookup for that set, when the builder linked a CharaSpec row to
// it; null otherwise. The one way Ember finds an element of the pick space:
// the reader and the launch's check both ask it.
uint8_t* LinkedValue(const sfv::SpecSet& kind, uint8_t* parent, int32_t key)
{
	uint8_t* set = parent + kind.set;
	int32_t index = -1;
	At<FindFn>(kind.find)(set, &index, key);
	uint8_t* elements = Field<uint8_t*>(set, sfv::spec::kSetElements);
	if (index < 0 || !elements) return nullptr;
	uint8_t* value = elements + (size_t)kind.stride * (size_t)index + sfv::spec::kValue;
	return Field<void*>(value, sfv::spec::kRow) ? value : nullptr;
}

// The elements of one of the spec's sets in their array's order, the order
// the builder added them in and the game's listers walk (sfv::spec). A set
// with free slots is not laid out as Ember knows (the builder empties a set
// and fills it, and nothing removes from one): none, with `error` said.
std::optional<Records> SetElements(uint8_t* set, uint32_t stride, const char* what, std::string& error)
{
	const int32_t freeSlots = Field<int32_t>(set, sfv::spec::kSetFreeSlots);
	if (freeSlots != 0) {
		error = "the spec's " + std::string(what) + " set has " + std::to_string(freeSlots) +
			" free slots, which its builder never leaves; Ember does not read it";
		return std::nullopt;
	}
	return Records(set, sfv::spec::kSetElements, sfv::spec::kSetCount, stride);
}

// The game grants the value's code: its flag at `flag` (sfv::spec), set by
// the spec's refresh from the code cache, which SFV's grant marks granted
// throughout (EntitlementGrant).
bool GameGrants(uint8_t* value, uint32_t flag = sfv::spec::kEntitled)
{
	return Field<uint8_t>(value, flag) != 0;
}

// The account's own cache grants the value's code whose key is at `key`
// (sfv::spec::kCodeKey, kSecondCodeKey), of the code cache's kind `kind`:
// the answer EntitlementGrant read at the spec's last refresh, before it
// granted everything. Without that answer nothing was granted (the grant
// writes only what it read first), so the game's flag at `flag` is still the
// account's.
bool AccountGrantsCode(const AccountGrants& account, uint8_t* value, int32_t kind, uint32_t key, uint32_t flag)
{
	if (!account.captured) return GameGrants(value, flag);
	return account.Grants(kind, Field<uint64_t>(value, key));
}

// The fighter's costumes and colours: every costume number of its
// CostumeDatas that names a package and has a row, in number order, and
// under each every colour 0-127 with a row, in number order; with what the
// account's entitlement cache grants of each and what the game grants. This
// copy's installs and the names are ReadInstallsAndNames'.
FighterContent ReadFighter(uint8_t* entry, uint8_t* value, const AccountGrants& account)
{
	namespace cc = sfv::codecache;
	FighterContent fighter;
	fighter.code = Narrow(Field<GameString>(entry, 0));
	fighter.name = FighterName(entry);
	fighter.entitled = AccountGrantsCode(account, value, cc::kFighterCodes, sfv::spec::kCodeKey, sfv::spec::kEntitled);
	fighter.visible =
		AccountGrantsCode(account, value, cc::kFighterCodes, sfv::spec::kSecondCodeKey, sfv::spec::kFighterVisible);
	fighter.granted = GameGrants(value);
	const Records costumeDatas(entry, sfv::content::kCharaCostumeDatas, sfv::content::kCharaCostumeCount,
		sfv::content::kGameStringStride);
	for (int number = 0; number < costumeDatas.count; number++) {
		uint8_t* path = costumeDatas.Nth(number);
		uint8_t* costumeValue = LinkedValue(sfv::kSpecCostumeSet, value, number);
		// A package named, not just the empty FString's terminator.
		if (!path || !costumeValue || Field<GameString>(path, 0).count <= 1) continue;
		CostumeContent costume;
		costume.id = number;
		costume.entitled =
			AccountGrantsCode(account, costumeValue, cc::kCostumeCodes, sfv::spec::kCodeKey, sfv::spec::kEntitled);
		costume.granted = GameGrants(costumeValue);
		for (int colour = 0; colour < sfv::spec::kColourKeys; colour++) {
			if (uint8_t* colourValue = LinkedValue(sfv::kSpecColourSet, costumeValue, colour)) {
				costume.colours.push_back({colour, false,
					AccountGrantsCode(account, colourValue, cc::kColourCodes, sfv::spec::kCodeKey, sfv::spec::kEntitled),
					GameGrants(colourValue)});
			}
		}
		fighter.costumes.push_back(std::move(costume));
	}
	return fighter;
}

// The universe's fighters, and each one's CharaUniqueID (the spec's key) in
// the same order; or why the table can never give them.
struct FighterUniverse {
	std::vector<FighterContent> fighters;
	std::vector<int> keys;
	std::string error;
	// ELEVEN's spec row, read as a fighter's is, though a battle cannot load
	// it (BattleFighter): Ember asks it for the look the game's Eleven wears.
	std::optional<FighterContent> eleven;
};

// The fighters of the universe, in the fighter table's order (the order the
// spec's builder adds them in, and so the game's own listers' order), each
// with its costumes and colours: every entry with a CharaUniqueID of 0 or
// more, a row, and a battle setup of its own (BattleFighter, the rule a
// launch asks too), so Random and ELEVEN are not among them. Nothing here
// depends on this copy's install or account, so every copy of the build
// keeps the same fighters in the same order. Two entries the builder adds
// (rva 0x56C2B0: an id of 0 or more and a code) with the same id are an
// error: the builder keeps one element for both and adds both entries'
// costume numbers under it, so which entry's CostumeDatas the rows describe
// is not known.
FighterUniverse ReadFighters(const SpecTables& tables, const AccountGrants& account)
{
	FighterUniverse universe;
	std::vector<std::pair<int, std::string>> added; // the builder's entries: id, code
	CharaRecords(tables.charaAsset).ForEach([&](uint8_t* entry) {
		if (!universe.error.empty()) return;
		const int32_t id = Field<int32_t>(entry, sfv::content::kCharaUniqueId);
		// A code, not just the empty FString's terminator.
		if (id < 0 || Field<GameString>(entry, 0).count <= 1) return;
		const std::string code = Narrow(Field<GameString>(entry, 0));
		const auto twin = std::find_if(added.begin(), added.end(), [id](const auto& seen) { return seen.first == id; });
		if (twin != added.end()) {
			universe.error = "the fighter table has CharaUniqueID " + std::to_string(id) + " twice (" + twin->second + " and " +
				code + "), so the spec's costumes for it are not known";
			return;
		}
		added.emplace_back(id, code);
		uint8_t* value = LinkedValue(sfv::kSpecFighterSet, tables.spec, id);
		if (value && !NamesSetup(entry) && SameCode(code, "ELV")) universe.eleven = ReadFighter(entry, value, account);
		if (!value || !NamesSetup(entry)) return;
		universe.keys.push_back(id);
		universe.fighters.push_back(ReadFighter(entry, value, account));
	});
	if (!universe.error.empty()) return {{}, {}, std::move(universe.error), std::nullopt};
	return universe;
}

// That the universe's fighter order is the game's listers' is read from the
// builder, not seen: the spec's fighter set holds its elements in the order
// the builder added them, which is the order the listers walk. A universe
// whose keys come in another order is logged once. It asks neither the
// install nor the account, and it changes no id: the universe keeps the
// table's order on every copy.
void LogOrder(uint8_t* spec, const std::vector<int>& keys)
{
	static bool logged = false;
	if (logged) return;
	std::string error; // a set laid out otherwise: nothing to compare with
	const std::optional<Records> elements = SetElements(spec + sfv::spec::kFighters, sfv::spec::kFighterStride, "fighters", error);
	if (!elements) return;
	std::vector<int> theirs;
	elements->ForEach([&](uint8_t* element) {
		const int32_t key = Field<int32_t>(element, 0);
		if (Contains(keys, key)) theirs.push_back(key);
	});
	if (theirs == keys) return;
	Log("content: the fighter table's order differs from the spec's fighter set's; the universe keeps the table's");
	logged = true;
}

// This copy's own facts about the fighters, and the costumes' names: each
// asks the game's package files (the fighter's package, kFighterInstalled;
// each costume's, kCostumePackage) or its text, so they are read once the
// rest of the lists are there.
void ReadInstallsAndNames(uint8_t* charaAsset, std::vector<FighterContent>& fighters)
{
	for (FighterContent& fighter : fighters) {
		const CodeView view(fighter.code);
		const GameString code = view.Get();
		fighter.installed = At<FighterInstalledFn>(sfv::kFighterInstalled)(charaAsset, &code);
		for (CostumeContent& costume : fighter.costumes) {
			costume.installed = At<CostumePackageFn>(sfv::kCostumePackage)(charaAsset, &code, costume.id);
			GameString name{};
			At<CostumeNameFn>(sfv::kCostumeName)(nullptr, &name, &code, costume.id);
			costume.name = TakeUtf8(name);
		}
	}
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

// The stage universe: every element of the spec's stage set (one per
// StageSpec row), in the set's order, less Random and the stages without
// stage data, each with whether this copy has its package (the refresh's
// own test, sfv::spec::kStageInstalled), whether that package is here and
// the account's own cache grants both of the row's codes (`entitled`, the
// selectable flag as the account's answer sets it) and the game's selectable
// flag (sfv::spec::kStageSelectable, `granted`: every installed stage while
// SFV's grant is on). The game's stage listers are not asked: both keep only
// the selectable stages (sfv::kSpecData's comment), and the universe must not
// depend on the account.
std::vector<StageContent> ReadStages(const SpecTables& tables, const AccountGrants& account, std::string& error)
{
	namespace cc = sfv::codecache;
	const std::optional<Records> elements = SetElements(tables.spec + sfv::spec::kStages, sfv::spec::kStageStride, "stages", error);
	if (!elements) return {};
	std::vector<StageContent> stages;
	elements->ForEach([&](uint8_t* element) {
		std::string code = StageCodeAt(tables.stageAsset, Field<int32_t>(element, 0));
		if (Unplayable(code)) return;
		uint8_t* value = element + sfv::spec::kValue;
		StageContent stage;
		stage.code = std::move(code);
		stage.installed = Field<uint8_t>(value, sfv::spec::kStageInstalled) != 0;
		stage.granted = GameGrants(value, sfv::spec::kStageSelectable);
		stage.entitled = account.captured
			? stage.installed && account.Grants(cc::kStageCodes, Field<uint64_t>(value, sfv::spec::kCodeKey)) &&
				account.Grants(cc::kStageCodes, Field<uint64_t>(value, sfv::spec::kSecondCodeKey))
			: stage.granted;
		stages.push_back(std::move(stage));
	});
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

// Each fighter's V-Skills and V-Triggers from the game's name assets
// (FitChoices). False, with nothing changed, when either asset is not there
// or is past what Ember reads (ChoiceNames).
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
// jump's check ask the battle setup asset `setup` (sfv::kOwnedFighters and
// the rest), into `lists`; the looks' answer is then kContentOwnership's
// (ApplyContentOwnership). A costume or colour of the universe the game's
// lists leave out (one the entitlement cache does not grant) reads as not
// owned. Stages are not asked (the game's OwnedStages): their `owned` is
// kContentOwnership's alone, from `installed` and `entitled`.
void ReadOwnership(uint8_t* setup, ContentLists& lists)
{
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
			for (ColourContent& colour : costume.colours) colour.owned = Contains(colours, colour.id);
		}
	}
}

// No lists while `what` is not there; each wait is logged once.
ContentLists Waiting(const char* what)
{
	static std::vector<std::string> said;
	if (!Contains(said, what)) {
		Log("content: %s; the lists wait for it", what);
		said.emplace_back(what);
	}
	return {};
}

// No lists, ever, for this exe: why (ContentLists::error).
ContentLists Never(std::string why)
{
	ContentLists lists;
	lists.error = std::move(why);
	return lists;
}

} // namespace

bool BattleFighter(uint8_t* charaAsset, std::wstring_view code)
{
	if (!charaAsset || code.empty()) return false;
	uint8_t* entry = EntryWithCode(charaAsset, code);
	return entry && NamesSetup(entry);
}

LookLoad LookLoadState(std::wstring_view code, int costume, int colour)
{
	const SpecTables tables = Tables();
	if (!tables.charaAsset || !tables.spec) return LookLoad::NoTables;
	uint8_t* entry = code.empty() ? nullptr : EntryWithCode(tables.charaAsset, code);
	if (!entry || !NamesSetup(entry)) return LookLoad::NotBattleFighter;
	const CodeView view(code);
	const GameString game = view.Get();
	if (!At<FighterInstalledFn>(sfv::kFighterInstalled)(tables.charaAsset, &game)) return LookLoad::FighterNotInstalled;
	uint8_t* fighter = LinkedValue(sfv::kSpecFighterSet, tables.spec, Field<int32_t>(entry, sfv::content::kCharaUniqueId));
	uint8_t* costumeValue = fighter ? LinkedValue(sfv::kSpecCostumeSet, fighter, costume < 0 ? 0 : costume) : nullptr;
	if (costume >= 0) {
		if (!costumeValue) return LookLoad::CostumeNotInSpec;
		if (!At<CostumePackageFn>(sfv::kCostumePackage)(tables.charaAsset, &game, costume)) return LookLoad::CostumeNotInstalled;
	}
	if (colour >= 0 && !(costumeValue && LinkedValue(sfv::kSpecColourSet, costumeValue, colour))) return LookLoad::ColourNotInSpec;
	return LookLoad::Loadable;
}

SelectGridList ReadSelectGridList()
{
	// UKWBattleSetupDataAsset +48: TArray<FString> {data, int32 count, int32
	// capacity} (rva 0x55A9B0 hands it to the array copy rva 0x5156A0, whose
	// element copy rva 0x4E1F30 walks 16-byte FStrings). A count past
	// kMaxEntries, or a code longer than kMaxCode, reads as not this list.
	constexpr uint32_t kSelectGridList = 48;
	constexpr int kMaxEntries = 512;
	constexpr int kMaxCode = 16;
	SelectGridList list;
	uint8_t* setup = BattleSetup();
	if (!setup) return list.error = "the battle setup asset is not there", list;
	GameArray<GameString> codes{};
	if (!TryRead(setup + kSelectGridList, &codes)) return list.error = "the asset's +48 cannot be read", list;
	if (codes.count < 0 || codes.count > kMaxEntries || (codes.count > 0 && !codes.data)) {
		return list.error = "the asset's +48 holds " + std::to_string(codes.count) + " entries, not a list Ember reads", list;
	}
	for (int i = 0; i < codes.count; i++) {
		GameString code{};
		if (!TryRead(codes.data + i, &code) || code.count < 0 || code.count > kMaxCode) {
			list.codes.clear();
			return list.error = "entry " + std::to_string(i) + " of the asset's +48 is not a fighter code", list;
		}
		list.codes.push_back(Narrow(code));
	}
	return list;
}

ContentLists ReadContentLists()
{
	// The waits first, cheapest first: until all of them are over nothing
	// is read, so a frame of waiting costs these checks alone.
	const SpecTables tables = Tables();
	if (!tables.Loaded()) return Waiting("the game's content tables are not loaded yet");
	if (!At<SpecReadyFn>(sfv::kSpecReady)(tables.spec)) return Waiting("the spec's map is not built and refreshed yet");
	// Without the battle setup asset there is no answer to what this copy's
	// player may pick: no lists at all, rather than lists that say nothing is
	// owned or everything is.
	uint8_t* setup = BattleSetup();
	if (!setup) return Waiting("the battle setup asset is not there yet");
	if (const auto [vSkills, vTriggers] = ChoiceAssets(); !vSkills || !vTriggers) {
		return Waiting("the V-Skill or V-Trigger name asset is not there yet");
	}

	// Everything is there: what is read from here on is final for this exe,
	// so a list it cannot make is an error, never a wait.
	ContentLists lists;
	const AccountGrants account = AccountGrantsNow();
	FighterUniverse universe = ReadFighters(tables, account);
	if (!universe.error.empty()) return Never(std::move(universe.error));
	if (universe.fighters.empty()) return Never("the spec has no fighter a battle can load");
	lists.fighters = std::move(universe.fighters);
	lists.eleven = std::move(universe.eleven);
	lists.stages = ReadStages(tables, account, lists.error);
	if (!lists.error.empty()) return Never(std::move(lists.error));
	if (std::none_of(lists.stages.begin(), lists.stages.end(), [](const StageContent& stage) { return stage.installed; })) {
		return Never("this copy has none of the spec's " + std::to_string(lists.stages.size()) + " stages installed");
	}
	LogOrder(tables.spec, universe.keys);
	ReadOwnership(setup, lists);
	if (!ReadChoices(lists.fighters)) return Never("the V-Skill or V-Trigger name asset is past what Ember reads");
	s_choiceCounts.clear();
	for (const FighterContent& fighter : lists.fighters) s_choiceCounts.emplace_back(CodeView(fighter.code).wide, CountsOf(fighter));
	ReadInstallsAndNames(tables.charaAsset, lists.fighters);
	LimitToAccount(lists);
	ApplyContentOwnership(lists, kContentOwnership);

	// The stages' names last: each may load a stage's data, so only an
	// installed stage's is asked for (one this copy lacks is never shown, and
	// its data asset may be in the package it lacks).
	for (StageContent& stage : lists.stages) {
		if (stage.installed) stage.name = StageName(tables.stageAsset, stage.code);
	}
	return lists;
}

std::optional<GrantCounts> CountGrants()
{
	const SpecTables tables = Tables();
	if (!tables.Loaded() || !At<SpecReadyFn>(sfv::kSpecReady)(tables.spec)) return std::nullopt;
	const AccountGrants account = AccountGrantsNow();
	GrantCounts counts;
	FighterUniverse universe = ReadFighters(tables, account);
	if (!universe.error.empty()) {
		counts.error = std::move(universe.error);
		return counts;
	}
	const std::vector<StageContent> stages = ReadStages(tables, account, counts.error);
	if (!counts.error.empty()) return counts;
	for (const FighterContent& fighter : universe.fighters) {
		counts.fighters++;
		counts.fightersGranted += fighter.granted;
		counts.fightersEntitled += fighter.entitled;
		for (const CostumeContent& costume : fighter.costumes) {
			counts.costumes++;
			counts.costumesGranted += costume.granted;
			counts.costumesEntitled += costume.entitled;
			for (const ColourContent& colour : costume.colours) {
				counts.colours++;
				counts.coloursGranted += colour.granted;
				counts.coloursEntitled += colour.entitled;
			}
		}
	}
	for (const StageContent& stage : stages) {
		if (!stage.installed) continue;
		counts.stagesInstalled++;
		counts.stagesGranted += stage.granted;
		counts.stagesEntitled += stage.entitled;
	}
	return counts;
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

std::optional<int> CharaVoiceLanguage(std::wstring_view code)
{
	using SystemSaveFn = uint8_t* (*)(uint8_t* gameInstance);
	using CharaVoiceFn = uint8_t (*)(uint8_t* options, const GameString* code);
	uint8_t* instance = At<GameDataFn>(sfv::kGameData)();
	uint8_t* save = instance ? At<SystemSaveFn>(sfv::kSystemSave)(instance) : nullptr;
	uint8_t* options = save ? Field<uint8_t*>(save, sfv::saves::kOptions) : nullptr;
	if (!options || code.empty()) return std::nullopt;
	const CodeView view(code);
	const GameString game = view.Get();
	const int voice = At<CharaVoiceFn>(sfv::kCharaVoiceLanguage)(options, &game);
	// The battle takes only these two (sfv::jump::kCharVoice); another
	// number is the lookup's own default, English.
	return voice == sfv::jump::kVoiceJapanese ? voice : sfv::jump::kVoiceEnglish;
}

} // namespace sfve::mod
