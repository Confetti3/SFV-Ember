#include "mod/GameHooks.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GuardedStart.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <MinHook.h>

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <atomic>

namespace sfve::mod {

namespace {

using InputRecordFn = uint64_t (*)(uint8_t* inputUnit, uint32_t predictRemote);
using InputAnyFn = uint64_t (*)(uint8_t* unitContext, uint32_t mask);
using UnitTickFn = void (*)(uint8_t* rollbackUnit);
using GetManagerFn = uint8_t* (*)(uint8_t* context);
using FlowMessageFn = uint64_t (*)(uint8_t* receiver, uint8_t* message);
// The game's lookups of a battle's unit (FindUnit) and of a unit's part (FindPart).
using GameLookupFn = GameSharedPtr* (*)(uint8_t* owner, GameSharedPtr* out, const void* type, uint32_t index);
using GetBattleDataFn = uint8_t* (*)(uint8_t* context);
using GameAllocFn = void* (*)(size_t size);
using BattleFrameCtorFn = uint8_t* (*)(uint8_t* battleFrame);

BattleListener* s_listener;
BattleFrameFn o_battleFrame;
BattleFrameCtorFn o_battleFrameCtor;
InputRecordFn o_inputRecord;
InputAnyFn o_padsAny;
UnitTickFn o_unitTick;
GetManagerFn s_getManager;
FlowMessageFn o_flowMessage;
uintptr_t s_stateMessageType;
GameLookupFn s_findUnit;
GameLookupFn s_findPart;
GetBattleDataFn s_getBattleData;
GameAllocFn o_gameAlloc;
std::atomic<uint64_t> s_zeroedAllocs;
std::atomic<uint64_t> s_zeroedBytes;
uintptr_t s_imageBase;

void h_BattleFrame(uint8_t* battleFrame)
{
	s_listener->OnBattleFrame(battleFrame, o_battleFrame);
}

uint8_t* h_BattleFrameCtor(uint8_t* battleFrame)
{
	uint8_t* constructed = o_battleFrameCtor(battleFrame);
	s_listener->OnBattleFrameConstructed(battleFrame);
	return constructed;
}

uint64_t h_InputRecord(uint8_t* inputUnit, uint32_t predictRemote)
{
	s_listener->OnInputRecord(inputUnit);
	return o_inputRecord(inputUnit, predictRemote);
}

// Once the match is decided the battle asks the pads themselves (kPadsAny):
// each copy would hear its own player, and a press that skips the winner's
// scenes would move one copy's flow alone. A tick that runs on a session's
// inputs hears those instead: both players' inputs for this frame, which
// outside the fight are the skip bit on a skip frame and nothing else
// (IntroSkip.hxx). Not the battle's input record (rva 0x2F2C40): the
// decided match no longer feeds it, so it never held the bit there.
uint64_t h_PadsAny(uint8_t* unitContext, uint32_t mask)
{
	if (s_listener->InjectingInputs()) return s_listener->InjectedAny(mask) ? 1 : 0;
	return o_padsAny(unitContext, mask);
}

void h_UnitTick(uint8_t* rollbackUnit)
{
	s_listener->OnRollbackUnitTick(rollbackUnit);
	o_unitTick(rollbackUnit);
}

uint64_t h_FlowMessage(uint8_t* receiver, uint8_t* message)
{
	// The message's first virtual function names its type.
	using TypeFn = uintptr_t (*)(uint8_t*);
	const TypeFn type = **reinterpret_cast<TypeFn**>(message);
	if (type(message) == s_stateMessageType) {
		s_listener->OnFlowState(*reinterpret_cast<int*>(message + sfv::flow::kStateIndex));
	}
	return o_flowMessage(receiver, message);
}

// The battle code's operator new hands out memory as the heap left it, and
// its objects do not set every field they own before the battle uses them:
// padding, arrays filled on first use, the halves of 8-byte slots a 4-byte
// value went into. Each process's leftovers differ, so two copies playing the
// same match held different bytes in their rollback state until the game
// wrote them (some only at a round start). Zeroed memory is the same in every
// process; nothing correct can depend on what the heap left.
void* h_GameAlloc(size_t size)
{
	void* p = o_gameAlloc(size);
	if (p) {
		memset(p, 0, size);
		s_zeroedAllocs.fetch_add(1, std::memory_order_relaxed);
		s_zeroedBytes.fetch_add(size, std::memory_order_relaxed);
	}
	return p;
}

} // namespace

void HookTransaction::Add(const char* name, void* target, void* detour, void** original)
{
	hooks_.push_back({name, target, detour, original, nullptr});
}

void HookTransaction::Add(uintptr_t imageBase, const CodeSite& site, void* detour, void** original)
{
	Add(site.name, reinterpret_cast<void*>(imageBase + site.rva), detour, original);
}

HookCommit HookTransaction::Commit()
{
	std::vector<Hook> hooks = std::move(hooks_);
	hooks_.clear();
	std::optional<HookFailure> failure;
	size_t created = 0;
	for (; created < hooks.size(); created++) {
		Hook& hook = hooks[created];
		hook.before = *hook.original;
		const MH_STATUS status =
			hook.target ? calls_.create(hook.target, hook.detour, hook.original) : MH_ERROR_FUNCTION_NOT_FOUND;
		if (status != MH_OK) {
			failure = HookFailure{hook.site, status};
			break;
		}
	}
	for (size_t i = 0; !failure && i < created; i++) {
		const MH_STATUS status = calls_.enable(hooks[i].target);
		if (status != MH_OK) failure = HookFailure{hooks[i].site, status};
	}
	if (!failure) return {};
	HookCommit commit{HookCommit::Outcome::RolledBack, *failure, {}};
	for (size_t i = 0; i < created; i++) {
		const MH_STATUS status = calls_.remove(hooks[i].target);
		if (status == MH_OK) {
			*hooks[i].original = hooks[i].before;
		} else if (commit.outcome == HookCommit::Outcome::RolledBack) {
			// It keeps its trampoline, which the original pointer still names.
			commit.outcome = HookCommit::Outcome::RollbackIncomplete;
			commit.unremoved = HookFailure{hooks[i].site, status};
		}
	}
	return commit;
}

std::optional<HookFailure> CommitHookSet(HookTransaction& hooks, const char* set)
{
	const HookCommit commit = hooks.Commit();
	switch (commit.outcome) {
	case HookCommit::Outcome::Installed: return std::nullopt;
	case HookCommit::Outcome::RolledBack:
		Log("hooks: nothing of the %s set is in: %s could not be hooked (%d)", set, commit.failed.site,
			(int)commit.failed.status);
		return commit.failed;
	case HookCommit::Outcome::RollbackIncomplete: break;
	}
	// Part of the set is still live: an enabled hook MinHook could not
	// disable, whose detour feeds a set that is not in (a battle listener
	// whose mode never installs, an overlay that never releases the back
	// buffer it holds). Playing on would run that untested half; trying the
	// removal again would count on the memory protection that just failed.
	// Every set is committed from SfveStart, before the game's code has run,
	// so ending the process here costs the player one start and nothing of a
	// match, the same fail-closed answer as an incomplete network guard, with
	// its own exit code.
	Log("start refused: %s could not be hooked (%d) and %s could not be taken back out (%d), so part of the %s set "
		"is still in; closing the game so it never runs half hooked", commit.failed.site, (int)commit.failed.status,
		commit.unremoved.site, (int)commit.unremoved.status, set);
	start::EndProcess(start::Refusal::HooksStuck);
}

bool HookCodeSite(uintptr_t base, const CodeSite& site, void* detour, void** original)
{
	HookTransaction hook;
	hook.Add(base, site, detour, original);
	return !CommitHookSet(hook, site.name);
}

void AddGameHooks(HookTransaction& hooks, uintptr_t base, BattleListener* listener)
{
	s_listener = listener;
	s_getManager = reinterpret_cast<GetManagerFn>(base + sfv::kGetManager.rva);
	s_stateMessageType = base + sfv::flow::kStateMessageType;
	s_findUnit = reinterpret_cast<GameLookupFn>(base + sfv::kFindUnit.rva);
	s_findPart = reinterpret_cast<GameLookupFn>(base + sfv::kFindPart.rva);
	s_getBattleData = reinterpret_cast<GetBattleDataFn>(base + sfv::kGetBattleData.rva);
	s_imageBase = base;
	// The flow's delegates allocate through it even if the hook below is not
	// installed; hooking replaces this with the trampoline, and a set that
	// does not go in puts this back.
	o_gameAlloc = reinterpret_cast<GameAllocFn>(base + sfv::kGameAlloc.rva);
	hooks.Add(base, sfv::kInputRecord, (void*)h_InputRecord, (void**)&o_inputRecord);
	hooks.Add(base, sfv::kPadsAny, (void*)h_PadsAny, (void**)&o_padsAny);
	hooks.Add(base, sfv::kRollbackUnitPre, (void*)h_UnitTick, (void**)&o_unitTick);
	hooks.Add(base, sfv::kBattleFrameUpdate, (void*)h_BattleFrame, (void**)&o_battleFrame);
	hooks.Add(base, sfv::kBattleFrameCtor, (void*)h_BattleFrameCtor, (void**)&o_battleFrameCtor);
	hooks.Add(base, sfv::kFlowMessage, (void*)h_FlowMessage, (void**)&o_flowMessage);
	hooks.Add(base, sfv::kGameAlloc, (void*)h_GameAlloc, (void**)&o_gameAlloc);
	RegisterStatus("zeroedAllocs", [] {
		char text[64];
		snprintf(text, sizeof(text), "{\"count\":%llu,\"bytes\":%llu}",
			(unsigned long long)s_zeroedAllocs.load(std::memory_order_relaxed),
			(unsigned long long)s_zeroedBytes.load(std::memory_order_relaxed));
		return std::string(text);
	});
}

namespace {

// What a lookup found, with the reference it took given back: the battle
// keeps its units alive, and a unit its parts.
uint8_t* Look(GameLookupFn lookup, uint8_t* owner, uint32_t typeRva, uint32_t index)
{
	GameSharedPtr found{};
	lookup(owner, &found, reinterpret_cast<const void*>(s_imageBase + typeRva), index);
	if (found.control) ReleaseKeepingAlive(found.control);
	return found.object;
}

} // namespace

uint8_t* GameFindUnit(uint8_t* anyUnit, uint32_t typeRva, uint32_t index)
{
	uint8_t* owner = *reinterpret_cast<uint8_t**>(anyUnit + sfv::unit::kContext + sfv::unit::kContextOwner);
	uint8_t* battle = owner ? *reinterpret_cast<uint8_t**>(owner + sfv::unit::kOwnerBattle) : nullptr;
	return battle ? Look(s_findUnit, battle, typeRva, index) : nullptr;
}

uint8_t* GameFindPart(uint8_t* unit, uint32_t typeRva, uint32_t index)
{
	return Look(s_findPart, unit, typeRva, index);
}

uint8_t* GameGetManager(uint8_t* rollbackUnit)
{
	return s_getManager(rollbackUnit + sfv::rollbackunit::kManagerContext);
}

uint8_t* GameGetBattleData(uint8_t* anyUnit)
{
	return s_getBattleData(anyUnit + sfv::unit::kContext);
}

void* GameAlloc(size_t size)
{
	return h_GameAlloc(size);
}

} // namespace sfve::mod
