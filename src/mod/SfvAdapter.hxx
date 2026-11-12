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

#include "core/GameAdapter.hxx"
#include "mod/BattleIdentity.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/IntroSkip.hxx"
#include "mod/SfvFlow.hxx"
#include "mod/SfvRegionNames.hxx"

#include <stdint.h>

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sfve::mod {

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
	// The battle's match data (rounds, results); null until bound.
	const uint8_t* BattleData() const { return battleData_; }
	// The fighter of `side` (0 P1, 1 P2) faces right: the fight does not mirror
	// its x (sfv::fighter::kMirrored). None while the battle's fighters cannot
	// be found or read.
	std::optional<bool> FacingRight(int side) const;
	// A Practice room's game (sfv::practice): both players' vital, EX and
	// V-gauges refill and nobody is knocked out, set as Training's menu sets
	// them, in the battle data's tables and each fighter's copy. Once bound,
	// before the session's first frame. False (logged) when a table or a
	// fighter is missing or the game refused a value.
	bool SetPracticeGauges();

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
	const char* TakeScenesSkip() { return std::exchange(scenesSkip_, nullptr); }
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
	// The flow record's latched failure (FlowRollback::Failure).
	const char* StateFailure() const override { return flow_.Failure(); }

	std::string Describe() const; // JSON for the status report

	// Gives every random generator in the battle state (and the global one) a
	// seed derived from `seed` and the generator's place in its region, the
	// same in every process, and sets the match's start time (which the round
	// starts reseed the fighters' generators from) to `seed`. The game takes
	// both from the clock. Returns how many generators were reseeded.
	int ReseedRandom(uint32_t seed);
	// The match's start time (sfv::battledata::kStartTime), which lies outside
	// the rollback state: a match data reset takes it from the clock again.
	uint32_t StartTime() const;
	void SetStartTime(uint32_t value);
	// The game changed outside an update (the rematch's restart): the flow
	// record mirrors it again, so the next save holds what the game holds.
	void Recapture() { flow_.Capture(); }

	// The game's post-restore callbacks, run by its own Restore (logged at bind).
	struct PostRestore {
		void (*fn)(void* context, const int32_t* frameId);
		void* context;
	};
	const std::vector<PostRestore>& PostRestoreCallbacks() const { return postRestore_; }

private:
	void Forget();
	uint8_t* FindUnit(uint32_t typeRva) const;
	void WaitingToBind(const char* what);
	bool AddGameRegions(uint8_t* manager);
	void CollectPostRestore(uint8_t* manager);
	void RunUpdate();
	// A frame whose inputs carry the skip bit with the match decided: the
	// current flow state moved on (intro_skip::ScenesStepFor), before the
	// frame's update.
	void EndScenes();
	// The input unit's ticks per update while the match is decided: logged
	// each time the count changes.
	void NoteDecidedTicks();
	// Both fighters' CharaVisualParts, in the battle's order (null where
	// there is none).
	void FighterVisualParts(uint8_t* parts[kPlayers]) const;
	// The cost line (every kCostAdvances updates played forward): each kind
	// of update's time, and saving, loading, hashing (StateLayout::TakeCosts)
	// and the restore's callbacks since the last line.
	void LogCosts();

	BattleIdentity identity_;
	uint8_t* battleFrame_ = nullptr;
	uint8_t* battleData_ = nullptr;
	BattleFrameFn original_ = nullptr;
	uint8_t* inputUnit_ = nullptr;
	uint8_t* rollbackUnit_ = nullptr;
	bool bound_ = false;
	const char* waitingFor_ = nullptr; // what Bind last waited for (logged once)
	StateLayout layout_;
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
	// The models show the state the next update starts from: the engine drew a
	// frame since the last update, and nothing was restored (SocketReads).
	bool poseFresh_ = false;
	std::vector<size_t> fighterRegions_[kPlayers]; // each side's regions, the key of its socket reads
	uint8_t* fighterVisualParts_[kPlayers] = {};   // the fighters' CharaVisualParts, in the battle's order (from Bind)
	uint64_t keyedUpdates_ = 0; // updates whose fighters were keyed, and the time that took
	int64_t keyTicks_ = 0;
	FlowRollback flow_;
	// The costs since the last cost line (LogCosts), in QueryPerformanceCounter
	// ticks: updates played forward and replayed, and AfterLoad's work.
	struct UpdateCosts {
		uint64_t ticks = 0, replayed = 0, callbacks = 0;
		int64_t tickTicks = 0, replayedTicks = 0, callbackTicks = 0;
	};
	UpdateCosts costs_;

	// Ending the decided match's scenes (EndScenes); the game's functions
	// from the bind's image base.
	uintptr_t imageBase_ = 0;
	int64_t (*flowRequest_)(uint8_t* flowUnit, int32_t state, uint8_t flags, int32_t arg) = nullptr; // sfv::kFlowRequest
	int64_t (*setFlowPhase_)(uint8_t* flowState, int32_t phase) = nullptr;                          // sfv::kSetFlowPhase
	void (*markResultSkipped_)(uint8_t* unitContext) = nullptr;                                      // sfv::kMarkResultSkipped
	bool resimulating_ = false;       // the update now running replays a frame after a rollback
	const char* scenesSkip_ = nullptr; // TakeScenesSkip: points at scenesSkipText_
	char scenesSkipText_[192] = {};
	// What EndScenes last left alone (state id, phase), logged once each.
	int scenesIdleState_ = -1;
	int scenesIdlePhase_ = -1;
	int64_t decidedTicks_ = -1; // the tick count last logged with the match decided; -1 none
};

} // namespace sfve::mod
