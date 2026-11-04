#pragma once

// The host for Ember's interface on SFV: ImGui drawn into the game's own
// frames, with keyboard, mouse and pads routed to it. The interface itself
// (Ember's shell, from the shared Ember library) is plugged in with
// SetOverlayUi; this file is only the SFV side: the renderer and input.
//
// It opens with F1 or the pad's View/Back button once an interface is set;
// while it is open the game sees no keyboard, mouse or pad input.
//
// Drawing: SFV renders with Direct3D 11. The host hooks
// IDXGISwapChain::Present and ResizeBuffers (their code is shared by every
// swap chain, found through a throwaway one) and draws on the swap chain
// whose window is the game's, on the thread that presents.
//
// Threads: the game's window procedure and pad polling run on the game
// thread, Present on the engine's render thread. Window messages reach
// ImGui through a queue the render thread drains; ImGui is touched only on
// the render thread.

#include <functional>

namespace sfve::overlay {

struct OverlayUi {
	// Render thread, with the ImGui context current: before the first frame
	// and whenever the UI scale changes (1 at 900 px of height). Builds the
	// interface's fonts and style.
	std::function<void(float scale)> applyScale;
	// Render thread, inside an ImGui frame while the overlay is open; false
	// closes it.
	std::function<bool()> draw;
};

// Hooks the swap chain; false (logged) when it cannot.
bool InstallOverlay();

// The interface to show; until one is set the overlay does not open. Any
// thread, before or after the swap chain appears.
void SetOverlayUi(OverlayUi ui);

// The overlay's view of the game's pad polling: called for each pad state
// the game reads (VirtualPads' filter). Opens or closes the overlay on
// View/Back, feeds its navigation and, while it is open, clears the state so
// the game sees an idle pad.
void FilterGamePad(unsigned long user, void* xinputState);

} // namespace sfve::overlay
