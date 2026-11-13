#include "mod/SfvAdapter.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/ProjectileRelease.hxx"
#include "mod/RngAudit.hxx"
#include "mod/SfvLayoutRules.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SocketReads.hxx"
#include "mod/TinyFighters.hxx"
#include "mod/VersusLaunch.hxx"

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

// How often the socket reads' counts and the keys' cost are logged.
constexpr uint64_t kSocketSummaryUpdates = 1800;

// How often the costs of the session's work are logged: every this many
// updates played forward (rollbacks' replays not counted).
constexpr uint64_t kCostAdvances = 600;

int64_t Ticks()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

// The CharaUnit index a region's name starts with ("CharaUnit[1].CharaActionPart+0xD8"
// is 1), or -1.
int CharaIndexOf(const std::string& name)
{
	static const char kPrefix[] = "CharaUnit[";
	if (name.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return -1;
	int index = 0;
	size_t at = sizeof(kPrefix) - 1;
	if (at >= name.size() || name[at] < '0' || name[at] > '9') return -1;
	for (; at < name.size() && name[at] >= '0' && name[at] <= '9' && index < 1000; at++) index = index * 10 + (name[at] - '0');
	return at < name.size() && name[at] == ']' ? index : -1;
}

} // namespace

void SfvAdapter::SeeBattleFrame(uint8_t* battleFrame, BattleFrameFn original)
{
	original_ = original;
	// The engine calls the battle once a frame, and evaluates the fighters'
	// poses between two calls from what the last update's visual task set: the
	// models now show the state the last update left (SocketReads).
	poseFresh_ = true;
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
	// The battle that was latched as a session's ends here, unless this is
	// the frame of the launch's own, still loading (ProjectileRelease).
	BattleLatch().NewBattle(LaunchedBattleLoading());
	bound_ = false;
	battleData_ = nullptr;
	layout_.Clear();
	gameRegions_ = 0;
	postRestore_.clear();
	inputUnit_ = nullptr;
	rollbackUnit_ = nullptr;
	waitingFor_ = nullptr;
	for (PackedInput& bits : neutralised_) bits = 0;
	flow_.Forget();
	// The socket reads' counts are the battle's own (SocketReads): logged as
	// it ends, then forgotten with the record.
	if (fighterUnitCount_ > 0) Log("sockets: the battle ends: %s", SocketReadsSummary().c_str());
	if (queuesCarried_) Log("actions: the battle ends: %llu time(s) a fighter unit's command queue was not empty between updates",
		(unsigned long long)queuesCarried_);
	queuesCarried_ = 0;
	queueLines_ = 0;
	for (FighterUnit& unit : fighterUnits_) unit = {};
	fighterUnitCount_ = 0;
	ForgetSocketReads();
	keyedUpdates_ = 0;
	keyTicks_ = 0;
	modelsPlaced_ = false;
	RngAuditForget();
	costs_ = {};
	layout_.TakeCosts(); // the next battle's are its own
	scenesSkip_ = nullptr;
	scenesIdleState_ = scenesIdlePhase_ = -1;
	decidedTicks_ = -1;
	pausedBits_ = 0;
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
	// Every fighter unit, the two fighters first and then any partners (Akira's,
	// sfv::unit::kMostCharaUnits), as the battle added them and as RegionNames
	// numbers their regions: their parts' state outside the snapshot, and what
	// their socket reads are keyed by.
	FighterUnit units[kMostFighterUnits];
	int unitCount = 0;
	for (uint32_t index = 0; index < sfv::unit::kMostCharaUnits; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, index);
		if (!chara) break;
		FighterUnit& unit = units[unitCount++];
		unit.chara = chara;
		if (!TryRead(chara + sfv::fighter::kSide, &unit.side) || unit.side < 0 || unit.side >= kPlayers) unit.side = -1;
		unit.motion = GameFindPart(chara, sfv::fighter::kMotionPartType);
		unit.action = GameFindPart(chara, sfv::fighter::kActionPartType);
		unit.visual = GameFindPart(chara, sfv::visual::kPartType);
		for (size_t row = 0; index < std::size(outside.fighterParts) && row < std::size(sfv::kPartRunsOutsideSnapshot); row++) {
			outside.fighterParts[index][row] =
				reinterpret_cast<uintptr_t>(GameFindPart(chara, sfv::kPartRunsOutsideSnapshot[row].partType));
		}
	}
	if (unitCount < kPlayers) {
		WaitingToBind("both fighters");
		return false;
	}
	gameInstance_ = reinterpret_cast<uint8_t* (*)()>(base + sfv::kGameInstance.rva);
	imageBase_ = base;
	flowRequest_ = reinterpret_cast<decltype(flowRequest_)>(base + sfv::kFlowRequest.rva);
	setFlowPhase_ = reinterpret_cast<decltype(setFlowPhase_)>(base + sfv::kSetFlowPhase.rva);
	markResultSkipped_ = reinterpret_cast<decltype(markResultSkipped_)>(base + sfv::kMarkResultSkipped.rva);
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
	for (int i = 0; i < unitCount; i++) fighterUnits_[i] = units[i];
	fighterUnitCount_ = unitCount;
	{
		// The units and the regions named after each (RegionNames), so a log
		// shows which partner a CharaUnit[k] is.
		std::string text;
		for (int i = 0; i < unitCount; i++) {
			size_t regions = 0;
			for (const StateRegion& r : layout_.Regions()) regions += CharaIndexOf(r.name) == i ? 1 : 0;
			char one[96];
			snprintf(one, sizeof(one), "%sCharaUnit[%d] P%d%s (%zu regions%s)", i ? ", " : "", i, units[i].side + 1,
				i >= kPlayers ? " partner" : "", regions, units[i].motion ? "" : ", no motion part: its reads unkeyed");
			text += one;
		}
		Log("adapter: socket reads keyed by each fighter unit's animation and place: %s", text.c_str());
		if (GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, sfv::unit::kMostCharaUnits)) {
			Log("adapter: more than %u fighter units; the ones past them are not kept at LOD 0, keyed or given their outside runs",
				sfv::unit::kMostCharaUnits);
		}
	}
	RngAuditBind(layout_, base);
	// A value the input unit takes from the match data as a round starts
	// (sfv::inputunit::kRoundStartValue), said to differ between machines:
	// logged on every copy, so two copies' lines compare.
	{
		namespace iu = sfv::inputunit;
		int32_t value = 0, overridden = 0, source = 0;
		const bool read = TryRead(inputUnit_ + iu::kRoundStartValue, &value) &&
			TryRead(inputUnit_ + iu::kRoundStartOverride, &overridden) && TryRead(battleData + iu::kRoundStartSource, &source);
		if (read) {
			Log("adapter: input unit +0x%X = %d at bind (its +0x%X %d; the battle data's +%u %d)", iu::kRoundStartValue, value,
				iu::kRoundStartOverride, overridden, iu::kRoundStartSource, source);
		} else {
			Log("adapter: input unit +0x%X could not be read at bind", iu::kRoundStartValue);
		}
	}
	Log("adapter: bound: %zu game regions + %zu outside the game's snapshot, %zu bytes; intros %s, input source %d",
		gameRegions_, layout_.Regions().size() - gameRegions_, layout_.TotalSize(),
		Field<int32_t>(battleData, sfv::battledata::kIntroSkippable) ? "skippable" : "not skippable",
		Field<int32_t>(battleData, sfv::battledata::kInputSource));
	// Whether the winner's demo would take the skip bit by itself (its gate,
	// rva 0x3F0020, wants this kind); the mod ends the scenes either way.
	Log("adapter: battle kind %d (the winner's demo opens its own skip for kind %d only)",
		Field<int32_t>(battleData, sfv::resultflow::kBattleKind), sfv::resultflow::kKindSkippable);
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
	static const std::pair<int, int> kGauges[] = {{practice::kVitalParam, practice::kVitalRefill},
		{practice::kExtraParam, practice::kExtraRefill}, {practice::kVGaugeParam, practice::kVGaugeRefill}};
	if (!SetGaugeParams(kGauges, std::size(kGauges), "Practice gauges")) return false;
	Log("adapter: Practice gauges set for both players (vital %d, EX %d, V-gauge %d)", practice::kVitalRefill,
		practice::kExtraRefill, practice::kVGaugeRefill);
	return true;
}

bool SfvAdapter::SetStartFullExtra()
{
	namespace practice = sfv::practice;
	static const std::pair<int, int> kGauges[] = {{practice::kExtraParam, practice::kExtraFullAtRoundStart}};
	if (!SetGaugeParams(kGauges, std::size(kGauges), "EX gauge full at each round's start")) return false;
	Log("adapter: the EX gauge starts full at every round's start for both players (param %d = %d)", practice::kExtraParam,
		practice::kExtraFullAtRoundStart);
	return true;
}

bool SfvAdapter::SetGaugeParams(const std::pair<int, int>* gauges, size_t count, const char* what)
{
	namespace practice = sfv::practice;
	using SetParamFn = bool (*)(uint8_t* table, int param, int value);
	if (!bound_ || !battleData_) return Log("adapter: no %s: the battle is not bound", what), false;
	const auto set = reinterpret_cast<SetParamFn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
		sfv::kSetPlayerParam.rva);
	bool sides[kPlayers] = {};
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, (uint32_t)index);
		const int side = chara ? Field<int32_t>(chara, practice::kCharaSide) : -1;
		if (side < 0 || side >= kPlayers || sides[side]) {
			return Log("adapter: no %s: fighter %d has side %d", what, index, side), false;
		}
		sides[side] = true;
		// The battle data is the adapter's (const for the rest of it): its
		// tables are what Training's menu writes too.
		uint8_t* table = const_cast<uint8_t*>(battleData_) + practice::kPlayerParams + practice::kPlayerParamsStride * side;
		for (size_t k = 0; k < count; k++) {
			const auto& [param, value] = gauges[k];
			if (!set(table, param, value) || !set(chara + practice::kCharaParams, param, value)) {
				return Log("adapter: the game refused gauge %d = %d (%s) for side %d", param, value, what, side), false;
			}
		}
	}
	return true;
}

uint8_t* SfvAdapter::FindUnit(uint32_t typeRva) const
{
	return GameFindUnit(rollbackUnit_, typeRva);
}

std::optional<bool> SfvAdapter::FacingRight(int side) const
{
	if (!rollbackUnit_ || side < 0 || side >= kPlayers) return std::nullopt;
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, (uint32_t)index);
		int32_t charaSide = -1;
		if (!chara || !TryRead(chara + sfv::fighter::kSide, &charaSide) || charaSide != side) continue;
		const uint8_t* part = GameFindPart(chara, sfv::fighter::kActionPartType);
		int32_t mirrored = 0;
		if (!part || !TryRead(part + sfv::fighter::kMirrored, &mirrored)) return std::nullopt;
		return mirrored != 1;
	}
	return std::nullopt;
}

void SfvAdapter::FighterVisualParts(uint8_t* parts[kPlayers]) const
{
	for (int index = 0; index < kPlayers; index++) {
		uint8_t* chara = rollbackUnit_ ? GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, (uint32_t)index) : nullptr;
		parts[index] = chara ? GameFindPart(chara, sfv::visual::kPartType) : nullptr;
	}
}

void SfvAdapter::NoteCommandQueues()
{
	namespace f = sfv::fighter;
	for (int i = 0; i < fighterUnitCount_; i++) {
		const uint8_t* action = fighterUnits_[i].action;
		uintptr_t begin = 0, end = 0;
		if (!action || !TryRead(action + f::kCommandQueueBegin, &begin) || !TryRead(action + f::kCommandQueueEnd, &end) || end <= begin) {
			continue;
		}
		queuesCarried_++;
		if (queueLines_++ < kMostQueueLines) {
			Log("actions: frame %d%s: CharaUnit[%d]'s command queue holds %llu command(s) after the update (outside the snapshot: a "
				"restore would keep them)",
				inputUnit_ ? Field<int32_t>(inputUnit_, sfv::inputunit::kInputFrame) : -1, replaying_ ? " replayed" : "", i,
				(unsigned long long)((end - begin) / sizeof(void*)));
		}
	}
}

void SfvAdapter::KeyFighterUnits(SocketUpdate* sockets) const
{
	namespace f = sfv::fighter;
	const TinySides tiny = TinyFightersNow();
	sockets->unitCount = 0;
	for (int i = 0; i < fighterUnitCount_ && i < kMostFighterUnits; i++) {
		const FighterUnit& unit = fighterUnits_[i];
		UnitKeys& keys = sockets->units[sockets->unitCount++];
		keys.chara = unit.chara;
		keys.unit = i;
		keys.side = unit.side;
		// A unit with no animation state has nothing that decides its pose:
		// its reads stay unkeyed (pose 0) and keep the live value.
		if (!unit.motion) continue;
		// The game's snapshot holds all of it (sfv::fighter::kMotionRun...), so
		// after a restore it is the restored state's; read in place, on the
		// game thread before the update's tasks start.
		const socket_reads::Run motion[2] = {
			{unit.motion + f::kMotionRun, f::kMotionRunSize}, {unit.motion + f::kMotionRun2, f::kMotionRun2Size}};
		int32_t facing = -1;
		if (unit.action && !TryRead(unit.action + f::kMirrored, &facing)) facing = -1;
		const bool small = unit.side >= 0 && unit.side < kPlayers && tiny.side[unit.side];
		keys.pose = socket_reads::MakePoseKey(motion, facing, small);
		keys.root = socket_reads::MakeRootKey({unit.chara + f::kUnitRun, f::kUnitRunSize}, {unit.chara + f::kPlaceRun, f::kPlaceRunSize});
	}
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
		if (padSource_ == input::PadSource::Neutral && (values[p] & ~neutralised_[p])) {
			neutralised_[p] |= values[p];
			Log("adapter: pad %d held %08x while the battle loaded; recorded as %s", p, values[p],
				loadHeld_[p] ? "the launch's costume code" : "nothing pressed");
		}
		values[p] = input::RecordedInput(padSource_, values[p], pending_.p[p], loadHeld_[p]);
	}
}

PackedInput RecordedInputBits(const uint8_t* inputUnit, int pad)
{
	namespace iu = sfv::inputunit;
	PackedInput bits = 0;
	const uint8_t* ring = inputUnit + iu::kRings + iu::kRingStride * (uint32_t)pad;
	for (uint32_t slot = 0; slot < iu::kRingSlots; slot++) {
		bits |= *reinterpret_cast<const uint32_t*>(ring + iu::kSlotSize * slot + iu::kSlotValue);
	}
	return bits;
}

PackedInput SfvAdapter::RecordedInputs(int player) const
{
	return inputUnit_ ? RecordedInputBits(inputUnit_, player) : 0;
}

bool SfvAdapter::Fighting() const
{
	return flow_.Announced() == sfv::flow::kFight;
}

bool SfvAdapter::MatchDecided() const
{
	return battleData_ && Field<int32_t>(battleData_, sfv::battledata::kDecided) != 0;
}

void SfvAdapter::SeeFlowState(int state)
{
	const int before = flow_.Announced();
	flow_.Announce(state);
	if (state == before || !MatchDecided()) return;
	// Which states the decided match's scenes pass through, and whether the
	// battle still ticks there (docs/design/beta8/skip.md).
	Log("flow: state %d -> %d with the match decided (%u input tick(s) so far in this update%s)", before, state,
		ticksThisUpdate_, resimulating_ ? ", a frame replayed after a rollback" : "");
}

bool SfvAdapter::LocalPadsAny(uint32_t mask) const
{
	uint8_t* flow = flow_.Unit();
	return bound_ && flow && GameLocalPadsAny(flow + sfv::unit::kContext, mask);
}

void SfvAdapter::EndScenes()
{
	namespace rf = sfv::resultflow;
	using intro_skip::ScenesAction;
	uint8_t* flow = flow_.Unit();
	uint8_t* state = flow ? Field<GameSharedPtr>(flow, sfv::flowunit::kCurrent).object : nullptr;
	if (!state || !flowRequest_ || !setFlowPhase_ || !markResultSkipped_) return;
	const int32_t id = Field<int32_t>(state, rf::kStateId);
	const uint32_t vtable = (uint32_t)(Field<uintptr_t>(state, 0) - imageBase_);
	const int32_t phase = Field<int32_t>(state, sfv::flowstate::kPhase);
	// The KO's slow motion (battle frame state word, bit 0x20) holds Finish: a
	// round that ends in a KO is not cut while the game still plays it
	// (docs/design/beta9/skip.md section 2). The word is in the rollback state,
	// so both copies read the same one on the same frame.
	const uint32_t stateWord = battleFrame_ ? Field<uint32_t>(battleFrame_, sfv::battleframe::kState) : 0;
	const bool koStop = battleFrame_ && (stateWord & intro_skip::kKoStopBit) != 0;
	const intro_skip::ScenesStep step = intro_skip::ScenesStepFor(id, vtable, phase, koStop);
	// Finish's last phase asks for the state the constructor gave it; the
	// skip asks for that one only when it is RoundOver, as the versus flow's.
	const bool unexpectedNext = step.action == ScenesAction::RequestNext && Field<int32_t>(state, rf::kNextState) != rf::kRoundOver;
	if (step.action == ScenesAction::None || unexpectedNext) {
		// Logged once per state and phase: the skip frames come every 15.
		if (id != scenesIdleState_ || phase != scenesIdlePhase_) {
			scenesIdleState_ = id;
			scenesIdlePhase_ = phase;
			if (unexpectedNext) {
				Log("scene skip: Finish asks for flow state %d next, not RoundOver (%d); it plays on",
					Field<int32_t>(state, rf::kNextState), rf::kRoundOver);
			} else {
				// MatchResultDemo's phase 6 waits for the results screens, whose
				// pages hear the bit as a press (intro_skip::PadsHear).
				Log("scene skip: the skip bit in flow state %d (class rva 0x%X) phase %d moves no phase here; the "
					"game's own questions for a press hear it; battle frame state word %08x", id, vtable, phase, stateWord);
			}
		}
		return;
	}
	scenesIdleState_ = scenesIdlePhase_ = -1;
	bool done = false;
	switch (step.action) {
	case ScenesAction::RequestNext:
		// What Finish's last phase does (rva 0x40EBE0): the flow switches to
		// RoundOver on this frame's update, which asks for the demo on its
		// first tick.
		done = flowRequest_(flow, Field<int32_t>(state, rf::kNextState), (uint8_t)Field<int32_t>(state, rf::kNextFlags),
				   Field<int32_t>(state, sfv::flowstate::kArg)) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_), "Finish ends in its phase %d: the flow %s RoundOver", phase,
			done ? "asked for" : "refused");
		// The cut skips Finish's phase 12, whose last act is rva 0x3074D0
		// (fighters, -1): each fighter unit's dword +2376 set to -1 (skip.md,
		// "Settled in IDA"). What `fighters` is (the state's list at
		// +272..+280) was not read to its element type, so the writes are
		// made through the field helpers to the two fighters' units. A
		// confirmed frame on every copy, in the rollback state.
		if (done) {
			for (int i = 0; i < fighterUnitCount_ && i < kPlayers; i++) {
				if (fighterUnits_[i].chara) Field<int32_t>(fighterUnits_[i].chara, sfv::fighter::kFinishMask) = sfv::fighter::kFinishMaskValue;
			}
		}
		break;
	case ScenesAction::ResultSkip: {
		// What phase 3 (rva 0x409930) does on the skip bit when its gate is
		// open, here whatever the gate says: the skip's mark, skipped, phase 4,
		// which runs on at once to the results' event and the wait for them.
		const int32_t gate = Field<int32_t>(state, rf::kResultSkipGate);
		markResultSkipped_(flow + sfv::unit::kContext);
		Field<int32_t>(state, rf::kResultSkipped) = 1;
		done = setFlowPhase_(state, step.phase) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_),
			"the winner's demo skipped from phase %d to %d (its own gate %d)%s, now in phase %d", phase, step.phase, gate,
			done ? "" : ": the phase was refused", Field<int32_t>(state, sfv::flowstate::kPhase));
		break;
	}
	case ScenesAction::SetPhase:
		done = setFlowPhase_(state, step.phase) != 0;
		snprintf(scenesSkipText_, sizeof(scenesSkipText_), "flow state %d moved from phase %d to %d%s, now in phase %d",
			id, phase, step.phase, done ? "" : ": the phase was refused", Field<int32_t>(state, sfv::flowstate::kPhase));
		break;
	case ScenesAction::None: break;
	}
	Log("scene skip: %s, battle frame state word %08x", scenesSkipText_, stateWord);
	scenesSkip_ = scenesSkipText_;
}

void SfvAdapter::NoteDecidedTicks()
{
	if (!MatchDecided()) {
		decidedTicks_ = -1;
		return;
	}
	if (decidedTicks_ == (int64_t)ticksInLastUpdate_) return;
	Log("adapter: %u input tick(s) in this update with the match decided (flow state %d; before, %lld)", ticksInLastUpdate_,
		flow_.Announced(), (long long)decidedTicks_);
	decidedTicks_ = ticksInLastUpdate_;
}

void SfvAdapter::RunUpdate()
{
	ticksThisUpdate_ = 0;
	// The fight reads the fighters' models (sfv::kSocketWorldPosition), so it
	// sees a small side at the game's own scale, and the picture is made
	// small again after it (TinyFighters).
	uint8_t* visualParts[kPlayers] = {};
	if (TinyFightersNow().Any()) FighterVisualParts(visualParts);
	BeginFightUpdate(visualParts);
	// The fight's socket reads in this update: whether the models show the
	// state it starts from and stand where it puts them, and each fighter
	// unit's keys now.
	SocketUpdate sockets;
	sockets.inputFrame = inputUnit_ ? Field<int32_t>(inputUnit_, sfv::inputunit::kInputFrame) : -1;
	sockets.replaying = replaying_;
	sockets.fresh = poseFresh_;
	sockets.placed = modelsPlaced_;
	if (bound_) {
		// The poses the engine evaluates next are at LOD 0 whatever the window
		// (SocketReads): the same bones on every copy, partners' too.
		uint8_t* visual[kMostFighterUnits] = {};
		for (int i = 0; i < fighterUnitCount_; i++) visual[i] = fighterUnits_[i].visual;
		KeepFighterMeshesAtLodZero(visual, fighterUnitCount_);
		LARGE_INTEGER from{}, to{};
		QueryPerformanceCounter(&from);
		KeyFighterUnits(&sockets);
		QueryPerformanceCounter(&to);
		keyTicks_ += to.QuadPart - from.QuadPart;
		keyedUpdates_++;
	}
	NoteSocketUpdate(sockets);
	RngAuditUpdate(bound_, Fighting());
	original_(battleFrame_);
	RngAuditUpdate(false, false);
	poseFresh_ = false; // the state moved on; the models show it after the engine's next frame
	// The update's last phase ran the visual task (rva 0x3A06C0), which gave
	// every actor its position and layers for the state the update leaves.
	modelsPlaced_ = true;
	if (bound_) NoteCommandQueues();
	EndFightUpdate(visualParts);
	if (bound_ && keyedUpdates_ % kSocketSummaryUpdates == 0) {
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		const double us = frequency.QuadPart ? 1e6 * (double)keyTicks_ / (double)frequency.QuadPart / (double)keyedUpdates_ : 0.0;
		Log("sockets: %llu updates keyed in this battle, %.1f us per update to key %d fighter unit(s); %s",
			(unsigned long long)keyedUpdates_, us, fighterUnitCount_, SocketReadsSummary().c_str());
	}
	ticksInLastUpdate_ = ticksThisUpdate_;
	flow_.Capture(); // the record mirrors the game whenever it is saved or hashed
}

void SfvAdapter::RunOriginal(bool neutralPads, const std::array<PackedInput, kPlayers>* held)
{
	padSource_ = neutralPads ? input::PadSource::Neutral : input::PadSource::Pads;
	for (int p = 0; p < kPlayers; p++) loadHeld_[p] = neutralPads && held ? (*held)[p] : 0;
	RunUpdate();
	padSource_ = input::PadSource::Pads;
	for (PackedInput& bits : loadHeld_) bits = 0;
}

void SfvAdapter::PresetHeldAtLoad(const std::array<PackedInput, kPlayers>& codes)
{
	if (!inputUnit_) return;
	namespace iu = sfv::inputunit;
	auto* held = reinterpret_cast<uint32_t*>(inputUnit_ + iu::kHeldAtLoad);
	uint32_t& taken = *reinterpret_cast<uint32_t*>(inputUnit_ + iu::kHeldAtLoadTaken);
	const bool wasTaken = taken != 0;
	const uint32_t before[kPlayers] = {held[0], held[1]};
	for (int p = 0; p < kPlayers; p++) held[p] = codes[p];
	taken = 1;
	Log("adapter: costume codes held at load P1 %08x, P2 %08x (the game's snapshot %s: %08x, %08x)", codes[0], codes[1],
		wasTaken ? "was taken" : "was not taken yet", before[0], before[1]);
}

void SfvAdapter::Advance(const FrameInputs& inputs, bool resimulating)
{
	// One GGPO frame is one update: the game's own replay mechanism (extra
	// ticks) is not used, rollbacks call Advance once per frame instead.
	Field<uint32_t>(battleFrame_, sfv::battleframe::kExtraTicks) = 0;
	pending_ = inputs;
	resimulating_ = resimulating;
	padSource_ = input::PadSource::Session;
	replaying_ = resimulating;
	// The skip bit reaches the game only on the skip frames of the intro and
	// of a decided match's scenes, which every session path steps confirmed
	// and gives alike (GivenInputs; a fight frame never carries it). In the
	// scenes the game does not end anything on it, so the frame does, from
	// the state at its start (IntroSkip.hxx), at the match's end and between
	// two rounds. The phases it runs hear the session's inputs too, should
	// they ask the pads.
	if (InjectedAny(intro_skip::kSkipBit) && intro_skip::Scenes(FlowState(), MatchDecided())) EndScenes();
	const int64_t from = Ticks();
	RunUpdate();
	const int64_t took = Ticks() - from;
	replaying_ = false;
	padSource_ = input::PadSource::Pads;
	resimulating_ = false;
	NoteDecidedTicks();
	NotePause();
	if (resimulating) {
		costs_.replayed++;
		costs_.replayedTicks += took;
		return;
	}
	costs_.ticks++;
	costs_.tickTicks += took;
	if (costs_.ticks >= kCostAdvances) LogCosts();
}

void SfvAdapter::NotePause()
{
	// SFV's own pause never runs in a session's battle (GameHooks keeps the
	// PauseUnit's task from it, sfv::pause); a pause bit here would be one
	// copy's alone. Logged on each change.
	// The mask takes the KO's bits too (0x30: the KO's slow motion, 0x20, and
	// its neighbour) so a round's ending shows in the log (skip.md section 2).
	const uint32_t bits = Field<uint32_t>(battleFrame_, sfv::battleframe::kState) & (sfv::pause::kBits | 0x30);
	if (bits == pausedBits_) return;
	Log("adapter: the game's pause bits are %x in a session's battle (were %x; flow state %d)", bits, pausedBits_,
		flow_.Announced());
	pausedBits_ = bits;
}

void SfvAdapter::LogCosts()
{
	const StateLayout::Costs layout = layout_.TakeCosts();
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	// Average microseconds per call, and the calls.
	const auto us = [&](int64_t ticks, uint64_t calls) {
		return calls && frequency.QuadPart ? 1e6 * (double)ticks / (double)frequency.QuadPart / (double)calls : 0.0;
	};
	// The session's counts (docs/design/beta8/perf.md): saves that kept no
	// copy, and of the hashes, those made once a frame was final; saves a
	// rollback replaced before they were final, whose hash was never needed.
	Log("costs: the last %llu updates: tick %.0f us; replayed tick %.0f us (%llu); save %.0f us (%llu, %llu skipped), "
		"load %.0f us (%llu), hash %.0f us (%llu, %llu once final, %llu not needed), the restore's callbacks %.0f us (%llu)",
		(unsigned long long)costs_.ticks, us(costs_.tickTicks, costs_.ticks), us(costs_.replayedTicks, costs_.replayed),
		(unsigned long long)costs_.replayed, us(layout.saveTicks, layout.saves), (unsigned long long)layout.saves,
		(unsigned long long)layout.skippedSaves, us(layout.loadTicks, layout.loads), (unsigned long long)layout.loads,
		us(layout.hashTicks, layout.hashes), (unsigned long long)layout.hashes, (unsigned long long)layout.confirmedHashes,
		(unsigned long long)layout.droppedHashes, us(costs_.callbackTicks, costs_.callbacks),
		(unsigned long long)costs_.callbacks);
	costs_ = {};
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
	const int64_t from = Ticks();
	// A restored state: the models show another one, and stand where another
	// one put them, until an update's visual task places them again (SocketReads).
	poseFresh_ = false;
	modelsPlaced_ = false;
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
	costs_.callbacks++;
	costs_.callbackTicks += Ticks() - from;
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
