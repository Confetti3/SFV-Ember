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
		AddGameRun(layout, run, size_t(r - begin), r->ptr, (size_t)r->size, applied);
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

// A run may match more than one row (Balrog's part has two, one for each
// padding of its records), and every row that matches applies.
size_t AddGameRun(StateLayout& layout, const NamedRun& run, size_t entry, uint8_t* ptr, size_t size, int* applied)
{
	namespace m = sfv::manager;
	bool matched[std::size(m::kRunRules)] = {};
	const char* kind = "";
	for (size_t k = 0; run.owner && k < std::size(m::kRunRules); k++) {
		const m::RunRule& rule = m::kRunRules[k];
		if (strcmp(rule.owner, run.owner) != 0) continue;
		if (rule.offset != m::kAnyRun && rule.offset != run.offset) continue;
		matched[k] = true;
		if (applied) applied[k]++;
		if (*rule.kind) kind = rule.kind;
	}
	const std::string name = run.name.empty() ? "game." + std::to_string(entry) : run.name;
	const size_t index = layout.Add(name + kind, ptr, size, !*kind);
	for (size_t k = 0; k < std::size(m::kRunRules); k++) {
		if (!matched[k]) continue;
		const m::RunRule& rule = m::kRunRules[k];
		// Masked words are 8 bytes, the peers' dwords 4.
		const size_t unit = rule.peersOnly ? 4 : 8;
		const size_t width = size_t(rule.maskTo) - rule.maskFrom;
		for (size_t from = rule.maskFrom; from < size; from += rule.maskEvery) {
			for (size_t at = from; at - from < width && at < size; at += unit) {
				if (rule.peersOnly) {
					layout.IgnoreForPeers(index, at);
				} else {
					layout.IgnoreWord(index, at);
				}
			}
			if (!rule.maskEvery) break;
		}
	}
	return index;
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
