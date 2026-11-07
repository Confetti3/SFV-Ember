#pragma once

// Work that must run on the game thread (calling the game's own functions,
// reading its scene objects). Other threads queue it; the scene manager's
// tick (GameScene) runs it once a frame, before it ticks the current scene
// state, in menus and battles alike. So work runs between two frames of a
// state, never inside one; it must still not end a battle it runs in.

#include <stdint.h>

#include <functional>

namespace sfve::mod {

void RunOnGameThread(std::function<void()> work);

// Runs work once a frame on the game thread until it returns false.
void RunEachGameFrame(std::function<bool()> work);

// Called by the scene manager's tick, once a frame. A call from inside the
// work it runs returns at once.
void PumpGameThread();

// The game thread (0 before the first frame of the scene manager).
uint32_t GameThreadId();

// The game frames pumped so far (one per scene manager tick); any thread.
uint64_t GameFrameNumber();

} // namespace sfve::mod
