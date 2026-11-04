#pragma once

// Work that must run on the game thread outside a battle (calling the game's
// own functions from the menus). Other threads queue it; the game thread runs
// it once a frame from the pad poll (UE4 polls XInput on the game thread,
// VirtualPads hooks it), so the queue drains in menus and battles alike.

#include <stdint.h>

#include <functional>

namespace sfve::mod {

void RunOnGameThread(std::function<void()> work);

// Runs work once a frame on the game thread until it returns false.
void RunEachGameFrame(std::function<bool()> work);

// Called by the pad poll hook, once per polled frame.
void PumpGameThread();

// The thread that last pumped the queue (0 before the first poll).
uint32_t GameThreadId();

} // namespace sfve::mod
