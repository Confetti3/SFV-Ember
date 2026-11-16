#pragma once

// GameAdapter over Street Fighter V's battle.
//
// Layout: every region of the game's own rollback manager (what its online
// rollback saves), plus the state its snapshot leaves out: the battle frame's
// counters and the input unit's frame counters (its netcode manages them),
// and what a round's end changes (the rule unit's judgement, the match
// data's round results and flow flags, the round flow as a FlowRollback
// record, which mirrors the game after every update and every load). Pointer
// words are masked as the synctest finds them.
//
// Advance runs exactly one original battle-frame update with the extra-tick
// count forced to zero, so one call is one GGPO frame, and both players'
// inputs are written over the pads' values when the tick records them.
// Rollbacks replay by calling Advance again, exactly like a forward frame.
//
// The adapter is the game's bind, save/load and advance surface; what it
// does besides is composed from modules of its own: the regions the game's
// rollback manager lists (GameRegions), Training's gauges (PracticeGauges),
// ending the decided match's scenes on a skip frame (ScenesSkip), the cost
// and pace lines (UpdateCosts) and the per-update diagnostics (UpdateNotes).

#include "core/GameAdapter.hxx"
#include "core/MatchData.hxx"
#include "mod/BattleIdentity.hxx"
#include "mod/FighterUnits.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameRegions.hxx"
#include "mod/IntroSkip.hxx"
#include "mod/Pace.hxx"
#include "mod/RegionTable.hxx"
#include "mod/ScenesSkip.hxx"
#include "mod/SfvFlow.hxx"
#include "mod/SfvRegionNames.hxx"
#include "mod/SocketReads.hxx"
#include "mod/TickCounts.hxx"
#include "mod/UpdateCosts.hxx"
#include "mod/UpdateNotes.hxx"

#include <stdint.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfve::mod {

class SfvAdapter;
// What a battle of the player's own records for each slot this tick, as the
// pads left it (input::PadSource::Pads): the Training lab's one place to put
// its input in (SfvTrainingLab), which it does only in the Training battle.
// One hook; null for none. Game thread, inside InputRecord.
using TrainingPadsHook = void (*)(SfvAdapter& adapter, uint32_t* values, int slots);
void SetTrainingPadsHook(TrainingPadsHook hook);

// The bits recorded in any slot of a pad's input record ring
// (sfv::inputunit::kRings): 0 when the battle has recorded nothing pressed
// for that pad since its input unit was made.
PackedInput RecordedInputBits(const uint8_t* inputUnit, int pad);

class SfvAdapter : public GameAdapter {
public:
	// The battle objects as the hooks see them. A battle is its frame's
	// construction, not its address (BattleIdentity): the next battle's frame
	// can lie where the last one's did. A new battle forgets the last one's
	// layout, which is rebuilt on the next Bind.
	void SeeBattleFrameConstructed(uint8_t* battleFrame) { identity_.Constructed(battleFrame); }
	void SeeBattleFrame(uint8_t* battleFrame, BattleFrameFn original);
	// The battle the frames belong to, numbered from 1 (0 before any): what
	// tells one battle from the next.
	uint64_t Battle() const { return identity_.Battle(); }
	void SeeRollbackUnit(uint8_t* rollbackUnit) { rollbackUnit_ = rollbackUnit; }
	// A unit of this battle, which finds the others (GameFindUnit); null
	// before the rollback unit's first tick.
	uint8_t* AnyUnit() const { return rollbackUnit_; }

	// Builds the layout once this battle's objects are known.
	bool Bind();
	bool Bound() const { return bound_; }
	uint8_t* BattleFrame() const { return battleFrame_; }
	// The names of the rollback regions' owners and their objects, as of the
	// bind (game thread only: the desync report copies Objects() there).
	const RegionNames& Names() const { return regionNames_; }
	// The battle's match data (rounds, results); null until bound.
	const uint8_t* BattleData() const { return battleData_; }
	// The same, for the one writer of Training's dummy control (SfvTrainingLab,
	// sfv::dummy), which writes it only in the Training battle.
	uint8_t* TrainingBattleData() const { return battleData_; }
	// The fighter of `side` (0 P1, 1 P2) faces right: the fight does not mirror
	// its x (sfv::fighter::kMirrored). None while the battle's fighters cannot
	// be found or read.
	std::optional<bool> FacingRight(int side) const;
	// A Practice room's game (sfv::practice): both players' vital, EX and
	// V-gauges refill and nobody is knocked out, set as Training's menu sets
	// them, in the battle data's tables and each fighter's copy. Once bound,
	// before the session's first frame. False (logged) when a table or a
	// fighter is missing or the game refused a value (mod/PracticeGauges.hxx).
	bool SetPracticeGauges();
	// Only the EX (Critical Art) gauge, full at every round's start and
	// otherwise as in a match (sfv::practice::kExtraFullAtRoundStart); vital
	// and the V-gauge stay a match's. The same once-before-frame-0 write and
	// the same results as SetPracticeGauges.
	bool SetStartFullExtra();

	// From the input-record hook: remembers what the game computed for each
	// pad this tick, then writes the injected inputs over it while Advance runs.
	void OnInputRecord(uint8_t* inputUnit);
	PackedInput Sampled(int player) const { return sampled_[player]; }
	// The update now running takes Advance's inputs in place of the pads'.
	bool Injecting() const { return padSource_ == input::PadSource::Session; }
	// Either player's injected input for this tick has a bit of `mask`; or,
	// with the match decided, the skip request answers a question for any
	// button (intro_skip::PadsHear: the results screens' pages turn on it).
	bool InjectedAny(uint32_t mask) const
	{
		return intro_skip::PadsHear(pending_.p[0] | pending_.p[1], mask, MatchDecided());
	}
	// Every bit the player's input record ring holds (RecordedInputBits);
	// 0 before the input unit is known.
	PackedInput RecordedInputs(int player) const;
	// True when the battle is running ticks (not paused, not between rounds
	// with ticks stopped): the last original update recorded at least one tick.
	uint32_t TicksInLastUpdate() const { return ticksInLastUpdate_; }
	// What each fighter unit of the bound battle computes with
	// (FighterData), read as it bound: what a room's copies compare before
	// frame 0, and what a replay records.
	const match_data::Facts& BattleFacts() const { return battleFacts_; }

	// The state the battle flow last announced (part of the rollback state);
	// each change with the match decided is logged.
	void SeeFlowState(int state);
	int FlowState() const { return flow_.Announced(); }
	bool Fighting() const;
	// The match is decided (sfv::battledata::kDecided, part of the rollback
	// state): the scenes after its last round are the ones a press skips.
	bool MatchDecided() const;
	// Whether a local pad holds a bit of `mask` (in the battle's input bits)
	// now, as the game asks its pads after the decision (sfv::kPadsAny,
	// unhooked): this copy's press in the decided match's scenes even if the
	// battle's input record stood still. False until bound. Game thread.
	bool LocalPadsAny(uint32_t mask) const;
	// What the last Advance did to end the decided match's scenes (a line
	// for the log, which the netplay mode gives its frame), taken once; null
	// when it did nothing.
	const char* TakeScenesSkip() { return scenes_.Take(); }
	// Only the fight reads the players' inputs (GameAdapter::InputsMatter),
	// and the intro and the decided match's scenes their skip request on
	// their skip frames (IntroSkip.hxx).
	bool InputsMatter() const override { return Fighting(); }
	PackedInput InputBits() const override { return input::kInputBits; }
	PackedInput ConfirmedBits(int frame) const override
	{
		return intro_skip::ConfirmedBits(FlowState(), frame, MatchDecided());
	}

	// The original update with no injection (outside any session). With
	// `neutralPads` the battle records both players' pads as nothing pressed,
	// as a session gives them outside the fight: a room's or a replay's battle
	// before its session, so the ticks it runs while it loads hold the same
	// inputs on every copy, not each PC's own pad as P1's (sfv::inputunit).
	// `held`, with neutralPads, is what each player holds instead of nothing:
	// the costume codes the launch gave both (VersusLaunch's LoadingCodes,
	// EMBER_LIBRARY.md section 55).
	void RunOriginal(bool neutralPads = false, const std::array<PackedInput, kPlayers>* held = nullptr);
	// As a room's or a replay's battle with costume codes binds: the input
	// unit's held-at-load snapshot (sfv::inputunit::kHeldAtLoad) holds the
	// codes and is marked taken, so every copy's fighters read the same
	// buttons and the game never takes it again from a session's frame.
	void PresetHeldAtLoad(const std::array<PackedInput, kPlayers>& codes);

	std::string BuildId() const override;
	StateLayout& Layout() override { return layout_; }
	const StateLayout& Layout() const { return layout_; }
	void Advance(const FrameInputs& inputs, bool resimulating) override;
	// What the game's own Restore does after copying the regions back, then
	// the flow record written back.
	void AfterLoad() override;
	// The flow record's latched failure (FlowRollback::Failure), or the game's
	// region table no longer the one the layout was built from (RegionTable).
	const char* StateFailure() const override
	{
		const char* flow = flow_.Failure();
		return flow ? flow : regionTable_.Changed();
	}

	std::string Describe() const; // JSON for the status report

	// Gives every random generator in the battle state (and the global one) a
	// seed derived from `seed` and the generator's place in its region, the
	// same in every process, and sets the match's start time (which the round
	// starts reseed the fighters' generators from) to `seed`. The game takes
	// both from the clock. Returns how many generators were reseeded.
	int ReseedRandom(uint32_t seed);
	// The battle frame's own counts (sfv::battleframe kUpdates,
	// kTickedUpdates, kClock), 0 on every copy at frame 0: they count the
	// updates each process ran before its battle bound, one or two more on
	// one copy, and the slow-motion gate reads the update count. Restored
	// with the state from then on, so the copies keep them equal.
	void ZeroBattleFrameCounts();
	// The match's start time (sfv::battledata::kStartTime), which lies outside
	// the rollback state: a match data reset takes it from the clock again.
	uint32_t StartTime() const;
	void SetStartTime(uint32_t value);
	// The game changed outside an update (the rematch's restart): the flow
	// record mirrors it again, so the next save holds what the game holds.
	void Recapture() { flow_.Capture(); }

	// The game's post-restore callbacks, run by its own Restore (logged at bind).
	using PostRestore = GamePostRestore;
	const std::vector<PostRestore>& PostRestoreCallbacks() const { return postRestore_; }

private:
	void Forget();
	uint8_t* FindUnit(uint32_t typeRva) const;
	void WaitingToBind(const char* what);
	void RunUpdate();
	// A frame whose inputs carry the skip bit with the match decided: the
	// current flow state moved on (ScenesSkip), before the frame's update.
	void EndScenes();
	// Both fighters' CharaVisualParts, in the battle's order (null where
	// there is none): TinyFighters' sides.
	void FighterVisualParts(uint8_t* parts[kPlayers]) const;

public:
	// What the pace line needs of the session (UpdateCosts::PaceCounts). The
	// netplay mode sets it for each session; null where the battle has none.
	using PaceCounts = UpdateCosts::PaceCounts;
	void SetPaceSource(std::function<PaceCounts()> source) { updateCosts_.SetPaceSource(std::move(source)); }
	// An update is due in the frame now running: the mode steps its session
	// (whether a frame is played or waits for the other player's inputs), so
	// the frame's time is the pace line's. A frame Advance plays forward is
	// due by itself; one the battle stands still in is not (pace::FrameClock).
	void NoteUpdateDue() { updateCosts_.Due(); }

private:

	BattleIdentity identity_;
	uint8_t* battleFrame_ = nullptr;
	uint8_t* battleData_ = nullptr;
	BattleFrameFn original_ = nullptr;
	uint8_t* inputUnit_ = nullptr;
	uint8_t* rollbackUnit_ = nullptr;
	bool bound_ = false;
	const char* waitingFor_ = nullptr; // what Bind last waited for (logged once)
	StateLayout layout_;
	RegionTable regionTable_; // the game's table the layout was built from
	size_t gameRegions_ = 0;
	RegionNames regionNames_;
	std::vector<PostRestore> postRestore_;

	input::PadSource padSource_ = input::PadSource::Pads;
	PackedInput neutralised_[kPlayers] = {}; // the pad bits RunOriginal(true) recorded as neutral this battle (logged once)
	PackedInput loadHeld_[kPlayers] = {};    // what RunOriginal(true) records instead (the costume codes; else 0)
	FrameInputs pending_;
	PackedInput sampled_[kPlayers] = {};
	uint32_t ticksInLastUpdate_ = 0;
	uint8_t* (*gameInstance_)() = nullptr; // sfv::kGameInstance
	uint32_t ticksThisUpdate_ = 0;
	bool replaying_ = false; // the update Advance runs replays a frame (SocketReads' log)
	// Where every fighter unit stands and faces as an update begins (SocketReads).
	void PlaceFighterUnits(SocketUpdate* sockets) const;
	match_data::Facts battleFacts_;
	TickCounts tickCounts_; // the bound battle's ticks per update
	// The battle's fighter units, partners included, in its order (from Bind,
	// FighterUnits.hxx).
	FighterUnit fighterUnits_[kMostFighterUnits];
	int fighterUnitCount_ = 0;
	uint64_t placedUpdates_ = 0; // this battle's updates whose fighter units were placed (the sockets line)
	FlowRollback flow_;
	UpdateCosts updateCosts_; // the cost and pace lines
	ScenesSkip scenes_;       // ending the decided match's scenes (EndScenes)
	UpdateNotes notes_;       // the per-update diagnostics
	bool resimulating_ = false; // the update now running replays a frame after a rollback
};

} // namespace sfve::mod
