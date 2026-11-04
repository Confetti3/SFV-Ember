#pragma once

// Receives the battle hooks and runs each rendered frame of battle through
// the process's BattleMode (probe, synctest or netplay) once the adapter has
// bound this battle's state. Logs an inventory of each battle's rollback
// state and publishes a "battle" status field.

#include "mod/BattleMode.hxx"
#include "mod/GameHooks.hxx"
#include "mod/SfvAdapter.hxx"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace sfve::mod {

class BattleDriver : public BattleListener {
public:
	explicit BattleDriver(std::unique_ptr<BattleMode> mode) : mode_(std::move(mode)) {}

	void OnBattleFrame(uint8_t* battleFrame, BattleFrameFn original) override;
	void OnInputRecord(uint8_t* inputUnit) override { adapter_.OnInputRecord(inputUnit); }
	void OnRollbackUnitTick(uint8_t* rollbackUnit) override { adapter_.SeeRollbackUnit(rollbackUnit); }
	void OnFlowState(int state) override { adapter_.SeeFlowState(state); }

	// The "battle" status field and the "restart" command (the mode's work
	// starts over on the current battle, from the game thread's next frame).
	// Diagnostics: "watch <region name or size> <offset>" puts a write
	// breakpoint on a field of that region (by size: the first of that size);
	// "watchhits" lists the writers.
	void RegisterControl();

private:
	void LogInventory() const;
	void PublishStatus();

	std::unique_ptr<BattleMode> mode_;
	SfvAdapter adapter_;
	uint8_t* boundBattle_ = nullptr; // the battle frame the mode was last given
	std::atomic<bool> restart_{false};
	uint32_t frames_ = 0;
	std::atomic<uint32_t> gameThread_{0};

	std::mutex statusMutex_;
	std::string status_ = "null";
};

} // namespace sfve::mod
