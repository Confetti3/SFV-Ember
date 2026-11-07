#include "mod/GameScene.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <stdio.h>

#include <atomic>
#include <mutex>
#include <string>

namespace sfve::mod {

namespace {

using ManagerTickFn = void (*)(uint8_t* manager, float deltaSeconds);
using ChangeSceneStateFn = void (*)(uint8_t* manager, uint8_t* stateClass, uint8_t* initMessage);

ManagerTickFn o_managerTick;
ChangeSceneStateFn o_changeSceneState;
uint8_t* s_manager;             // game thread only
std::atomic<uintptr_t> s_kind;  // the current state's kind, for other threads
uintptr_t s_imageBase;
std::mutex s_seenMutex;
BattleSeen s_seen;

// Once a frame, before the manager ticks its state (and, when a state has
// ended, puts the next one in its place): the queued game-thread work runs
// between two frames of a state.
void h_ManagerTick(uint8_t* manager, float deltaSeconds)
{
	if (s_manager != manager) {
		Log("scene: manager %p", (void*)manager);
		s_manager = manager;
	}
	PumpGameThread();
	o_managerTick(manager, deltaSeconds);
	s_kind = SceneStateKind(CurrentSceneState());
}

} // namespace

bool InstallGameScene(uintptr_t imageBase)
{
	s_imageBase = imageBase;
	// Other threads read the scene's kind, never the state itself (a scene
	// change can free it at any time).
	RegisterStatus("scene", [] {
		char text[24];
		snprintf(text, sizeof(text), "\"0x%llX\"", (unsigned long long)s_kind.load());
		return std::string(text);
	});
	o_changeSceneState = reinterpret_cast<ChangeSceneStateFn>(imageBase + sfv::kChangeSceneState.rva);
	return HookCodeSite(imageBase, sfv::kSceneManagerTick, (void*)h_ManagerTick, (void**)&o_managerTick);
}

uint8_t* CurrentSceneState()
{
	return s_manager ? *reinterpret_cast<uint8_t**>(s_manager + sfv::scene::kManagerCurrentState) : nullptr;
}

uintptr_t SceneStateKind(const uint8_t* state)
{
	return state ? *reinterpret_cast<const uintptr_t*>(state) - s_imageBase : 0;
}

bool SceneChangePending()
{
	return s_manager && *reinterpret_cast<uint8_t**>(s_manager + sfv::scene::kManagerNextState);
}

bool MainMenuInFront()
{
	return SceneStateKind(CurrentSceneState()) == sfv::scene::kMainMenuVtable && !SceneChangePending();
}

std::string CurrentSceneName()
{
	const uint8_t* state = CurrentSceneState();
	if (!state || SceneChangePending()) return {};
	switch (SceneStateKind(state)) {
	case sfv::title::kVtable: return "Title";
	case sfv::scene::kMainMenuVtable: return "MainMenu";
	case sfv::scene::kJumpStateVtable: return "BattleJumpSetup";
	case sfv::scene::kVersusBattleVtable: return "VersusBattle";
	case sfv::scene::kTrainingBattleVtable: return "TrainingBattle";
	}
	char text[24];
	snprintf(text, sizeof(text), "0x%llX", (unsigned long long)SceneStateKind(state));
	return text;
}

BattleSeen LastBattleSeen()
{
	std::lock_guard lock(s_seenMutex);
	return s_seen;
}

void NoteBattleSeen(const BattleSeen& seen)
{
	std::lock_guard lock(s_seenMutex);
	s_seen = seen;
}

bool ChangeScene(uint8_t* stateClass, uint8_t* initMessage)
{
	if (!s_manager) return false;
	o_changeSceneState(s_manager, stateClass, initMessage);
	return true;
}

} // namespace sfve::mod
