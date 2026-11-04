#pragma once

// The game functions the mod hooks (SfvOffsets.hxx), reported to one
// listener on the game thread.

#include <stddef.h>
#include <stdint.h>

#include "mod/BuildCheck.hxx"

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
	// The battle flow announced a new state (sfv::flow).
	virtual void OnFlowState(int state) = 0;
};

// Hooks one verified code site (MinHook, enabled at once); logs a failure.
bool HookCodeSite(uintptr_t imageBase, const CodeSite& site, void* detour, void** original);

// Hooks the verified build's functions. The listener must outlive the game.
bool InstallGameHooks(uintptr_t imageBase, BattleListener* listener);

// The game's GetManager, for the rollback unit's context.
uint8_t* GameGetManager(uint8_t* rollbackUnit);

// The battle's match data (sfv::battledata), from any unit.
uint8_t* GameGetBattleData(uint8_t* anyUnit);

// The battle code's operator new (closures for the game's delegates, which
// free them); hooked so every allocation comes zeroed.
void* GameAlloc(size_t size);

// The battle's unit of a type (sfv::unit::k*Type), found from any unit the
// way units find each other; null when there is none.
uint8_t* GameFindUnit(uint8_t* anyUnit, uint32_t typeRva);

} // namespace sfve::mod
