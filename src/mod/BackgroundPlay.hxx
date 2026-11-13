#pragma once

// Keeps a copy of the game playing while another window has the focus.
//
// SFV reads its pads (and keeps its battle running) only while it believes it
// is the foreground application, which it decides by asking Windows which
// window is in front. With background play on, that question answers with
// this process's own window, so two copies side by side, or a copy behind a
// stream tool, both keep running and reading their (virtual) pads.
//
// Off unless asked for at launch (-sfve_background) or by the player's saved
// Play in the background (dllmain, as the game starts); it changes nothing
// else. In exclusive full screen (the game's own Screen Mode setting, see
// mod/ScreenMode.hxx) it lets the engine hear that it lost the focus, as the
// game does unmodded: Windows takes such a window out of full screen then.

#include "mod/GameHooks.hxx"

namespace sfve::mod {

// Hooks the three window queries as one set (CommitHookSet) and starts the
// window filter. Empty when they are in; otherwise the hook that failed, and
// none is.
std::optional<HookFailure> InstallBackgroundPlay();

} // namespace sfve::mod
