#pragma once

// The game's way out, as a point the mod can still act in. A window close
// (Alt+F4, the title bar's close, Process.CloseMainWindow) reaches SFV's
// EXIT console command, which asks the engine to exit (sfv::kRequestExit)
// on the game thread; the engine then ends its main loop after that frame.
// The mod hooks that request, so whatever must leave gracefully (Ember's
// room, its helper, the settings' writer) does it there, with every thread
// still running, as SF4 Ember does in fMain::Destroy. Nothing is left for
// DLL detach, which runs under the loader lock after the other threads are
// gone.
//
// A graceful leave takes a few round trips (a room's Leave is sent and
// acknowledged), so the request runs the work's steps in place, about once
// a frame, until each says it is done or kExitWaitMs has passed, and only
// then lets the engine's exit go on. The game's own frames do not run
// meanwhile: once the window is closed, the engine no longer ticks the
// scenes (a held request that waited for them never ended, section 14). A
// request from another thread (a crash handler) waits for nothing.

#include <stdint.h>

#include <functional>

namespace sfve::mod {

// The longest the engine's exit waits for the work to be done.
constexpr uint32_t kExitWaitMs = 2500;

struct ExitWork {
	// Once, as the game asks to exit.
	std::function<void()> begin;
	// About once a frame after: one step of the work; true once it is done.
	std::function<bool()> step;
	// Once, done or not, just before the engine's exit goes on.
	std::function<void()> finish;
};

bool InstallGameExit(uintptr_t imageBase);

// Adds work for the game's exit. Game thread, before the exit.
void OnGameExit(ExitWork work);

} // namespace sfve::mod
