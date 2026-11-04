#include "mod/GameHooks.hxx"

#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <MinHook.h>

#include <windows.h>

namespace sfve::mod {

namespace {

using InputRecordFn = uint64_t (*)(uint8_t* inputUnit, uint32_t predictRemote);
using UnitTickFn = void (*)(uint8_t* rollbackUnit);
using GetManagerFn = uint8_t* (*)(uint8_t* context);
using FlowMessageFn = uint64_t (*)(uint8_t* receiver, uint8_t* message);
using FindUnitFn = GameSharedPtr* (*)(uint8_t* battle, GameSharedPtr* out, const void* type, uint32_t index);
using GetBattleDataFn = uint8_t* (*)(uint8_t* context);
using GameAllocFn = void* (*)(size_t size);

BattleListener* s_listener;
BattleFrameFn o_battleFrame;
InputRecordFn o_inputRecord;
UnitTickFn o_unitTick;
GetManagerFn s_getManager;
FlowMessageFn o_flowMessage;
uintptr_t s_stateMessageType;
FindUnitFn s_findUnit;
GetBattleDataFn s_getBattleData;
GameAllocFn s_gameAlloc;
uintptr_t s_imageBase;

void h_BattleFrame(uint8_t* battleFrame)
{
	s_listener->OnBattleFrame(battleFrame, o_battleFrame);
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

} // namespace

bool HookCodeSite(uintptr_t base, const CodeSite& site, void* detour, void** original)
{
	void* target = reinterpret_cast<void*>(base + site.rva);
	MH_STATUS st = MH_CreateHook(target, detour, original);
	if (st == MH_OK) st = MH_EnableHook(target);
	if (st != MH_OK) Log("hooks: %s failed (%d)", site.name, (int)st);
	return st == MH_OK;
}

bool InstallGameHooks(uintptr_t base, BattleListener* listener)
{
	s_listener = listener;
	s_getManager = reinterpret_cast<GetManagerFn>(base + sfv::kGetManager.rva);
	s_stateMessageType = base + sfv::flow::kStateMessageType;
	s_findUnit = reinterpret_cast<FindUnitFn>(base + sfv::kFindUnit.rva);
	s_getBattleData = reinterpret_cast<GetBattleDataFn>(base + sfv::kGetBattleData.rva);
	s_gameAlloc = reinterpret_cast<GameAllocFn>(base + sfv::kGameAlloc.rva);
	s_imageBase = base;
	const bool ok = HookCodeSite(base, sfv::kInputRecord, (void*)h_InputRecord, (void**)&o_inputRecord) &&
		HookCodeSite(base, sfv::kRollbackUnitPre, (void*)h_UnitTick, (void**)&o_unitTick) &&
		HookCodeSite(base, sfv::kBattleFrameUpdate, (void*)h_BattleFrame, (void**)&o_battleFrame) &&
		HookCodeSite(base, sfv::kFlowMessage, (void*)h_FlowMessage, (void**)&o_flowMessage);
	Log("hooks: %s", ok ? "battle frame, input record, rollback unit and battle flow hooked" : "INCOMPLETE");
	return ok;
}

uint8_t* GameFindUnit(uint8_t* anyUnit, uint32_t typeRva)
{
	uint8_t* owner = *reinterpret_cast<uint8_t**>(anyUnit + sfv::unit::kContext + sfv::unit::kContextOwner);
	uint8_t* battle = owner ? *reinterpret_cast<uint8_t**>(owner + sfv::unit::kOwnerBattle) : nullptr;
	if (!battle) return nullptr;
	GameSharedPtr found{};
	s_findUnit(battle, &found, reinterpret_cast<const void*>(s_imageBase + typeRva), 0);
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
	return s_gameAlloc(size);
}

} // namespace sfve::mod
