#include "mod/GameRegions.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <string.h>

#include <iterator>
#include <string>

namespace sfve::mod {

namespace {

struct RegionEntry {
	uint8_t* ptr;
	uint64_t size;
};

constexpr uint64_t kMaxRegionSize = 64ull << 20;

constexpr size_t kMaxRegions = 4096;

} // namespace

// Every region of the game's rollback manager, named by its owner; the runs
// sfv::manager::kRunRules lists are restored but not hashed, or masked.
bool AddGameRegions(StateLayout& layout, RegionNames& names, uint8_t* rollbackUnit, uint8_t* manager)
{
	auto* begin = Field<RegionEntry*>(manager, sfv::manager::kRegionsBegin);
	auto* end = Field<RegionEntry*>(manager, sfv::manager::kRegionsEnd);
	if (!begin || end <= begin || size_t(end - begin) > kMaxRegions) {
		Log("adapter: the rollback manager has no regions (%p..%p)", (void*)begin, (void*)end);
		return false;
	}
	names.Collect(rollbackUnit, manager);
	int applied[std::size(sfv::manager::kRunRules)] = {};
	for (RegionEntry* r = begin; r < end; r++) {
		if (!r->ptr || r->size == 0 || r->size > kMaxRegionSize) {
			Log("adapter: region %zu looks wrong (%p, %llu bytes)", size_t(r - begin), (void*)r->ptr, r->size);
			return false;
		}
		const NamedRun run = names.Name(r->ptr, (size_t)r->size);
		const sfv::manager::RunRule* rule = nullptr;
		for (size_t k = 0; run.owner && k < std::size(sfv::manager::kRunRules); k++) {
			const sfv::manager::RunRule& candidate = sfv::manager::kRunRules[k];
			if (strcmp(candidate.owner, run.owner) != 0) continue;
			if (candidate.offset != sfv::manager::kAnyRun && candidate.offset != run.offset) continue;
			rule = &candidate;
			applied[k]++;
		}
		const char* kind = rule ? rule->kind : "";
		const std::string name = run.name.empty() ? "game." + std::to_string(r - begin) : run.name;
		const size_t index = layout.Add(name + kind, r->ptr, (size_t)r->size, !*kind);
		if (rule) {
			const size_t width = size_t(rule->maskTo) - rule->maskFrom;
			for (size_t from = rule->maskFrom; from < r->size; from += rule->maskEvery) {
				for (size_t at = from; at - from < width && at < r->size; at += 8) layout.IgnoreWord(index, at);
				if (!rule->maskEvery) break;
			}
		}
	}
	names.LogSummary();
	for (size_t k = 0; k < std::size(sfv::manager::kRunRules); k++) {
		if (!applied[k]) {
			Log("adapter: no %s run at +0x%X in this battle; its rule applied nowhere", sfv::manager::kRunRules[k].owner,
				sfv::manager::kRunRules[k].offset);
		}
	}
	return true;
}

void CollectPostRestore(uint8_t* manager, std::vector<GamePostRestore>& out)
{
	out.clear();
	uint8_t* sentinel = Field<uint8_t*>(manager, sfv::manager::kPostRestoreList);
	for (uint8_t* node = Field<uint8_t*>(sentinel, 0); node && node != sentinel; node = Field<uint8_t*>(node, 0)) {
		if (!Field<uint64_t>(node, sfv::manager::kNodeToken)) continue;
		out.push_back({Field<GamePostRestore>(node, sfv::manager::kNodeFunction).fn,
			Field<void*>(node, sfv::manager::kNodeContext)});
	}
}

} // namespace sfve::mod
