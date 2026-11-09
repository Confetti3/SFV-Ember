#include "mod/GameHooks.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameMemory.hxx"
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
using UnitTickFn = void (*)(uint8_t* rollbackUnit);
using GetManagerFn = uint8_t* (*)(uint8_t* context);
using FlowMessageFn = uint64_t (*)(uint8_t* receiver, uint8_t* message);
using FindUnitFn = GameSharedPtr* (*)(uint8_t* battle, GameSharedPtr* out, const void* type, uint32_t index);
using GetBattleDataFn = uint8_t* (*)(uint8_t* context);
using GameAllocFn = void* (*)(size_t size);
using BattleFrameCtorFn = uint8_t* (*)(uint8_t* battleFrame);

BattleListener* s_listener;
BattleFrameFn o_battleFrame;
BattleFrameCtorFn o_battleFrameCtor;
InputRecordFn o_inputRecord;
UnitTickFn o_unitTick;
GetManagerFn s_getManager;
FlowMessageFn o_flowMessage;
uintptr_t s_stateMessageType;
FindUnitFn s_findUnit;
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

void HookTransaction::Add(uintptr_t imageBase, const CodeSite& site, void* detour, void** original)
{
	hooks_.push_back({site.name, reinterpret_cast<void*>(imageBase + site.rva), detour, original, nullptr});
}

std::optional<HookFailure> HookTransaction::Commit()
{
	std::vector<Hook> hooks = std::move(hooks_);
	hooks_.clear();
	std::optional<HookFailure> failure;
	size_t created = 0;
	for (; created < hooks.size(); created++) {
		Hook& hook = hooks[created];
		hook.before = *hook.original;
		const MH_STATUS status = calls_.create(hook.target, hook.detour, hook.original);
		if (status != MH_OK) {
			failure = HookFailure{hook.site, status};
			break;
		}
	}
	for (size_t i = 0; !failure && i < created; i++) {
		const MH_STATUS status = calls_.enable(hooks[i].target);
		if (status != MH_OK) failure = HookFailure{hooks[i].site, status};
	}
	if (!failure) return std::nullopt;
	for (size_t i = 0; i < created; i++) {
		// A hook MinHook could not remove keeps its trampoline, which the
		// original pointer still names.
		if (calls_.remove(hooks[i].target) == MH_OK) *hooks[i].original = hooks[i].before;
	}
	return failure;
}

bool HookCodeSite(uintptr_t base, const CodeSite& site, void* detour, void** original)
{
	HookTransaction hook;
	hook.Add(base, site, detour, original);
	const std::optional<HookFailure> failure = hook.Commit();
	if (failure) Log("hooks: %s failed (%d)", failure->site, (int)failure->status);
	return !failure;
}

void AddGameHooks(HookTransaction& hooks, uintptr_t base, BattleListener* listener)
{
	s_listener = listener;
	s_getManager = reinterpret_cast<GetManagerFn>(base + sfv::kGetManager.rva);
	s_stateMessageType = base + sfv::flow::kStateMessageType;
	s_findUnit = reinterpret_cast<FindUnitFn>(base + sfv::kFindUnit.rva);
	s_getBattleData = reinterpret_cast<GetBattleDataFn>(base + sfv::kGetBattleData.rva);
	s_imageBase = base;
	// The flow's delegates allocate through it even if the hook below is not
	// installed; hooking replaces this with the trampoline, and a set that
	// does not go in puts this back.
	o_gameAlloc = reinterpret_cast<GameAllocFn>(base + sfv::kGameAlloc.rva);
	hooks.Add(base, sfv::kInputRecord, (void*)h_InputRecord, (void**)&o_inputRecord);
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

uint8_t* GameFindUnit(uint8_t* anyUnit, uint32_t typeRva, uint32_t index)
{
	uint8_t* owner = *reinterpret_cast<uint8_t**>(anyUnit + sfv::unit::kContext + sfv::unit::kContextOwner);
	uint8_t* battle = owner ? *reinterpret_cast<uint8_t**>(owner + sfv::unit::kOwnerBattle) : nullptr;
	if (!battle) return nullptr;
	GameSharedPtr found{};
	s_findUnit(battle, &found, reinterpret_cast<const void*>(s_imageBase + typeRva), index);
	// Hand back the reference: the battle keeps the unit alive.
	if (found.control) ReleaseKeepingAlive(found.control);
	return found.object;
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
