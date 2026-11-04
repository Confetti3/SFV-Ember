#include "mod/GameScene.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <MinHook.h>

#include <stdio.h>

#include <atomic>
#include <string>

namespace sfve::mod {

namespace {

using ChangeSceneStateFn = void (*)(uint8_t* manager, uint8_t* stateClass, uint8_t* initMessage);

ChangeSceneStateFn o_changeSceneState;
std::atomic<uint8_t*> s_manager;
std::atomic<uintptr_t> s_kind; // the current state's kind, for other threads
uintptr_t s_imageBase;

void h_ChangeSceneState(uint8_t* manager, uint8_t* stateClass, uint8_t* initMessage)
{
	if (s_manager.exchange(manager) != manager) Log("scene: manager %p", (void*)manager);
	o_changeSceneState(manager, stateClass, initMessage);
}

} // namespace

bool InstallGameScene(uintptr_t imageBase)
{
	s_imageBase = imageBase;
	// Other threads read the scene's kind, never the state itself (it can be
	// freed by a scene change at any time).
	RunEachGameFrame([] {
		s_kind = SceneStateKind(CurrentSceneState());
		return true;
	});
	RegisterStatus("scene", [] {
		char text[24];
		snprintf(text, sizeof(text), "\"0x%llX\"", (unsigned long long)s_kind.load());
		return std::string(text);
	});
	void* target = reinterpret_cast<void*>(imageBase + sfv::kChangeSceneState.rva);
	MH_STATUS st = MH_CreateHook(target, (void*)h_ChangeSceneState, (void**)&o_changeSceneState);
	if (st == MH_OK) st = MH_EnableHook(target);
	if (st != MH_OK) Log("scene: hooking %s failed (%d)", sfv::kChangeSceneState.name, (int)st);
	return st == MH_OK;
}

uint8_t* CurrentSceneState()
{
	uint8_t* manager = s_manager;
	return manager ? *reinterpret_cast<uint8_t**>(manager + sfv::scene::kManagerCurrentState) : nullptr;
}

uintptr_t SceneStateKind(const uint8_t* state)
{
	return state ? *reinterpret_cast<const uintptr_t*>(state) - s_imageBase : 0;
}

bool ChangeScene(uint8_t* stateClass, uint8_t* initMessage)
{
	uint8_t* manager = s_manager;
	if (!manager || !o_changeSceneState) return false;
	o_changeSceneState(manager, stateClass, initMessage);
	return true;
}

} // namespace sfve::mod
