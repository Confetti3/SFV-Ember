#include "mod/SfvAdapter.hxx"

#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <iterator>

namespace sfve::mod {

namespace {

struct RegionEntry {
	uint8_t* ptr;
	uint64_t size;
};

constexpr uint64_t kMaxRegionSize = 64ull << 20;

// Heap and code addresses of a Windows x64 process (1 TB up to the end of
// user space), 4-byte aligned. They differ between processes even when the
// state is the same, so hashes read them as zero.
bool LooksLikeAddress(uint64_t value)
{
	return value >= (1ull << 40) && value <= 0x00007FFFFFFFFFFFull && (value & 3) == 0;
}
constexpr size_t kMaxRegions = 4096;

template <typename T>
T& Field(uint8_t* object, uint32_t offset)
{
	return *reinterpret_cast<T*>(object + offset);
}

} // namespace

void SfvAdapter::SeeBattleFrame(uint8_t* battleFrame, BattleFrameFn original)
{
	original_ = original;
	if (battleFrame == battleFrame_) return;
	Forget();
	battleFrame_ = battleFrame;
	Log("adapter: new battle frame %p", (void*)battleFrame);
}

void SfvAdapter::Forget()
{
	bound_ = false;
	layout_.Clear();
	gameRegions_ = 0;
	postRestore_.clear();
	inputUnit_ = nullptr;
	rollbackUnit_ = nullptr;
	flow_.Forget();
}

bool SfvAdapter::Bind()
{
	if (bound_) return true;
	if (!battleFrame_ || !inputUnit_ || !rollbackUnit_) return false;
	uint8_t* flowUnit = FlowUnit();
	uint8_t* ruleUnit = FindUnit(sfv::unit::kVersusRuleUnitType);
	if (!flowUnit || !ruleUnit) return false; // not created yet

	layout_.Clear();
	layout_.SetAddressTest(LooksLikeAddress);
	uint8_t* manager = GameGetManager(rollbackUnit_);
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	if (!AddGameRegions(manager) || !flow_.Bind(flowUnit, base)) {
		layout_.Clear();
		return false;
	}
	gameRegions_ = layout_.Regions().size();
	CollectPostRestore(manager);
	AddStateOutsideTheGameSnapshot(ruleUnit, base);
	bound_ = true;
	Log("adapter: bound: %zu game regions + %zu outside the game's snapshot, %zu bytes", gameRegions_,
		layout_.Regions().size() - gameRegions_, layout_.TotalSize());
	return true;
}

// Every region of the game's rollback manager. The presentation regions and
// the replay unit's counters are restored but not hashed.
bool SfvAdapter::AddGameRegions(uint8_t* manager)
{
	auto* begin = Field<RegionEntry*>(manager, sfv::manager::kRegionsBegin);
	auto* end = Field<RegionEntry*>(manager, sfv::manager::kRegionsEnd);
	if (!begin || end <= begin || size_t(end - begin) > kMaxRegions) {
		Log("adapter: the rollback manager has no regions (%p..%p)", (void*)begin, (void*)end);
		return false;
	}
	uint8_t* replayUnit = FindUnit(sfv::unit::kReplayUnitType);
	const uint8_t* replayCounters = replayUnit ? replayUnit + sfv::replayunit::kCounters : nullptr;
	if (!replayUnit) Log("adapter: no replay unit; its tick counters stay in the hash");
	int presentation[std::size(sfv::manager::kPresentationSizes)] = {};
	for (RegionEntry* r = begin; r < end; r++) {
		if (!r->ptr || r->size == 0 || r->size > kMaxRegionSize) {
			Log("adapter: region %zu looks wrong (%p, %llu bytes)", size_t(r - begin), (void*)r->ptr, r->size);
			return false;
		}
		const char* kind = r->ptr == replayCounters && r->size == sfv::replayunit::kCountersSize ? ".replaycount" : "";
		for (size_t k = 0; k < std::size(sfv::manager::kPresentationSizes); k++) {
			if (r->size != sfv::manager::kPresentationSizes[k].size) continue;
			kind = ".presentation";
			presentation[k]++;
		}
		char name[40];
		snprintf(name, sizeof(name), "game.%zu%s", size_t(r - begin), kind);
		layout_.Add(name, r->ptr, (size_t)r->size, !*kind);
	}
	for (size_t k = 0; k < std::size(sfv::manager::kPresentationSizes); k++) {
		if (presentation[k] != sfv::manager::kPresentationSizes[k].count) {
			Log("adapter: expected %d presentation region(s) of %llu bytes, found %d; hashes may include them",
				sfv::manager::kPresentationSizes[k].count,
				(unsigned long long)sfv::manager::kPresentationSizes[k].size, presentation[k]);
		}
	}
	return true;
}

void SfvAdapter::CollectPostRestore(uint8_t* manager)
{
	postRestore_.clear();
	uint8_t* sentinel = Field<uint8_t*>(manager, sfv::manager::kPostRestoreList);
	for (uint8_t* node = Field<uint8_t*>(sentinel, 0); node && node != sentinel; node = Field<uint8_t*>(node, 0)) {
		if (!Field<uint64_t>(node, sfv::manager::kNodeToken)) continue;
		postRestore_.push_back({Field<PostRestore>(node, sfv::manager::kNodeFunction).fn,
			Field<void*>(node, sfv::manager::kNodeContext)});
	}
}

// What a battle tick changes that the game's snapshot leaves out: its netcode
// manages the counters itself, and its own rollback never crossed a round's
// end, which is where the rest changes.
void SfvAdapter::AddStateOutsideTheGameSnapshot(uint8_t* ruleUnit, uintptr_t base)
{
	layout_.Add("global.rng", reinterpret_cast<uint8_t*>(base + sfv::rng::kGlobal + sfv::rng::kState), 4);
	layout_.Add("battleframe", battleFrame_ + sfv::battleframe::kState, sfv::battleframe::kStateSize);
	layout_.Add("inputunit.counters", inputUnit_ + sfv::inputunit::kCounters, sfv::inputunit::kCountersSize);
	layout_.Add("ruleunit.judge", ruleUnit + sfv::ruleunit::kJudgeState, 4);
	uint8_t* battleData = GameGetBattleData(rollbackUnit_);
	layout_.Add("battledata.results", battleData + sfv::battledata::kResults, sfv::battledata::kResultsSize);
	layout_.Add("battledata.flowflags", battleData + sfv::battledata::kFlowFlags, sfv::battledata::kFlowFlagsSize);
	layout_.Add("flow", &flow_.Record(), sizeof(FlowRecord));
}

void SfvAdapter::OnInputRecord(uint8_t* inputUnit)
{
	inputUnit_ = inputUnit;
	ticksThisUpdate_++;
	auto* values = reinterpret_cast<uint32_t*>(inputUnit + sfv::inputunit::kPadValues);
	for (int p = 0; p < kPlayers; p++) {
		sampled_[p] = values[p]; // what the pads say now, for the next frame's input
		if (injecting_) values[p] = pending_.p[p];
	}
}

bool SfvAdapter::Fighting() const
{
	return flow_.Announced() == sfv::flow::kFight;
}

void SfvAdapter::RunUpdate()
{
	ticksThisUpdate_ = 0;
	original_(battleFrame_);
	ticksInLastUpdate_ = ticksThisUpdate_;
}

void SfvAdapter::RunOriginal()
{
	injecting_ = false;
	RunUpdate();
}

void SfvAdapter::Advance(const FrameInputs& inputs, bool)
{
	// One GGPO frame is one update: the game's own replay mechanism (extra
	// ticks) is not used, rollbacks call Advance once per frame instead.
	Field<uint32_t>(battleFrame_, sfv::battleframe::kExtraTicks) = 0;
	pending_ = inputs;
	injecting_ = true;
	RunUpdate();
	injecting_ = false;
}

int SfvAdapter::ReseedRandom(uint32_t seed)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	const uint64_t vtable = base + sfv::rng::kVtable;
	int reseeded = 0;
	for (const StateRegion& r : layout_.Regions()) {
		const uintptr_t begin = reinterpret_cast<uintptr_t>(r.ptr);
		for (uintptr_t at = (begin + 7) & ~uintptr_t(7); at + sfv::rng::kState + 4 <= begin + r.size; at += 8) {
			if (*reinterpret_cast<const uint64_t*>(at) != vtable) continue;
			// Region size and offset place the generator the same way in every process.
			const uint32_t place = (uint32_t)(r.size * 2654435761u) ^ (uint32_t)((at - begin) * 40503u);
			*reinterpret_cast<uint32_t*>(at + sfv::rng::kState) = seed ^ place;
			reseeded++;
		}
	}
	*reinterpret_cast<uint32_t*>(base + sfv::rng::kGlobal + sfv::rng::kState) = seed;
	return reseeded + 1;
}

void SfvAdapter::AfterLoad()
{
	// The callbacks take a frame id but none of them reads it (they call a
	// member function of the object they were registered for).
	const int32_t frameId[2] = {0, 0};
	for (const PostRestore& cb : postRestore_) cb.fn(cb.context, frameId);
	flow_.Apply();
}

std::string SfvAdapter::BuildId() const
{
	char text[64];
	snprintf(text, sizeof(text), "sfv-13173604-%016llx", (unsigned long long)layout_.LayoutId());
	return text;
}

std::string SfvAdapter::Describe() const
{
	const FlowRollback::Stats& flow = flow_.GetStats();
	char text[384];
	snprintf(text, sizeof(text),
		"{\"bound\":%s,\"gameRegions\":%zu,\"regions\":%zu,\"bytes\":%zu,\"ticksLastUpdate\":%u,\"sampled\":[%u,%u],"
		"\"flowState\":%d,\"flowSwitches\":%llu,\"delegatesRebuilt\":%llu,\"refusedReleases\":%llu}",
		bound_ ? "true" : "false", gameRegions_, layout_.Regions().size(), layout_.TotalSize(), ticksInLastUpdate_,
		sampled_[0], sampled_[1], flow_.Announced(), (unsigned long long)flow.stateSwitches,
		(unsigned long long)flow.delegatesRebuilt, (unsigned long long)flow.refusedReleases);
	return text;
}

} // namespace sfve::mod
