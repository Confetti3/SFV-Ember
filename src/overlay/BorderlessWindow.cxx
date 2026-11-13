#include "overlay/BorderlessWindow.hxx"

#include "mod/Log.hxx"

#include <atomic>

namespace sfve::overlay {

namespace {

using mod::Log;

std::atomic<bool> s_on{false};
std::atomic<bool> s_checkPending{false};
std::atomic<unsigned> s_presents{0};
std::atomic<int> s_applied{0};
std::atomic<bool> s_gaveUp{false};
std::atomic<bool> s_exclusive{false};

// The window's own thread only, from here down.
unsigned long long s_lastApplyMs = 0;

constexpr unsigned kCheckEvery = 30; // Presents: about half a second
constexpr int kApplyLimit = 40;
constexpr int kLoggedApplies = 3;

UINT CheckMessage()
{
	// Registered by name, so no message the game uses can be mistaken for it.
	static const UINT message = RegisterWindowMessageW(L"SFVEmberBorderlessCheck");
	return message;
}

// "unaware", "system" or "per-monitor": how Windows treats this window's
// pixels, read from user32 when it has the call (Windows 10 1607 and later).
const char* DpiAwareness(HWND window)
{
	using GetContext = HANDLE(WINAPI*)(HWND);
	using GetAwareness = int(WINAPI*)(HANDLE);
	const HMODULE user32 = GetModuleHandleW(L"user32.dll");
	const auto context = reinterpret_cast<GetContext>(user32 ? GetProcAddress(user32, "GetWindowDpiAwarenessContext") : nullptr);
	const auto awareness = reinterpret_cast<GetAwareness>(user32 ? GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext") : nullptr);
	if (!context || !awareness) return "unknown";
	switch (awareness(context(window))) {
	case 0: return "unaware";
	case 1: return "system";
	case 2: return "per-monitor";
	default: return "unknown";
	}
}

void Fit(HWND window)
{
	if (!IsWindowVisible(window) || IsIconic(window)) return;
	const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	RECT now{};
	if (!monitor || !GetMonitorInfoW(monitor, &info) || !GetWindowRect(window, &now)) return;
	const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
	const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
	if (fit::Fits(style, exStyle, now, info.rcMonitor)) return;
	const unsigned long long ms = GetTickCount64();
	if (!fit::MayApply(ms, s_lastApplyMs, s_applied, 250, kApplyLimit)) {
		if (s_applied >= kApplyLimit && !s_gaveUp.exchange(true))
			Log("borderless: the game restyled its window %d times; leaving it as it is now", kApplyLimit);
		return;
	}
	s_lastApplyMs = ms;
	const int count = ++s_applied;
	SetWindowLongPtrW(window, GWL_STYLE, fit::BorderlessStyle(style));
	SetWindowLongPtrW(window, GWL_EXSTYLE, fit::BorderlessExStyle(exStyle));
	const RECT& to = info.rcMonitor;
	// SWP_FRAMECHANGED makes the system recompute the (now absent) frame;
	// the window is neither activated nor reordered.
	const BOOL placed = SetWindowPos(window, nullptr, to.left, to.top, to.right - to.left, to.bottom - to.top,
		SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	if (count > kLoggedApplies) return; // a window the game keeps restyling is not logged each time
	DEVMODEW mode{};
	mode.dmSize = sizeof(mode);
	const bool haveMode = EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) != FALSE;
	RECT after{}, client{};
	GetWindowRect(window, &after);
	GetClientRect(window, &client);
	Log("borderless: #%d window %p style %08lx -> %08lx, (%ld,%ld)-(%ld,%ld) -> (%ld,%ld)-(%ld,%ld), client %ldx%ld%s; monitor "
		"(%ld,%ld)-(%ld,%ld), mode %ux%u, window is %s",
		count, (void*)window, (unsigned long)style, (unsigned long)GetWindowLongPtrW(window, GWL_STYLE), now.left, now.top, now.right,
		now.bottom, after.left, after.top, after.right, after.bottom, client.right - client.left, client.bottom - client.top,
		placed ? "" : " (SetWindowPos failed)", to.left, to.top, to.right, to.bottom, haveMode ? (unsigned)mode.dmPelsWidth : 0u,
		haveMode ? (unsigned)mode.dmPelsHeight : 0u, DpiAwareness(window));
}

} // namespace

void SetBorderless(bool on)
{
	s_on = on;
	if (on) CheckMessage(); // registered now, not on the first Present
	Log("borderless: %s", on ? "on: the game's window will be laid over its whole monitor without a frame" : "off");
}

bool BorderlessChosen()
{
	return s_on;
}

void BorderlessPresented(HWND window, bool exclusiveFullscreen)
{
	if (!s_on) return;
	if (exclusiveFullscreen != s_exclusive.exchange(exclusiveFullscreen))
		Log("borderless: the swap chain is %s", exclusiveFullscreen ? "in exclusive full screen; the window is left alone" : "windowed again");
	if (exclusiveFullscreen || s_presents++ % kCheckEvery != 0) return;
	// One request at a time; the window's thread answers it.
	if (!s_checkPending.exchange(true)) PostMessageW(window, CheckMessage(), 0, 0);
}

bool BorderlessMessage(HWND window, UINT message)
{
	if (!s_on || message != CheckMessage()) return false;
	s_checkPending = false;
	if (!s_exclusive) Fit(window);
	return true;
}

std::string BorderlessStatusJson()
{
	return std::string("{\"borderless\":") + (s_on ? "true" : "false") + ",\"applied\":" + std::to_string(s_applied.load()) +
		",\"gaveUp\":" + (s_gaveUp ? "true" : "false") + ",\"exclusive\":" + (s_exclusive ? "true" : "false") + "}";
}

} // namespace sfve::overlay
