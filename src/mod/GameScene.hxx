#pragma once

// SFV's screens are scene states (UObjects) run by one scene manager: the
// title, the main menu, character select, a battle. A state's tick calls its
// current step function (a pointer in the state), and a step changes scene
// by asking the manager for a new state class. The mod hooks the manager's
// tick (once a frame on the game thread): it learns the manager there, runs
// the game-thread queue (GameThread) there, and can then see the current
// state and change scene itself.

#include <stdint.h>

#include <string>

namespace sfve::mod {

// Also publishes the current state's kind as the status "scene".
bool InstallGameScene(uintptr_t imageBase);

// The scene manager's current state; null before the manager's first tick.
// Game thread only.
uint8_t* CurrentSceneState();

// The state's vtable as an rva into the game exe (0 for null).
uintptr_t SceneStateKind(const uint8_t* state);

// A scene change is already asked for (the manager has a next state).
// Game thread only.
bool SceneChangePending();

// The main menu is the current scene, with no change asked for: a match can
// be launched from it (VersusLaunch). Game thread only.
bool MainMenuInFront();

// The current scene's name for logs ("MainMenu"), or its kind in hex when it
// has none here; empty before the manager's first tick and while a scene
// change is pending. Game thread only.
std::string CurrentSceneName();

// Asks the manager to change to a state of `stateClass` (a UClass*), with an
// optional init message the new state receives. Game thread only; false
// before the manager is known.
bool ChangeScene(uint8_t* stateClass, uint8_t* initMessage);

} // namespace sfve::mod
