#include "mod/SfvAdapter.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvLayoutRules.hxx"
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
	if (!identity_.See(battleFrame)) return;
	const bool sameAddress = battleFrame == battleFrame_;
	Forget();
	battleFrame_ = battleFrame;
	Log("adapter: new battle frame %p (battle %llu, %llu frame(s) constructed%s)", (void*)battleFrame,
		(unsigned long long)identity_.Battle(), (unsigned long long)identity_.Constructions(),
		sameAddress ? ", at the last battle's address" : "");
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
	OutsideState outside = {reinterpret_cast<uintptr_t>(ruleUnit), reinterpret_cast<uintptr_t>(battleData),
		reinterpret_cast<uintptr_t>(battleFrame_), reinterpret_cast<uintptr_t>(inputUnit_), base};
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, (uint32_t)index);
		if (!chara) {
			WaitingToBind("both fighters");
			return false;
		}
		for (size_t row = 0; row < std::size(sfv::kPartRunsOutsideSnapshot); row++) {
			outside.fighterParts[index][row] =
				reinterpret_cast<uintptr_t>(GameFindPart(chara, sfv::kPartRunsOutsideSnapshot[row].partType));
		}
	}
	gameInstance_ = reinterpret_cast<uint8_t* (*)()>(base + sfv::kGameInstance.rva);
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
	AddStateOutsideTheGameSnapshot(layout_, outside);
	flow_.AddTo(layout_);
	battleData_ = battleData;
	bound_ = true;
	Log("adapter: bound: %zu game regions + %zu outside the game's snapshot, %zu bytes; intros %s, input source %d",
		gameRegions_, layout_.Regions().size() - gameRegions_, layout_.TotalSize(),
		Field<int32_t>(battleData, sfv::battledata::kIntroSkippable) ? "skippable" : "not skippable",
		Field<int32_t>(battleData, sfv::battledata::kInputSource));
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

bool SfvAdapter::SetPracticeGauges()
{
	namespace practice = sfv::practice;
	using SetParamFn = bool (*)(uint8_t* table, int param, int value);
	if (!bound_ || !battleData_) return Log("adapter: no Practice gauges: the battle is not bound"), false;
	const auto set = reinterpret_cast<SetParamFn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
		sfv::kSetPlayerParam.rva);
	constexpr int kGauges[][2] = {{practice::kVitalParam, practice::kVitalRefill},
		{practice::kExtraParam, practice::kExtraRefill}, {practice::kVGaugeParam, practice::kVGaugeRefill}};
	bool sides[kPlayers] = {};
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, (uint32_t)index);
		const int side = chara ? Field<int32_t>(chara, practice::kCharaSide) : -1;
		if (side < 0 || side >= kPlayers || sides[side]) {
			return Log("adapter: no Practice gauges: fighter %d has side %d", index, side), false;
		}
		sides[side] = true;
		// The battle data is the adapter's (const for the rest of it): its
		// tables are what Training's menu writes too.
		uint8_t* table = const_cast<uint8_t*>(battleData_) + practice::kPlayerParams + practice::kPlayerParamsStride * side;
		for (const auto& [param, value] : kGauges) {
			if (!set(table, param, value) || !set(chara + practice::kCharaParams, param, value)) {
				return Log("adapter: the game refused Practice gauge %d = %d for side %d", param, value, side), false;
			}
		}
	}
	Log("adapter: Practice gauges set for both players (vital %d, EX %d, V-gauge %d)", practice::kVitalRefill,
		practice::kExtraRefill, practice::kVGaugeRefill);
	return true;
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
			if (strcmp(candidate.owner, run.owner) != 0) continue;
			if (candidate.offset != sfv::manager::kAnyRun && candidate.offset != run.offset) continue;
			rule = &candidate;
			applied[k]++;
		}
		const char* kind = rule ? rule->kind : "";
		const std::string name = run.name.empty() ? "game." + std::to_string(r - begin) : run.name;
		const size_t index = layout_.Add(name + kind, r->ptr, (size_t)r->size, !*kind);
		if (rule) {
			const size_t width = size_t(rule->maskTo) - rule->maskFrom;
			for (size_t from = rule->maskFrom; from < r->size; from += rule->maskEvery) {
				for (size_t at = from; at - from < width && at < r->size; at += 8) layout_.IgnoreWord(index, at);
				if (!rule->maskEvery) break;
			}
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

bool SfvAdapter::MatchDecided() const
{
	return battleData_ && Field<int32_t>(battleData_, sfv::battledata::kDecided) != 0;
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

uint32_t SfvAdapter::StartTime() const
{
	return battleData_ ? Field<uint32_t>(battleData_, sfv::battledata::kStartTime) : 0;
}

void SfvAdapter::SetStartTime(uint32_t value)
{
	if (battleData_) Field<uint32_t>(battleData_, sfv::battledata::kStartTime) = value;
}

void SfvAdapter::AfterLoad()
{
	// The callbacks take the restored state's frame id, as the game's own load
	// passes it (rva 0x48A310); the sound callbacks drop what was played after
	// it (rva 0x4D5820, 0x4B6230).
	namespace iu = sfv::inputunit;
	const int32_t inputFrame = Field<int32_t>(inputUnit_, iu::kInputFrame);
	const int32_t frameId[2] = {Field<int32_t>(inputUnit_, iu::kTag), inputFrame - 1};
	for (const PostRestore& cb : postRestore_) cb.fn(cb.context, frameId);
	if (uint8_t* instance = gameInstance_()) Field<int32_t>(instance, iu::kInstanceInputFrame) = inputFrame;
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
	char text[448];
	snprintf(text, sizeof(text),
		"{\"battle\":%llu,\"bound\":%s,\"gameRegions\":%zu,\"regions\":%zu,\"bytes\":%zu,\"ticksLastUpdate\":%u,\"sampled\":[%u,%u],"
		"\"flowState\":%d,\"flowSwitches\":%llu,\"delegatesRebound\":%llu,\"keptUses\":%llu}",
		(unsigned long long)identity_.Battle(), bound_ ? "true" : "false", gameRegions_, layout_.Regions().size(), layout_.TotalSize(), ticksInLastUpdate_,
		sampled_[0], sampled_[1], flow_.Announced(), (unsigned long long)flow.stateSwitches,
		(unsigned long long)flow.delegatesRebound, (unsigned long long)flow.keptUses);
	return text;
}

} // namespace sfve::mod
