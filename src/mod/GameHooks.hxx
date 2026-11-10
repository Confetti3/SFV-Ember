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
	// The battle tick now running takes a session's inputs in place of the
	// pads' (SfvAdapter::Advance): what the battle asks of the pads
	// themselves is then answered from those inputs too (InjectedAny).
	virtual bool InjectingInputs() const = 0;
	// Whether either player's injected input for this tick has a bit of
	// `mask`.
	virtual bool InjectedAny(uint32_t mask) const = 0;
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

// A hook of a set, by name, and MinHook's status for the step it failed.
struct HookFailure {
	const char* site;
	MH_STATUS status;
};

// What a HookTransaction's Commit left in the game.
struct HookCommit {
	enum class Outcome {
		Installed,          // every hook of the set is in and enabled
		RolledBack,         // `failed` could not go in, and every hook the commit created is out again
		RollbackIncomplete, // `failed` could not go in, and `unremoved` (the first of them) could not be taken out
	};
	Outcome outcome = Outcome::Installed;
	HookFailure failed{};    // unless Installed: the hook that could not be created or enabled
	HookFailure unremoved{}; // RollbackIncomplete only: the first hook MinHook could not remove
};

// Hooks that go in together or not at all: code sites of the game, or any
// function by its address. Commit creates every hook, then enables them.
// When one fails, it removes every hook it created (MinHook disables an
// enabled one first) and puts back the value each original pointer held
// before, so nothing of the set is left in the game and no pointer is left on
// a freed trampoline. A hook it did not create (one that was already there)
// stays as it was.
//
// A hook that cannot be removed stays in, its original pointer on its
// trampoline, and the rollback goes on with the rest; the commit then says
// RollbackIncomplete. With MinHook that happens only to an enabled hook it
// could not disable (removing one never enabled only frees its trampoline),
// so part of the set is still live in the game. CommitHookSet is the answer
// every caller gives to that.
class HookTransaction {
public:
	explicit HookTransaction(HookCalls calls = {}) : calls_(calls) {}

	// `name` is the hook's name in a HookFailure, so it must outlive the set
	// (a string literal or a CodeSite's name). A null `target` (GetProcAddress
	// found no such export) fails the set at Commit with
	// MH_ERROR_FUNCTION_NOT_FOUND.
	void Add(const char* name, void* target, void* detour, void** original);
	void Add(uintptr_t imageBase, const CodeSite& site, void* detour, void** original);
	// Installs the hooks added since the last Commit.
	HookCommit Commit();

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

// Commits `hooks`, the set called `set` in the log. Empty when every hook is
// in; otherwise the failure, logged, with nothing of the set left in the game.
// A set that could not be taken back out ends the process
// (start::Refusal::HooksStuck) instead of returning: every set is committed
// from SfveStart, before the game's code runs, and a set left half in would
// run hooks nothing owns.
std::optional<HookFailure> CommitHookSet(HookTransaction& hooks, const char* set);

// Hooks one verified code site (a set of one, CommitHookSet).
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
// A unit's part of a type (sfv::kFindPart), the way the game's own lookups
// find one; null when the unit has none.
uint8_t* GameFindPart(uint8_t* unit, uint32_t typeRva, uint32_t index = 0);

} // namespace sfve::mod
