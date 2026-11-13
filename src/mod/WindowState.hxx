#pragma once

// What the overlay learns about the game's window that other parts of the mod
// need, without those parts reaching into the overlay: whether the game's swap
// chain is in exclusive full screen (the game's own Screen Mode setting, which
// Ember's Screen mode "Game's setting" leaves alone). Set on the render
// thread every Present (overlay/Overlay.cxx), read on any thread.

#include <atomic>

namespace sfve::mod {

inline std::atomic<bool> g_exclusiveFullscreen{false};

inline bool ExclusiveFullscreen()
{
	return g_exclusiveFullscreen.load(std::memory_order_relaxed);
}

inline void SetExclusiveFullscreen(bool exclusive)
{
	g_exclusiveFullscreen.store(exclusive, std::memory_order_relaxed);
}

} // namespace sfve::mod
