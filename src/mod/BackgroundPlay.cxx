#include "mod/BackgroundPlay.hxx"

#include "mod/Log.hxx"
#include "mod/WindowState.hxx"

#include <windows.h>

#include <atomic>

namespace sfve::mod {

namespace {

using WindowQuery_t = HWND(WINAPI*)();
WindowQuery_t o_GetForegroundWindow;
WindowQuery_t o_GetActiveWindow;
WindowQuery_t o_GetFocus;
WNDPROC s_gameWndProc;
// The window the filter is on, once it is: every focus query then compares
// instead of searching the process's windows.
std::atomic<HWND> s_gameWindow{nullptr};

bool OwnedByThisProcess(HWND hwnd)
{
	DWORD pid = 0;
	return hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid == GetCurrentProcessId();
}

} // namespace

int SelectGameWindow(const std::vector<WindowCandidate>& windows)
{
	int best = -1;
	for (size_t i = 0; i < windows.size(); i++) {
		const WindowCandidate& window = windows[i];
		if (window.className != L"UnrealWindow" || !window.visible) continue;
		if (best < 0 || window.area > windows[best].area) best = (int)i;
	}
	return best;
}

namespace {

// The game's window (SelectGameWindow) among this process's, or null before
// it exists.
HWND OwnMainWindow()
{
	struct Search {
		std::vector<HWND> handles;
		std::vector<WindowCandidate> windows;
	} search;
	EnumWindows(
		[](HWND hwnd, LPARAM param) -> BOOL {
			auto* s = reinterpret_cast<Search*>(param);
			RECT r;
			wchar_t name[64] = {};
			if (OwnedByThisProcess(hwnd) && GetClassNameW(hwnd, name, 64) && GetWindowRect(hwnd, &r)) {
				s->handles.push_back(hwnd);
				s->windows.push_back({name, IsWindowVisible(hwnd) != 0, (r.right - r.left) * (r.bottom - r.top)});
			}
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&search));
	const int chosen = SelectGameWindow(search.windows);
	return chosen < 0 ? nullptr : search.handles[chosen];
}

// Every "which window has the user's attention" query answers with ours.
template <WindowQuery_t* Original>
HWND WINAPI OwnWindowFirst()
{
	const HWND real = (*Original)();
	if (OwnedByThisProcess(real)) return real;
	HWND own = s_gameWindow.load(std::memory_order_acquire);
	if (!own) own = OwnMainWindow();
	return own ? own : real;
}

// The engine never hears that it lost the focus.
bool IsFocusLoss(UINT message, WPARAM wParam)
{
	switch (message) {
	case WM_ACTIVATEAPP: return wParam == FALSE;
	case WM_ACTIVATE: return LOWORD(wParam) == WA_INACTIVE;
	case WM_KILLFOCUS: return true;
	default: return false;
	}
}

LRESULT CALLBACK KeepActiveWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// In exclusive full screen (the game's own Screen Mode) Windows takes the
	// chain out of it when the window loses the focus; an engine that never
	// heard of the loss would go on drawing into a chain that is gone. There
	// the focus loss is the engine's, as in the game unmodded.
	if (IsFocusLoss(message, wParam) && !ExclusiveFullscreen()) return 0;
	return CallWindowProcW(s_gameWndProc, hwnd, message, wParam, lParam);
}

// Waits for the game window (its class, not the splash screen's), puts the filter in front of its window procedure
// and tells it once that it is active (it may have lost the focus already).
DWORD WINAPI SubclassWhenReady(void*)
{
	for (int attempt = 0; attempt < 600; attempt++) {
		if (const HWND window = OwnMainWindow()) {
			s_gameWndProc = reinterpret_cast<WNDPROC>(
				SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeepActiveWndProc)));
			if (!s_gameWndProc || GetWindowLongPtrW(window, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&KeepActiveWndProc)) {
				Log("background play: the game window %p did not take the filter (Win32 %lu); the engine hears focus changes", (void*)window,
					GetLastError());
				return 1;
			}
			s_gameWindow.store(window, std::memory_order_release);
			PostMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
			PostMessageW(window, WM_ACTIVATE, WA_ACTIVE, 0);
			PostMessageW(window, WM_SETFOCUS, 0, 0);
			Log("background play: game window %p keeps the focus", (void*)window);
			return 0;
		}
		Sleep(100);
	}
	Log("background play: no game window after 60 s");
	return 1;
}

} // namespace

std::optional<HookFailure> InstallBackgroundPlay()
{
	// user32 is loaded: the mod imports it. All three queries or none: with
	// only some answering with the game's window, the engine would see itself
	// in front and not active.
	const HMODULE user32 = GetModuleHandleW(L"user32.dll");
	HookTransaction hooks;
	hooks.Add("GetForegroundWindow", (void*)GetProcAddress(user32, "GetForegroundWindow"),
		(void*)OwnWindowFirst<&o_GetForegroundWindow>, (void**)&o_GetForegroundWindow);
	hooks.Add("GetActiveWindow", (void*)GetProcAddress(user32, "GetActiveWindow"),
		(void*)OwnWindowFirst<&o_GetActiveWindow>, (void**)&o_GetActiveWindow);
	hooks.Add("GetFocus", (void*)GetProcAddress(user32, "GetFocus"), (void*)OwnWindowFirst<&o_GetFocus>,
		(void**)&o_GetFocus);
	const std::optional<HookFailure> missing = CommitHookSet(hooks, "background play");
	if (!missing) {
		HANDLE thread = CreateThread(nullptr, 0, SubclassWhenReady, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
	}
	Log("background play: %s", missing ? "off: the game reads its pads only while it is in front" : "on");
	return missing;
}

} // namespace sfve::mod
