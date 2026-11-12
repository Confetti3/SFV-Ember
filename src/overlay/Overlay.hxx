#pragma once

// The host for Ember's interface on SFV: ImGui drawn into the game's own
// frames, with keyboard, mouse and pads routed to it. The interface itself
// (Ember's shell, src/ember/SfvShell) is plugged in with SetOverlayUi; this
// file is only the SFV side: the renderer, its textures and the input.
//
// Drawing: SFV renders with Direct3D 11. The host hooks
// IDXGISwapChain::Present and ResizeBuffers (their code is shared by every
// swap chain, found through a throwaway one) and draws on the swap chain
// whose window is the game's, on the thread that presents. Once an interface
// is set, every presented frame is an ImGui frame: the interface runs its
// hidden work then too, and says whether it is shown.
//
// Threads: the game's window procedure and pad polling run on the game
// thread, Present on the engine's render thread. Window messages reach ImGui
// through Ember's Win32 input bridge, which the render thread applies at the
// start of its frame (ui/Win32InputBridge.hxx); ImGui is drawn only on the
// render thread. The ImGui context is made and its backends replaced under
// Ember's OverlayLifecycle, so a window message never meets a half-made one.
//
// Ember's pictures (ui/SelectionArt.hxx) load through a Direct3D 11 uploader
// on the game's device, from the mod's folder: assets/brand, assets/input and
// assets/flags, Ember's own art. SFV's fighters and stages are drawn from
// the game's own textures instead (ember/SfvArt), with nothing packaged.

#include <functional>

namespace sfve::overlay {

struct OverlayUi {
	// Render thread, with the ImGui context current, before each frame: the
	// interface's fonts and style at the picture's scale (1 at 1080 lines of
	// height). True when it rebuilt the font atlas, which is then uploaded again.
	std::function<bool(float scale)> applyScale;
	// Render thread, after the backends' NewFrame and before ImGui's: the
	// frame's input that is not the window's (the pads).
	std::function<void()> beginFrame;
	// Render thread, inside the ImGui frame, every frame the game presents.
	struct Shown {
		bool shown = false;     // the interface is on screen: the keys and the mouse are its own
		bool available = false; // F10 may show it now
	};
	std::function<Shown()> draw;
	// Game thread, for each pad state the game reads: the slot and its
	// XINPUT_GAMEPAD buttons. True keeps that state from the game, which then
	// sees an idle pad.
	std::function<bool(unsigned long slot, unsigned short buttons)> takesPad;
	// Game thread, for each message to the game's window, before ImGui and
	// the game see it: true keeps it from both (Training's go now on Enter,
	// SfvShell). Optional.
	std::function<bool(unsigned message, unsigned long long w, long long l)> takesMessage;
	// The window's thread, for each key message: true keeps it from the game
	// (ImGui has already seen it), as SF4 Ember keeps a replay's keys.
	std::function<bool(unsigned message, unsigned key)> takesKey;
};

// Hooks the swap chain's Present and ResizeBuffers as one set
// (mod::CommitHookSet); false (logged) when they are not in, and then neither
// is.
bool InstallOverlay();

// The interface to show; until one is set the overlay draws nothing and the
// game keeps every input. Any thread, before or after the swap chain appears.
void SetOverlayUi(OverlayUi ui);

// The game's window has the focus (WM_ACTIVATEAPP). Any thread.
bool GameWindowFocused();

// A function called on the render thread for every real Present (not
// DXGI_PRESENT_TEST) of any swap chain the hook sees, before the overlay draws
// and whether or not an interface is set: the photo booth's frame grab
// (overlay/FrameGrab.hxx) takes the clean back buffer there. `swapChain` is the
// IDXGISwapChain being presented, as an opaque pointer so this header needs no
// DXGI. Null clears it. Any thread; the tap runs on the render thread and must
// be quick. It only runs once InstallOverlay has hooked Present.
using PresentTap = std::function<void(void* swapChain)>;
void SetPresentTap(PresentTap tap);

// The overlay's view of the game's pad polling: called for each pad state
// the game reads (VirtualPads' filter). While the interface takes the pad
// (OverlayUi::takesPad) the game sees an idle one, and a button still held
// when it lets go stays hidden from the game until released.
void FilterGamePad(unsigned long user, void* xinputState);

} // namespace sfve::overlay
