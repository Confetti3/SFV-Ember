#pragma once

// The game functions the mod hooks (SfvOffsets.hxx), reported to one
// listener on the game thread.

#include <stdint.h>

namespace sfve::mod {

using BattleFrameFn = void (*)(uint8_t* battleFrame);

class BattleListener {
public:
	virtual ~BattleListener() = default;
	// One rendered frame of battle. The listener decides how (and whether) the
	// original runs; `original` is the unhooked function.
	virtual void OnBattleFrame(uint8_t* battleFrame, BattleFrameFn original) = 0;
	// A battle tick is about to record its pad inputs.
	virtual void OnInputRecord(uint8_t* inputUnit) = 0;
	// A battle tick reached its snapshot task.
	virtual void OnRollbackUnitTick(uint8_t* rollbackUnit) = 0;
};

// Hooks the verified build's functions. The listener must outlive the game.
bool InstallGameHooks(uintptr_t imageBase, BattleListener* listener);

// The game's GetManager, for the rollback unit's context.
uint8_t* GameGetManager(uint8_t* rollbackUnit);

} // namespace sfve::mod
