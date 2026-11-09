#pragma once

// The game functions the mod hooks (SfvOffsets.hxx), reported to one
// listener on the game thread.

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <vector>

#include <MinHook.h>

#include "mod/BuildCheck.hxx"

namespace sfve::mod {

using BattleFrameFn = void (*)(uint8_t* battleFrame);

class BattleListener {
public:
	virtual ~BattleListener() = default;
	// One rendered frame of battle. The listener decides how (and whether) the
	// original runs; `original` is the unhooked function.
	virtual void OnBattleFrame(uint8_t* battleFrame, BattleFrameFn original) = 0;
	// The game constructed a battle frame: a new battle, wherever it lies.
	virtual void OnBattleFrameConstructed(uint8_t* battleFrame) = 0;
	// A battle tick is about to record its pad inputs.
	virtual void OnInputRecord(uint8_t* inputUnit) = 0;
	// A battle tick reached its snapshot task.
	virtual void OnRollbackUnitTick(uint8_t* rollbackUnit) = 0;
	// The battle flow announced a new state (sfv::flow).
	virtual void OnFlowState(int state) = 0;
};

// The MinHook calls a HookTransaction makes: MinHook's own, unless a test
// gives stand-ins that fail a step on purpose.
struct HookCalls {
	MH_STATUS(WINAPI* create)(LPVOID target, LPVOID detour, LPVOID* original) = &MH_CreateHook;
	MH_STATUS(WINAPI* enable)(LPVOID target) = &MH_EnableHook;
	MH_STATUS(WINAPI* remove)(LPVOID target) = &MH_RemoveHook;
};

// The hook a HookTransaction could not install, and MinHook's status for it.
struct HookFailure {
	const char* site;
	MH_STATUS status;
};

// Code-site hooks that go in together or not at all. Commit creates every
// hook, then enables them. When one fails, it removes every hook it created
// (MinHook disables an enabled one first) and puts back the value each
// original pointer held before, so nothing of the set is left in the game and
// no pointer is left on a freed trampoline. A hook it did not create (one
// that was already there) stays as it was.
class HookTransaction {
public:
	explicit HookTransaction(HookCalls calls = {}) : calls_(calls) {}

	void Add(uintptr_t imageBase, const CodeSite& site, void* detour, void** original);
	// Installs the hooks added since the last Commit: empty when all are in.
	std::optional<HookFailure> Commit();

private:
	struct Hook {
		const char* site;
		void* target;
		void* detour;
		void** original;
		void* before; // *original before the hook was created
	};
	HookCalls calls_;
	std::vector<Hook> hooks_;
};

// Hooks one verified code site (a transaction of one); logs a failure.
bool HookCodeSite(uintptr_t imageBase, const CodeSite& site, void* detour, void** original);

// Adds the listener's hooks (the battle frame and its constructor, the input
// record, the rollback unit, the battle flow and the allocator) to the
// battle's set, which goes in whole or not at all (dllmain). The listener
// must outlive the game.
void AddGameHooks(HookTransaction& hooks, uintptr_t imageBase, BattleListener* listener);

// The game's GetManager, for the rollback unit's context.
uint8_t* GameGetManager(uint8_t* rollbackUnit);

// The battle's match data (sfv::battledata), from any unit.
uint8_t* GameGetBattleData(uint8_t* anyUnit);

// The battle code's operator new (closures for the game's delegates, which
// free them); hooked so every allocation comes zeroed.
void* GameAlloc(size_t size);

// The battle's unit of a type (sfv::unit::k*Type), found from any unit the
// way units find each other; `index` counts units of exactly that type in
// the order the battle added them. Null when there is none.
uint8_t* GameFindUnit(uint8_t* anyUnit, uint32_t typeRva, uint32_t index = 0);

} // namespace sfve::mod
