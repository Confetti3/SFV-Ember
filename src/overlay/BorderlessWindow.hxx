#pragma once

// The player's Screen mode Borderless (mod/ScreenMode.hxx, docs/design/
// beta9/screen.md): the game's window, started with -windowed, loses its
// title bar and border and covers the whole monitor it is on. The monitor's
// mode is never changed, so nothing else on the desktop is disturbed and
// Alt+Tab is the ordinary kind. The engine follows the window's new size on
// its own (the window's WM_SIZE resizes its back buffers, which the overlay
// already follows: Overlay.cxx's h_ResizeBuffers).
//
// - Who does what: the render thread only notices (every 30th Present it asks
//   the window to check itself, never touching the window), and the window's
//   own thread, in the overlay's window procedure, restyles and moves it, so
//   no call crosses threads into the window's message handling.
// - Re-applied: the engine restyles its window when the player changes its own
//   options (or presses Alt+Enter), so each check looks at the style and the
//   rectangle again, at most 4 times a second and 40 times a session, then
//   leaves the window alone and says so in the log.
// - Not applied: while the swap chain is in exclusive full screen (the
//   player chose that in the game while Borderless was on) or the window is
//   minimised.
// - Pixels: the monitor's rectangle is read on the window's thread, in that
//   thread's DPI awareness, and the window is placed with the same numbers.
//   A process that is per-monitor aware gets physical pixels; an unaware one
//   gets the system's scaled picture of the same monitor, which still covers
//   it. The log names the awareness and the monitor's real mode.

#include <windows.h>

#include <string>

namespace sfve::overlay {

namespace fit {

// What Borderless takes off the window: the title bar and border
// (WS_CAPTION), the sizing frame and the maximize box. The system menu and
// minimize box stay: invisible without a title bar, and Alt+F4 and the
// taskbar's own actions keep working.
inline constexpr LONG_PTR kFrameStyle = WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX;
inline constexpr LONG_PTR kEdgeStyle = WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;

inline LONG_PTR BorderlessStyle(LONG_PTR style) { return style & ~kFrameStyle; }
inline LONG_PTR BorderlessExStyle(LONG_PTR exStyle) { return exStyle & ~kEdgeStyle; }

// The window is as Borderless wants it: no frame or edge and exactly the
// monitor's rectangle (the system menu and minimize box do not count).
inline bool Fits(LONG_PTR style, LONG_PTR exStyle, const RECT& window, const RECT& monitor)
{
	return !(style & (WS_CAPTION | WS_THICKFRAME)) && !(exStyle & kEdgeStyle) && window.left == monitor.left &&
		window.top == monitor.top && window.right == monitor.right && window.bottom == monitor.bottom;
}

// Whether to act now on a window that does not fit: not within `quietMs` of
// the last time, and not after `limit` times.
inline bool MayApply(unsigned long long nowMs, unsigned long long lastMs, int applied, unsigned long long quietMs = 250, int limit = 40)
{
	return applied < limit && (applied == 0 || nowMs - lastMs >= quietMs);
}

} // namespace fit

// Once, from the mod's start, before the swap chain is hooked: Borderless
// is on for this copy (-sfve_screen=borderless).
void SetBorderless(bool on);
bool BorderlessChosen();

// Render thread, every real Present of the game's swap chain (Overlay's
// Draw); `exclusiveFullscreen` is the chain's own answer.
void BorderlessPresented(HWND window, bool exclusiveFullscreen);

// The overlay's window procedure, on the window's thread, for every message
// before the game's: true when it was Borderless's own request to check the
// window (done; the game must not see it).
bool BorderlessMessage(HWND window, UINT message);

// The status field "screen": {"borderless":true,"applied":N,"gaveUp":false,
// "exclusive":false}.
std::string BorderlessStatusJson();

} // namespace sfve::overlay
