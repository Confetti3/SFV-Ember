#include "mod/BackgroundPlay.hxx"

#include "mod/Log.hxx"
#include "mod/WindowState.hxx"

#include <windows.h>

namespace sfve::mod {

namespace {

using WindowQuery_t = HWND(WINAPI*)();
WindowQuery_t o_GetForegroundWindow;
WindowQuery_t o_GetActiveWindow;
WindowQuery_t o_GetFocus;
WNDPROC s_gameWndProc;

bool OwnedByThisProcess(HWND hwnd)
{
	DWORD pid = 0;
	return hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid == GetCurrentProcessId();
}

// The largest visible top-level window of this process: the game viewport.
HWND OwnMainWindow()
{
	struct Search {
		HWND best = nullptr;
		LONG bestArea = 0;
	} search;
	EnumWindows(
		[](HWND hwnd, LPARAM param) -> BOOL {
			auto* s = reinterpret_cast<Search*>(param);
			RECT r;
			if (OwnedByThisProcess(hwnd) && IsWindowVisible(hwnd) && GetWindowRect(hwnd, &r)) {
				const LONG area = (r.right - r.left) * (r.bottom - r.top);
				if (area > s->bestArea) {
					s->best = hwnd;
					s->bestArea = area;
				}
			}
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&search));
	return search.best;
}

// Every "which window has the user's attention" query answers with ours.
template <WindowQuery_t* Original>
HWND WINAPI OwnWindowFirst()
{
	const HWND real = (*Original)();
	if (OwnedByThisProcess(real)) return real;
	const HWND own = OwnMainWindow();
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

// Waits for the game window, puts the filter in front of its window procedure
// and tells it once that it is active (it may have lost the focus already).
DWORD WINAPI SubclassWhenReady(void*)
{
	for (int attempt = 0; attempt < 600; attempt++) {
		if (const HWND window = OwnMainWindow()) {
			s_gameWndProc = reinterpret_cast<WNDPROC>(
				SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&KeepActiveWndProc)));
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
