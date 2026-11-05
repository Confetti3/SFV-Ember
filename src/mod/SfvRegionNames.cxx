#include "mod/SfvRegionNames.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <stdio.h>

#include <algorithm>
#include <iterator>

namespace sfve::mod {

// A registered run: object-relative start and length (map section 2; the
// registration functions receive object+8, already added here).
struct RunSpec {
	uint32_t offset;
	uint32_t size;
};

// A field holding a pointer to a separate object whose fields are
// registered too (map section 1, step 5).
struct SubObjectSpec {
	uint32_t pointerAt;
	RunSpec run;
};

struct OwnerSpec {
	const char* name;
	uint32_t typeRva; // the type object; 0 for the root
	std::vector<RunSpec> runs;
	std::vector<SubObjectSpec> subObjects;
};

namespace {

// Every unit and part registers through its own function (map section 2).
// ActionUnit's runs (+248/40, +376/68) are inherited by CharaUnit and
// ProjectileUnit; ActionPart's (+216/15808, +16048/4) by both action parts.
const OwnerSpec kUnits[] = {
	{"CharaUnit", sfv::unit::kCharaUnitType,
		{{248, 40}, {376, 68}, {1892, 300}, {2196, 208}, {2416, 28}, {2448, 28}, {2480, 652}, {3136, 24}, {3488, 48},
			{3548, 24}, {3584, 12}, {3604, 16}},
		{{0xC58, {16, 32}}}},
	{"ProjectileUnit", 0x3A64C20, {{248, 40}, {376, 68}, {464, 208}, {688, 104}}, {{0x2A0, {16, 20}}}},
	{"ProjectileGroupUnit", 0x3A65758, {{640, 600}, {1280, 1408}}, {}},
	{"CharaManagerUnit", 0x3A647F8, {{440, 4}, {448, 112}, {600, 8}}, {}},
	{"CameraUnit", 0x3A648A0, {{256, 528}, {792, 20}, {832, 76}, {928, 36}, {976, 120}, {1104, 36}, {1152, 128}, {1360, 60}},
		{}},
	{"InputUnit", 0x3A64C90, {{376, 8192}}, {}},
	{"VersusRuleUnit", sfv::unit::kVersusRuleUnitType, {{252, 64}, {69828, 120}}, {}},
	{"ComboObserverUnit", 0x3A692C8, {{248, 32}}, {}},
	{"StatusObserverUnit", 0x3A657A0, {{248, 240}}, {}},
	{"StatsUnit", 0x3A665F0, {{248, 77804}}, {}},
	{"VfxManagerUnit", 0x3A64BE8, {{248, 496}}, {}},
	{"ReplayUnit", sfv::unit::kReplayUnitType, {{248, 16}}, {}},
};

const OwnerSpec kParts[] = {
	{"CharaActionPart", 0x3A64A98, {{216, 15808}, {16048, 4}, {16104, 4}, {16112, 8}}, {{0x3EC8, {48, 224}}, {0x3ED8, {48, 224}}}},
	{"ProjectileActionPart", 0x3A64D70, {{216, 15808}, {16048, 4}}, {{0x3ED0, {48, 224}}}},
	{"CharaStatusPart", 0x3A64B08,
		{{192, 372}, {568, 204}, {812, 12}, {876, 8}, {900, 4}, {948, 8}, {972, 4}, {1020, 8}, {1044, 4}, {1092, 8},
			{1116, 132}, {1288, 76}},
		{}},
	{"CharaAbilityPart", 0x3A65850, {{192, 96}, {708, 32}}, {}},
	{"CharaCommandPart", 0x3A65C18, {{280, 28}, {312, 4}}, {{0xD8, {16, 1692}}}},
	{"CharaItemPart", 0x3A663A0, {{192, 24}}, {}},
	{"CharaMotionPart", 0x3A64750, {{192, 13092}, {13288, 192}}, {}},
	{"CharaMaterialPart", 0x3A65C50, {{216, 724}, {944, 1624}}, {}},
	{"CharaVisualPart", 0x3A64AD0, {{1752, 392}}, {}},
	{"AttackDataPart", 0x3A69338, {{192, 632}}, {}},
	{"ProjectileVisualPart", 0x3A66D00, {{192, 44}, {240, 136}}, {}},
};

// The root unit (the battle's mode unit) owns the rollback manager at this
// offset and registers BattleData+6176..+6404 (root+272 is BattleData).
constexpr uint32_t kManagerInRoot = 0x8A400;
const OwnerSpec kRoot = {"ModeUnit", 0, {{6448, 228}}, {}};

// The character-specific parts (CharaUniquePart_NNN) derive from this type
// and register a few words each, which the map lists per character only.
constexpr uint32_t kUniquePartBaseRva = 0x3A64B40;

// A unit's parts: a vector of 16-byte shared pointers.
constexpr uint32_t kPartsBegin = 192;
constexpr uint32_t kPartsEnd = 200;
constexpr size_t kMaxParts = 256;
constexpr uint32_t kMaxUnitsOfAType = 64;
// A type object: +8 its parent type.
constexpr uint32_t kTypeParent = 8;
// How far past an object's start an unowned entry is still reported as
// "near" it.
constexpr uintptr_t kNearWindow = 0x40000;

uintptr_t ImageBase()
{
	return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

// A part's concrete type: its vtable's first function returns it.
uintptr_t TypeOf(uint8_t* object)
{
	uintptr_t vtable = 0, getter = 0;
	if (!TryRead(object, &vtable) || !TryRead(reinterpret_cast<void*>(vtable), &getter) || !getter) return 0;
	using TypeFn = uintptr_t (*)(uint8_t*);
	return reinterpret_cast<TypeFn>(getter)(object);
}

bool DerivesFrom(uintptr_t type, uintptr_t base)
{
	for (int depth = 0; type && depth < 16; depth++) {
		if (type == base) return true;
		if (!TryRead(reinterpret_cast<void*>(type + kTypeParent), &type)) return false;
	}
	return false;
}

std::string Hex(uintptr_t v)
{
	char text[24];
	snprintf(text, sizeof(text), "0x%llX", (unsigned long long)v);
	return text;
}

} // namespace

void RegionNames::AddObject(const std::string& name, uint8_t* object, const OwnerSpec& spec)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(object);
	objects_.emplace_back(base, name);
	for (const RunSpec& r : spec.runs) runs_[base + r.offset] = {{name + "+" + Hex(r.offset), spec.name, r.offset}, r.size};
	for (const SubObjectSpec& s : spec.subObjects) {
		uint8_t* sub = nullptr;
		if (!TryRead(object + s.pointerAt, &sub) || !sub) continue;
		const std::string subName = name + "->" + Hex(s.pointerAt);
		objects_.emplace_back(reinterpret_cast<uintptr_t>(sub), subName);
		// A sub-object's runs belong to their own object, not the owner's
		// layout: they carry no owner type, so no owner rule applies.
		runs_[reinterpret_cast<uintptr_t>(sub) + s.run.offset] = {{subName + "+" + Hex(s.run.offset), nullptr, s.run.offset},
			s.run.size};
	}
}

void RegionNames::AddParts(const std::string& unitName, uint8_t* unit)
{
	GameSharedPtr* begin = nullptr;
	GameSharedPtr* end = nullptr;
	if (!TryRead(unit + kPartsBegin, &begin) || !TryRead(unit + kPartsEnd, &end) || end < begin ||
		size_t(end - begin) > kMaxParts) {
		return;
	}
	const uintptr_t image = ImageBase();
	int seen[std::size(kParts)] = {};
	for (GameSharedPtr* p = begin; p < end; p++) {
		GameSharedPtr part{};
		if (!TryRead(p, &part) || !part.object) continue;
		const uintptr_t type = TypeOf(part.object);
		const auto spec = std::find_if(std::begin(kParts), std::end(kParts),
			[&](const OwnerSpec& s) { return image + s.typeRva == type; });
		if (spec == std::end(kParts)) {
			const bool unique = DerivesFrom(type, image + kUniquePartBaseRva);
			const std::string name = unitName + "." + (unique ? "CharaUniquePart" : "Part") + "@" + Hex(type - image);
			objects_.emplace_back(reinterpret_cast<uintptr_t>(part.object), name);
			unknownParts_.push_back(name);
			continue;
		}
		const int index = seen[spec - std::begin(kParts)]++;
		AddObject(unitName + "." + spec->name + (index ? "[" + std::to_string(index) + "]" : ""), part.object, *spec);
	}
}

void RegionNames::Collect(uint8_t* anyUnit, uint8_t* manager)
{
	*this = RegionNames{};
	if (manager) AddObject(kRoot.name, manager - kManagerInRoot, kRoot);
	for (const OwnerSpec& spec : kUnits) {
		for (uint32_t k = 0; k < kMaxUnitsOfAType; k++) {
			uint8_t* unit = GameFindUnit(anyUnit, spec.typeRva, k);
			if (!unit) break;
			const std::string name = std::string(spec.name) + "[" + std::to_string(k) + "]";
			AddObject(name, unit, spec);
			AddParts(name, unit);
		}
	}
	std::sort(objects_.begin(), objects_.end());
}

NamedRun RegionNames::Name(const uint8_t* ptr, size_t size)
{
	const uintptr_t at = reinterpret_cast<uintptr_t>(ptr);
	const auto it = runs_.find(at);
	if (it == runs_.end()) {
		unowned_.push_back(Hex(at) + "/" + std::to_string(size) + " " + Nearest(at));
		return NamedRun{};
	}
	Run& run = it->second;
	run.seen = true;
	named_++;
	if (run.expectedSize != size) {
		otherSize_.push_back(run.named.name + ": " + std::to_string(size) + " bytes, the map says " +
			std::to_string(run.expectedSize));
	}
	return run.named;
}

std::string RegionNames::Nearest(uintptr_t ptr) const
{
	auto it = std::upper_bound(objects_.begin(), objects_.end(), std::make_pair(ptr, std::string("\x7f")));
	if (it == objects_.begin()) return "(no known object below)";
	--it;
	if (ptr - it->first > kNearWindow) return "(no known object within " + Hex(kNearWindow) + ")";
	return "(" + it->second + "+" + Hex(ptr - it->first) + "?)";
}

void RegionNames::LogSummary()
{
	int missing = 0;
	for (const auto& [at, run] : runs_) missing += !run.seen;
	Log("regions: %d named from %zu known objects; %zu unowned, %zu of another size, %d expected run(s) not in the table, "
		"%zu unknown part(s)",
		named_, objects_.size(), unowned_.size(), otherSize_.size(), missing, unknownParts_.size());
	for (const std::string& u : unowned_) Log("regions:   unowned %s", u.c_str());
	for (const std::string& o : otherSize_) Log("regions:   size %s", o.c_str());
	for (const auto& [at, run] : runs_) {
		if (!run.seen) Log("regions:   not in the table: %s/%zu", run.named.name.c_str(), run.expectedSize);
	}
	for (const std::string& p : unknownParts_) Log("regions:   unknown part %s", p.c_str());
}

} // namespace sfve::mod
