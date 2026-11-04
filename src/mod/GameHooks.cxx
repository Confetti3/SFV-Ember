#include "mod/GameHooks.hxx"

#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <MinHook.h>

namespace sfve::mod {

namespace {

using InputRecordFn = uint64_t (*)(uint8_t* inputUnit, uint32_t predictRemote);
using UnitTickFn = void (*)(uint8_t* rollbackUnit);
using GetManagerFn = uint8_t* (*)(uint8_t* context);

BattleListener* s_listener;
BattleFrameFn o_battleFrame;
InputRecordFn o_inputRecord;
UnitTickFn o_unitTick;
GetManagerFn s_getManager;

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

bool Hook(uintptr_t base, const CodeSite& site, void* detour, void** original)
{
	void* target = reinterpret_cast<void*>(base + site.rva);
	MH_STATUS st = MH_CreateHook(target, detour, original);
	if (st == MH_OK) st = MH_EnableHook(target);
	if (st != MH_OK) Log("hooks: %s failed (%d)", site.name, (int)st);
	return st == MH_OK;
}

} // namespace

bool InstallGameHooks(uintptr_t base, BattleListener* listener)
{
	s_listener = listener;
	s_getManager = reinterpret_cast<GetManagerFn>(base + sfv::kGetManager.rva);
	const bool ok = Hook(base, sfv::kInputRecord, (void*)h_InputRecord, (void**)&o_inputRecord) &&
		Hook(base, sfv::kRollbackUnitPre, (void*)h_UnitTick, (void**)&o_unitTick) &&
		Hook(base, sfv::kBattleFrameUpdate, (void*)h_BattleFrame, (void**)&o_battleFrame);
	Log("hooks: %s", ok ? "battle frame, input record and rollback unit hooked" : "INCOMPLETE");
	return ok;
}

uint8_t* GameGetManager(uint8_t* rollbackUnit)
{
	return s_getManager(rollbackUnit + sfv::rollbackunit::kManagerContext);
}

} // namespace sfve::mod
