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
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/SfvFlow.hxx"
#include "mod/SfvRegionNames.hxx"

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

class SfvAdapter : public GameAdapter {
public:
	// The battle objects as the hooks see them; a new battle frame means a new
	// battle, and the layout is rebuilt on the next Bind.
	void SeeBattleFrame(uint8_t* battleFrame, BattleFrameFn original);
	void SeeRollbackUnit(uint8_t* rollbackUnit) { rollbackUnit_ = rollbackUnit; }

	// Builds the layout once this battle's objects are known.
	bool Bind();
	bool Bound() const { return bound_; }
	uint8_t* BattleFrame() const { return battleFrame_; }
	// The battle's match data (rounds, results); null until bound.
	const uint8_t* BattleData() const { return battleData_; }

	// From the input-record hook: remembers what the game computed for each
	// pad this tick, then writes the injected inputs over it while Advance runs.
	void OnInputRecord(uint8_t* inputUnit);
	PackedInput Sampled(int player) const { return sampled_[player]; }
	// True when the battle is running ticks (not paused, not between rounds
	// with ticks stopped): the last original update recorded at least one tick.
	uint32_t TicksInLastUpdate() const { return ticksInLastUpdate_; }

	// The state the battle flow last announced (part of the rollback state).
	void SeeFlowState(int state) { flow_.Announce(state); }
	bool Fighting() const;
	// Only the fight reads the players' inputs (GameAdapter::InputsMatter).
	bool InputsMatter() const override { return Fighting(); }
	PackedInput InputBits() const override { return input::kInputBits; }

	// The original update with no injection (outside any session).
	void RunOriginal();

	std::string BuildId() const override;
	StateLayout& Layout() override { return layout_; }
	const StateLayout& Layout() const { return layout_; }
	void Advance(const FrameInputs& inputs, bool resimulating) override;
	// What the game's own Restore does after copying the regions back, then
	// the flow record written back.
	void AfterLoad() override;

	std::string Describe() const; // JSON for the status report

	// Gives every random generator in the battle state (and the global one) a
	// seed derived from `seed` and the generator's place in its region, the
	// same in every process, and sets the match's start time (which the round
	// starts reseed the fighters' generators from) to `seed`. The game takes
	// both from the clock. Returns how many generators were reseeded.
	int ReseedRandom(uint32_t seed);

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
	void AddStateOutsideTheGameSnapshot(uint8_t* ruleUnit, uint8_t* battleData, uintptr_t base);
	void RunUpdate();

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

	bool injecting_ = false;
	FrameInputs pending_;
	PackedInput sampled_[kPlayers] = {};
	uint32_t ticksInLastUpdate_ = 0;
	uint8_t* (*gameInstance_)() = nullptr; // sfv::kGameInstance
	uint32_t ticksThisUpdate_ = 0;
	FlowRollback flow_;
};

} // namespace sfve::mod
