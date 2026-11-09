// xinput1_3.dll proxy, placed next to StreetFighterV.exe.
//
// Two jobs:
// * forward every XInput export to the system's real XInput;
// * start sfv_ember.dll before any game code runs. Windows loads this proxy
//   as one of the exe's imports, before the exe has started: the proxy points
//   the exe's entry point at its start (src/mod/GuardedStart.hxx), which
//   starts the mod outside the loader lock and then runs the game.
//
// The game runs only guarded. If the entry point cannot be pointed at the
// start, if sfv_ember.dll is missing or refuses to start, or if the proxy is
// loaded after the game started with no copy of the mod guarding it, the
// process ends: a proxy without its mod would be a game without its network
// guard.

#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "../mod/GuardedStart.hxx"

namespace {

using sfve::start::Refusal;

HMODULE s_self;
HMODULE s_real;

HMODULE RealXInput()
{
	if (s_real) return s_real;
	const wchar_t* names[] = {L"\\xinput1_3.dll", L"\\xinput1_4.dll", L"\\xinput9_1_0.dll"};
	wchar_t path[MAX_PATH];
	for (const wchar_t* name : names) {
		const UINT n = GetSystemDirectoryW(path, MAX_PATH);
		if (n == 0 || n + wcslen(name) >= MAX_PATH) break;
		wcscat_s(path, name);
		if ((s_real = LoadLibraryW(path)) != nullptr) break;
	}
	return s_real;
}

FARPROC Real(const char* nameOrOrdinal)
{
	HMODULE real = RealXInput();
	return real ? GetProcAddress(real, nameOrOrdinal) : nullptr;
}

// Outside the loader lock only: a refusal the player sees before the game
// closes.
[[noreturn]] void RefuseWithNotice(Refusal why, const wchar_t* notice)
{
	MessageBoxW(nullptr, notice, L"SFV Ember", MB_OK | MB_ICONERROR);
	sfve::start::Refuse(why, notice);
}

// The sfv_ember.dll beside this proxy; false when its path does not fit.
bool SiblingMod(wchar_t (&path)[MAX_PATH])
{
	const DWORD n = GetModuleFileNameW(s_self, path, MAX_PATH);
	wchar_t* slash = n && n < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
	if (!slash || (slash - path) + 16 >= MAX_PATH) return false;
	wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"sfv_ember.dll");
	return true;
}

// Launcher.exe injected the mod from its package: that copy starts from its
// own entry hook before the game (src/mod/EarlyStart.cxx), or has ended the
// process, so this proxy starts no second one. A second copy would only stay
// inert (src/mod/StartClaim.hxx).
bool LauncherCopyLoaded(const wchar_t* sibling)
{
	HMODULE injected = GetModuleHandleW(L"sfv_ember.dll");
	if (!injected) return false;
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(injected, path, MAX_PATH);
	return length && length < MAX_PATH && _wcsicmp(path, sibling) != 0;
}

// The start (sfve::start::Start), from the exe's entry point.
void StartMod()
{
	wchar_t path[MAX_PATH];
	if (!SiblingMod(path)) {
		RefuseWithNotice(Refusal::ModUnavailable, L"SFV Ember could not find its own folder. The game was not started.");
	}
	if (LauncherCopyLoaded(path)) return;
	HMODULE mod = LoadLibraryW(path);
	using Start = int (*)(int);
	Start start = mod ? reinterpret_cast<Start>(GetProcAddress(mod, "SfveStart")) : nullptr;
	if (!start) {
		RefuseWithNotice(Refusal::ModUnavailable,
			L"sfv_ember.dll is missing next to the game. The game was not started, so it cannot go online "
			L"unguarded. Put the file back, or delete xinput1_3.dll from the game folder to play without SFV Ember.");
	}
	if (!start(1)) {
		RefuseWithNotice(Refusal::ModUnavailable, L"SFV Ember refused to start; see %APPDATA%\\SFVEmber\\logs\\sfv_ember.log.");
	}
}

// Loaded by LoadLibrary, the game's code has already run: the proxy can no
// longer start the mod before it. That is right only when the launcher's copy
// guards this game from its own entry hook.
void CheckLateLoad()
{
	wchar_t path[MAX_PATH];
	if (SiblingMod(path) && LauncherCopyLoaded(path)) return;
	sfve::start::Refuse(Refusal::StartedLate, L"xinput1_3.dll was loaded after the game started, with no SFV Ember "
											  L"guarding it; the game cannot run unguarded");
}

} // namespace

extern "C" {

DWORD WINAPI XInputGetState(DWORD user, void* state)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(Real("XInputGetState"));
	return f ? f(user, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputSetState(DWORD user, void* vibration)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(Real("XInputSetState"));
	return f ? f(user, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, void* caps)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(Real("XInputGetCapabilities"));
	return f ? f(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

void WINAPI XInputEnable(BOOL enable)
{
	auto f = reinterpret_cast<void(WINAPI*)(BOOL)>(Real("XInputEnable"));
	if (f) f(enable);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, GUID*, GUID*)>(Real("XInputGetDSoundAudioDeviceGuids"));
	return f ? f(user, render, capture) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE type, void* info)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, BYTE, void*)>(Real("XInputGetBatteryInformation"));
	return f ? f(user, type, info) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, void* keystroke)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(Real("XInputGetKeystroke"));
	return f ? f(user, reserved, keystroke) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputGetStateEx(DWORD user, void* state)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, void*)>(Real(MAKEINTRESOURCEA(100)));
	return f ? f(user, state) : XInputGetState(user, state);
}

DWORD WINAPI XInputWaitForGuideButton(DWORD user, DWORD flags, void* listen)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD, DWORD, void*)>(Real(MAKEINTRESOURCEA(101)));
	return f ? f(user, flags, listen) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD user)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD)>(Real(MAKEINTRESOURCEA(102)));
	return f ? f(user) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI XInputPowerOffController(DWORD user)
{
	auto f = reinterpret_cast<DWORD(WINAPI*)(DWORD)>(Real(MAKEINTRESOURCEA(103)));
	return f ? f(user) : ERROR_DEVICE_NOT_CONNECTED;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
	if (reason != DLL_PROCESS_ATTACH) return TRUE;
	s_self = module;
	DisableThreadLibraryCalls(module);
	// A non-null `reserved` means a static load: the exe has not run yet.
	if (reserved) sfve::start::StartAtEntry(&StartMod);
	else CheckLateLoad();
	return TRUE;
}
