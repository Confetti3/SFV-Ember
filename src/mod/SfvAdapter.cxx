#include "mod/SfvAdapter.hxx"

#include "mod/GameMemory.hxx"
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

constexpr size_t kMaxRegions = 4096;

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
	battleData_ = nullptr;
	layout_.Clear();
	gameRegions_ = 0;
	postRestore_.clear();
	inputUnit_ = nullptr;
	rollbackUnit_ = nullptr;
	waitingFor_ = nullptr;
	flow_.Forget();
}

bool SfvAdapter::Bind()
{
	if (bound_) return true;
	if (!battleFrame_ || !inputUnit_ || !rollbackUnit_) return false;
	uint8_t* flowUnit = FindUnit(sfv::unit::kFlowUnitType);
	uint8_t* ruleUnit = FindUnit(sfv::unit::kVersusRuleUnitType);
	uint8_t* battleData = GameGetBattleData(rollbackUnit_);
	if (!flowUnit || !ruleUnit || !battleData) {
		WaitingToBind(!flowUnit ? "the flow unit" : !ruleUnit ? "the versus rule unit" : "the match data");
		return false;
	}
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	if (!flow_.Bind(flowUnit, base)) {
		WaitingToBind("a flow it can record");
		return false;
	}
	layout_.Clear();
	layout_.SetAddressTest(LooksLikeAddress);
	uint8_t* manager = GameGetManager(rollbackUnit_);
	if (!AddGameRegions(manager)) {
		layout_.Clear();
		flow_.Forget();
		WaitingToBind("a usable region table");
		return false;
	}
	gameRegions_ = layout_.Regions().size();
	CollectPostRestore(manager);
	AddStateOutsideTheGameSnapshot(ruleUnit, battleData, base);
	battleData_ = battleData;
	bound_ = true;
	Log("adapter: bound: %zu game regions + %zu outside the game's snapshot, %zu bytes", gameRegions_,
		layout_.Regions().size() - gameRegions_, layout_.TotalSize());
	return true;
}

// Logs, once per battle, what keeps the layout from being built (the battle
// runs unchanged meanwhile).
void SfvAdapter::WaitingToBind(const char* what)
{
	if (waitingFor_ == what) return;
	waitingFor_ = what;
	Log("adapter: not bound yet: waiting for %s", what);
}

uint8_t* SfvAdapter::FindUnit(uint32_t typeRva) const
{
	return GameFindUnit(rollbackUnit_, typeRva);
}

// Every region of the game's rollback manager, named by its owner; the runs
// sfv::manager::kRunRules lists are restored but not hashed, or masked.
bool SfvAdapter::AddGameRegions(uint8_t* manager)
{
	auto* begin = Field<RegionEntry*>(manager, sfv::manager::kRegionsBegin);
	auto* end = Field<RegionEntry*>(manager, sfv::manager::kRegionsEnd);
	if (!begin || end <= begin || size_t(end - begin) > kMaxRegions) {
		Log("adapter: the rollback manager has no regions (%p..%p)", (void*)begin, (void*)end);
		return false;
	}
	regionNames_.Collect(rollbackUnit_, manager);
	int applied[std::size(sfv::manager::kRunRules)] = {};
	for (RegionEntry* r = begin; r < end; r++) {
		if (!r->ptr || r->size == 0 || r->size > kMaxRegionSize) {
			Log("adapter: region %zu looks wrong (%p, %llu bytes)", size_t(r - begin), (void*)r->ptr, r->size);
			return false;
		}
		const NamedRun run = regionNames_.Name(r->ptr, (size_t)r->size);
		const sfv::manager::RunRule* rule = nullptr;
		for (size_t k = 0; run.owner && k < std::size(sfv::manager::kRunRules); k++) {
			const sfv::manager::RunRule& candidate = sfv::manager::kRunRules[k];
			if (strcmp(candidate.owner, run.owner) != 0 || candidate.offset != run.offset) continue;
			rule = &candidate;
			applied[k]++;
		}
		const char* kind = rule ? rule->kind : "";
		const std::string name = run.name.empty() ? "game." + std::to_string(r - begin) : run.name;
		const size_t index = layout_.Add(name + kind, r->ptr, (size_t)r->size, !*kind);
		if (rule) {
			for (size_t at = rule->maskFrom; at < rule->maskTo && at < r->size; at += 8) layout_.IgnoreWord(index, at);
		}
	}
	regionNames_.LogSummary();
	for (size_t k = 0; k < std::size(sfv::manager::kRunRules); k++) {
		if (!applied[k]) {
			Log("adapter: no %s run at +0x%X in this battle; its rule applied nowhere", sfv::manager::kRunRules[k].owner,
				sfv::manager::kRunRules[k].offset);
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
// end, which is where the rest changes. The counters' per-process values (the
// updates each process ran before binding, a clock reading) are left out of
// the hash peers compare.
void SfvAdapter::AddStateOutsideTheGameSnapshot(uint8_t* ruleUnit, uint8_t* battleData, uintptr_t base)
{
	layout_.Add("global.rng", reinterpret_cast<uint8_t*>(base + sfv::rng::kGlobal + sfv::rng::kState), 4);
	namespace bf = sfv::battleframe;
	namespace iu = sfv::inputunit;
	const size_t frame = layout_.Add("battleframe", battleFrame_ + bf::kState, bf::kStateSize);
	for (uint32_t own : {bf::kUpdates, bf::kTickedUpdates, bf::kClock}) layout_.IgnoreForPeers(frame, own - bf::kState);
	const size_t counters = layout_.Add("inputunit.counters", inputUnit_ + iu::kCounters, iu::kCountersSize);
	layout_.IgnoreForPeers(counters, iu::kSyncClock - iu::kCounters);
	layout_.Add("ruleunit.judge", ruleUnit + sfv::ruleunit::kJudgeState, 4);
	namespace bd = sfv::battledata;
	layout_.Add("battledata.results", battleData + bd::kResults, bd::kRounds - bd::kResults);
	for (int i = 0; i < bd::kMaxRounds; i++) {
		uint8_t* record = battleData + bd::kRounds + i * bd::kRoundStride;
		const std::string name = "battledata.round" + std::to_string(i + 1);
		layout_.Add(name, record, bd::kRoundStats);
		layout_.Add(name + ".stats", record + bd::kRoundStats, bd::kRoundStatsSize, false);
	}
	layout_.Add("battledata.judgement", battleData + bd::kDecided, bd::kResults + bd::kResultsSize - bd::kDecided);
	layout_.Add("battledata.flowflags", battleData + sfv::battledata::kFlowFlags, sfv::battledata::kFlowFlagsSize);
	flow_.AddTo(layout_);
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
	flow_.Capture(); // the record mirrors the game whenever it is saved or hashed
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
			if (r.size == sfv::rng::kFighterPairSize && at == begin) {
				*reinterpret_cast<uint32_t*>(at + sfv::rng::kFighterDraw) = 0;
			}
			reseeded++;
		}
	}
	*reinterpret_cast<uint32_t*>(base + sfv::rng::kGlobal + sfv::rng::kState) = seed;
	// The clock reading the round starts reseed the fighters' generators from.
	Field<uint32_t>(battleData_, sfv::battledata::kStartTime) = seed;
	return reseeded + 1;
}

void SfvAdapter::AfterLoad()
{
	// The callbacks take a frame id but none of them reads it (they call a
	// member function of the object they were registered for).
	const int32_t frameId[2] = {0, 0};
	for (const PostRestore& cb : postRestore_) cb.fn(cb.context, frameId);
	flow_.Apply();
	flow_.Capture(); // what the restore left in the game, so a load check sees it
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
		"\"flowState\":%d,\"flowSwitches\":%llu,\"delegatesRebound\":%llu,\"keptUses\":%llu}",
		bound_ ? "true" : "false", gameRegions_, layout_.Regions().size(), layout_.TotalSize(), ticksInLastUpdate_,
		sampled_[0], sampled_[1], flow_.Announced(), (unsigned long long)flow.stateSwitches,
		(unsigned long long)flow.delegatesRebound, (unsigned long long)flow.keptUses);
	return text;
}

} // namespace sfve::mod
