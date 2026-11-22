#include "mod/SfvAdapter.hxx"

#include "mod/FighterData.hxx"

#include "mod/GameImage.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/Pace.hxx"
#include "mod/PracticeGauges.hxx"
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

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <iterator>

namespace sfve::mod {

namespace {

// How often the socket reads' counts are logged.
constexpr uint64_t kSocketSummaryUpdates = 1800;

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
	updateCosts_.FrameEnded(); // the frame before has ended
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
	regionTable_.Forget();
	gameRegions_ = 0;
	postRestore_.clear();
	inputUnit_ = nullptr;
	rollbackUnit_ = nullptr;
	waitingFor_ = nullptr;
	for (PackedInput& bits : neutralised_) bits = 0;
	flow_.Forget();
	// The socket reads' counts are the battle's own (SocketReads): logged as
	// it ends, then forgotten.
	if (fighterUnitCount_ > 0) Log("sockets: the battle ends: %s", SocketReadsSummary().c_str());
	updateCosts_.EndBattle();
	notes_.Forget();
	if (const std::string ticks = tickCounts_.Summary(); !ticks.empty()) Log("ticks: the battle ends: %s", ticks.c_str());
	tickCounts_.Forget();
	for (FighterUnit& unit : fighterUnits_) unit = {};
	fighterUnitCount_ = 0;
	battleFacts_ = {};
	ForgetSocketReads();
	placedUpdates_ = 0;
	RngAuditForget();
	layout_.TakeCosts(); // the next battle's are its own
	scenes_.Forget();
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
	const uintptr_t base = GameBase();
	OutsideState outside = {reinterpret_cast<uintptr_t>(ruleUnit), reinterpret_cast<uintptr_t>(battleData),
		reinterpret_cast<uintptr_t>(battleFrame_), reinterpret_cast<uintptr_t>(inputUnit_), base,
		reinterpret_cast<uintptr_t>(FindUnit(sfv::kowait::kUnitType))};
	if (!outside.koWait) Log("adapter: this battle has no round's end wait unit (sfv::kowait); its state is not kept");
	// Every fighter unit, the two fighters first and then any partners (Akira's,
	// sfv::unit::kMostCharaUnits), as the battle added them and as RegionNames
	// numbers their regions: their parts' state outside the snapshot, and
	// where their anchored socket reads start.
	FighterUnit units[kMostFighterUnits];
	int unitCount = 0;
	for (uint32_t index = 0; index < sfv::unit::kMostCharaUnits; index++) {
		uint8_t* chara = GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, index);
		if (!chara) break;
		FighterUnit& unit = units[unitCount++];
		unit.chara = chara;
		if (!TryRead(chara + sfv::fighter::kSide, &unit.side) || unit.side < 0 || unit.side >= kPlayers) unit.side = -1;
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
	scenes_.Bind(base);
	if (!flow_.Bind(flowUnit, base)) {
		WaitingToBind("a flow it can record");
		return false;
	}
	layout_.Clear();
	layout_.SetAddressTest(LooksLikeAddress);
	uint8_t* manager = GameGetManager(rollbackUnit_);
	if (!AddGameRegions(layout_, regionNames_, rollbackUnit_, manager) || !regionTable_.Bind(manager)) {
		layout_.Clear();
		flow_.Forget();
		WaitingToBind("a usable region table");
		return false;
	}
	gameRegions_ = layout_.Regions().size();
	{
		// Which words the hashes read as addresses (mod/AddressRanges.hxx):
		// where the game's own heap lies decides the rule, from its battle
		// objects and its rollback regions (all the game allocator's), before
		// anything is hashed.
		std::vector<uint64_t> gameHeap = {reinterpret_cast<uint64_t>(battleFrame_), reinterpret_cast<uint64_t>(inputUnit_),
			reinterpret_cast<uint64_t>(rollbackUnit_), reinterpret_cast<uint64_t>(flowUnit), reinterpret_cast<uint64_t>(ruleUnit),
			reinterpret_cast<uint64_t>(battleData)};
		for (const StateRegion& r : layout_.Regions()) gameHeap.push_back(reinterpret_cast<uint64_t>(r.ptr));
		ChooseAddressRule(gameHeap);
	}
	CollectPostRestore(manager, postRestore_);
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
				i >= kPlayers ? " partner" : "", regions, units[i].action ? "" : ", no action part: its anchors unresolved");
			text += one;
		}
		Log("adapter: anchored socket reads start from each fighter unit's fight position: %s", text.c_str());
		if (GameFindUnit(rollbackUnit_, sfv::unit::kCharaUnitType, sfv::unit::kMostCharaUnits)) {
			Log("adapter: more than %u fighter units; the ones past them are not kept at LOD 0, anchored or given their outside runs",
				sfv::unit::kMostCharaUnits);
		}
	}
	RngAuditBind(layout_, base);
	// What each fighter unit computes with, which the copies of a room's
	// match compare before frame 0 (core/MatchData.hxx).
	battleFacts_ = {};
	for (int i = 0; i < unitCount; i++) battleFacts_.fighters.push_back(ReadFighterData(units[i].action, units[i].side));
	Log("adapter: the fighters' game data: %s", match_data::Text(battleFacts_).c_str());
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
	return practice_gauges::SetPractice(rollbackUnit_, bound_ ? battleData_ : nullptr);
}

bool SfvAdapter::SetStartFullExtra()
{
	return practice_gauges::SetStartFullExtra(rollbackUnit_, bound_ ? battleData_ : nullptr);
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

void SfvAdapter::PlaceFighterUnits(SocketUpdate* sockets) const
{
	namespace f = sfv::fighter;
	sockets->unitCount = 0;
	for (int i = 0; i < fighterUnitCount_ && i < kMostFighterUnits; i++) {
		const FighterUnit& unit = fighterUnits_[i];
		UnitPlace& place = sockets->units[sockets->unitCount++];
		place.chara = unit.chara;
		place.unit = i;
		place.side = unit.side;
		// Where the unit stands and which way it faces as the update begins:
		// what an anchored read is given (socket_reads::AnchorPosition). Both
		// are in the game's snapshot, so the same on every copy at this point;
		// a read in the update's middle could see the unit's movement task
		// before or after it ran.
		int32_t mirrored = 0;
		std::array<float, 4> position{};
		if (unit.chara && unit.action && TryRead(unit.chara + f::kFightPosition, &position) &&
			TryRead(unit.action + f::kMirrored, &mirrored) && std::isfinite(position[0]) && std::isfinite(position[1])) {
			std::copy(position.begin(), position.end(), place.position);
			place.facingLeft = mirrored == 1;
			place.anchorable = true;
		}
	}
}

namespace {
std::atomic<TrainingPadsHook> s_trainingPads{nullptr};
} // namespace

void SetTrainingPadsHook(TrainingPadsHook hook)
{
	s_trainingPads = hook;
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
	// A battle the pads play (never a session's or a replay's, nor one still
	// loading for them): the Training lab may put its input in.
	if (padSource_ == input::PadSource::Pads)
		if (const TrainingPadsHook hook = s_trainingPads.load()) hook(*this, values, kPlayers);
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
	scenes_.Apply(flow_.Unit(), battleFrame_, fighterUnits_, fighterUnitCount_);
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
	// The fight's socket reads in this update: where each fighter unit stands
	// and faces as it begins (SocketReads' anchors).
	SocketUpdate sockets;
	sockets.inputFrame = inputUnit_ ? Field<int32_t>(inputUnit_, sfv::inputunit::kInputFrame) : -1;
	sockets.replaying = replaying_;
	if (bound_) {
		// The poses the engine evaluates next are at LOD 0 whatever the window
		// (SocketReads): the same picture on every copy, partners' too.
		uint8_t* visual[kMostFighterUnits] = {};
		for (int i = 0; i < fighterUnitCount_; i++) visual[i] = fighterUnits_[i].visual;
		KeepFighterMeshesAtLodZero(visual, fighterUnitCount_);
		PlaceFighterUnits(&sockets);
		placedUpdates_++;
	}
	NoteSocketUpdate(sockets);
	RngAuditUpdate(bound_, Fighting());
	original_(battleFrame_);
	RngAuditUpdate(false, false);
	if (bound_) {
		notes_.CommandQueues(fighterUnits_, fighterUnitCount_,
			inputUnit_ ? Field<int32_t>(inputUnit_, sfv::inputunit::kInputFrame) : -1, replaying_);
	}
	EndFightUpdate(visualParts);
	if (bound_ && placedUpdates_ % kSocketSummaryUpdates == 0) {
		Log("sockets: %llu updates in this battle, %d fighter unit(s); %s", (unsigned long long)placedUpdates_, fighterUnitCount_,
			SocketReadsSummary().c_str());
	}
	if (bound_) {
		const int32_t frame = inputUnit_ ? Field<int32_t>(inputUnit_, sfv::inputunit::kInputFrame) : -1;
		if (const std::string line = tickCounts_.Note(frame, ticksThisUpdate_, replaying_); !line.empty()) Log("%s", line.c_str());
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

void SfvAdapter::ZeroBattleFrameCounts()
{
	namespace bf = sfv::battleframe;
	if (!battleFrame_) return;
	uint32_t& updates = Field<uint32_t>(battleFrame_, bf::kUpdates);
	uint32_t& ticked = Field<uint32_t>(battleFrame_, bf::kTickedUpdates);
	float& clock = Field<float>(battleFrame_, bf::kClock);
	Log("adapter: the battle frame counted %u updates, %u that ticked, clock %.4f before frame 0; 0 on every copy", updates, ticked,
		(double)clock);
	updates = ticked = 0;
	clock = 0.0f;
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
	const int64_t from = UpdateCosts::Now();
	updateCosts_.BeforeUpdate(resimulating);
	RunUpdate();
	const int64_t took = UpdateCosts::Now() - from;
	replaying_ = false;
	padSource_ = input::PadSource::Pads;
	resimulating_ = false;
	notes_.DecidedTicks(MatchDecided(), ticksInLastUpdate_, flow_.Announced());
	notes_.Pause(battleFrame_, flow_.Announced());
	updateCosts_.Played(took, resimulating, layout_);
}

int SfvAdapter::ReseedRandom(uint32_t seed)
{
	const uintptr_t base = GameBase();
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
	const int64_t from = UpdateCosts::Now();
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
	updateCosts_.Callbacks(UpdateCosts::Now() - from);
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
