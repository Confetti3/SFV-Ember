#pragma once

// Keeps a copy of the game playing while another window has the focus.
//
// SFV reads its pads (and keeps its battle running) only while it believes it
// is the foreground application, which it decides by asking Windows which
// window is in front. With background play on, that question answers with
// this process's own window, so two copies side by side, or a copy behind a
// stream tool, both keep running and reading their (virtual) pads.
//
// It keeps the game running there for two reasons, as SF4 Ember's
// BackgroundPlay::PolicyFor does (keepRunning):
//  - the player's saved Play in the background, or -sfve_background, read as
//    the game starts (dllmain): a change to the setting takes effect at the
//    next start;
//  - a room's match this copy only watches, from its launch to its end,
//    whatever the setting: a watcher behind a stream tool, or behind the
//    chat it types in, would otherwise stop at its window's focus, fall
//    behind P1's stream and be dropped. The pads reach the game behind
//    another window then, as with the setting; a watched fight plays P1's
//    inputs, not this copy's.
// Otherwise the queries and the window's messages pass as they are, and the
// game follows its window's focus as unmodded. In exclusive full screen (the
// game's own Screen Mode setting, see mod/ScreenMode.hxx) it lets the engine
// hear that it lost the focus, as the game does unmodded: Windows takes such
// a window out of full screen then.

#include "mod/GameHooks.hxx"

#include <string>
#include <vector>

namespace sfve::mod {

// A top-level window of this process, as the choice of the game's window sees it.
struct WindowCandidate {
	std::wstring className;
	bool visible = false;
	long area = 0;
};

// The game's window among a process's top-level windows: the largest visible
// one of the engine's window class (UnrealWindow). Not the largest of any
// class: the engine's splash screen (SplashScreenClass) is visible and
// can be the larger before the game window is. -1 when there is none.
int SelectGameWindow(const std::vector<WindowCandidate>& windows);

// Hooks the three window queries as one set (CommitHookSet) and starts the
// window filter, with the player's setting (`setting`: keep running behind
// for the whole session). Empty when they are in; otherwise the hook that
// failed, and none is.
std::optional<HookFailure> InstallBackgroundPlay(bool setting);

// Game thread, every frame: whether this copy only watches the room's match
// now. As it starts or stops keeping the game running behind another window,
// the engine is told it is active or, still behind, that it is not.
void KeepRunningWhileWatching(bool watching);

} // namespace sfve::mod
