#pragma once

// Receives the battle hooks and runs each rendered frame of battle through
// the process's BattleMode (probe, synctest or netplay) once the adapter has
// bound this battle's state. Logs an inventory of each battle's rollback
// state and publishes a "battle" status field.

#include "mod/BattleMode.hxx"
#include "mod/GameHooks.hxx"
#include "mod/SfvAdapter.hxx"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::mod {

class BattleDriver : public BattleListener {
public:
	explicit BattleDriver(std::unique_ptr<BattleMode> mode) : mode_(std::move(mode)) {}

	void OnBattleFrame(uint8_t* battleFrame, BattleFrameFn original) override;
	void OnBattleFrameConstructed(uint8_t* battleFrame) override { adapter_.SeeBattleFrameConstructed(battleFrame); }
	void OnInputRecord(uint8_t* inputUnit) override { adapter_.OnInputRecord(inputUnit); }
	bool InjectingInputs() const override { return adapter_.Injecting(); }
	bool InjectedAny(uint32_t mask) const override { return adapter_.InjectedAny(mask); }
	void OnRollbackUnitTick(uint8_t* rollbackUnit) override { adapter_.SeeRollbackUnit(rollbackUnit); }
	void OnFlowState(int state) override { adapter_.SeeFlowState(state); }
	// Not while a linked or replayed battle loads, nor where the mode says.
	bool GamePauseAllowed() const override;

	// The "battle" status field and the "restart" command (the mode's work
	// starts over on the current battle, from the game thread's next frame).
	// Diagnostics on the bound battle's state, regions named by name or by
	// size (the first of that size): "scan <u32>..." finds fields holding the
	// values, "peek <region> <offset> <length>" reads words, and "watch
	// <region> <offset>" puts a write breakpoint on a field; "watchhits" lists
	// the writers. The state is read on the game thread (ReadBattle).
	void RegisterControl();
	// The mode's own install (BattleMode::Install); once, after RegisterControl.
	void InstallMode() { mode_->Install(); }

private:
	struct Reading;
	using ReadFn = std::function<std::string(const StateLayout&)>;

	void LogInventory() const;
	void PublishStatus();
	// From the pipe thread: `read` runs on the game thread at the next battle
	// frame, before the frame's update and only once the battle is bound, the
	// one point where the layout is known to describe live memory. Its answer,
	// or an error when the battle is not bound or no battle frame comes.
	std::string ReadBattle(ReadFn read);
	// Each battle frame (game thread): answers the readings asked for.
	void AnswerReadings(bool bound);

	std::unique_ptr<BattleMode> mode_;
	SfvAdapter adapter_;
	uint64_t boundBattle_ = 0; // the battle (SfvAdapter::Battle) the mode was last given
	std::atomic<bool> restart_{false};
	uint32_t frames_ = 0;

	std::mutex readingsMutex_;
	std::vector<std::shared_ptr<Reading>> readings_; // asked for, not yet taken by the game thread

	std::mutex statusMutex_;
	std::string status_ = "null";
};

} // namespace sfve::mod
