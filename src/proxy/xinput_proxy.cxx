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

#include <optional>

#include "../mod/GuardedStart.hxx"
#include "XInputReentry.hxx"

namespace {

using sfve::start::Refusal;

HMODULE s_self;
HMODULE s_real;
HMODULE s_fallback;

HMODULE LoadSystemLibrary(const wchar_t* name)
{
	wchar_t path[MAX_PATH];
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (n == 0 || n + wcslen(name) >= MAX_PATH) return nullptr;
	wcscat_s(path, name);
	return LoadLibraryW(path);
}

// System32's XInput, by full path so that this proxy is not what is loaded.
HMODULE RealXInput()
{
	if (s_real) return s_real;
	for (const wchar_t* name : {L"\\xinput1_3.dll", L"\\xinput1_4.dll", L"\\xinput9_1_0.dll"}) {
		if ((s_real = LoadSystemLibrary(name)) != nullptr) break;
	}
	return s_real;
}

// Where a call that came back in through a tool that hooks XInput goes.
HMODULE FallbackXInput()
{
	if (!s_fallback) s_fallback = LoadSystemLibrary(L"\\xinput1_4.dll");
	return s_fallback;
}

// The real function `nameOrOrdinal` for a call at the depth `scope` records,
// or null when there is none to call (sfve::proxy::XInputReentry.hxx).
FARPROC Real(const sfve::proxy::ReentryScope& scope, const char* nameOrOrdinal)
{
	HMODULE real = nullptr;
	switch (scope.Route()) {
	case sfve::proxy::XInputRoute::Primary: real = RealXInput(); break;
	case sfve::proxy::XInputRoute::Fallback: real = FallbackXInput(); break;
	case sfve::proxy::XInputRoute::None: break;
	}
	return real ? GetProcAddress(real, nameOrOrdinal) : nullptr;
}

// The real function's answer to `args`, or nothing when it is not there.
template <typename... Args>
std::optional<DWORD> CallReal(const char* nameOrOrdinal, Args... args)
{
	const sfve::proxy::ReentryScope scope;
	auto f = reinterpret_cast<DWORD(WINAPI*)(Args...)>(Real(scope, nameOrOrdinal));
	if (!f) return std::nullopt;
	return f(args...);
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
	return CallReal("XInputGetState", user, state).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputSetState(DWORD user, void* vibration)
{
	return CallReal("XInputSetState", user, vibration).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, void* caps)
{
	return CallReal("XInputGetCapabilities", user, flags, caps).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

void WINAPI XInputEnable(BOOL enable)
{
	const sfve::proxy::ReentryScope scope;
	auto f = reinterpret_cast<void(WINAPI*)(BOOL)>(Real(scope, "XInputEnable"));
	if (f) f(enable);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
	return CallReal("XInputGetDSoundAudioDeviceGuids", user, render, capture).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE type, void* info)
{
	return CallReal("XInputGetBatteryInformation", user, type, info).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, void* keystroke)
{
	return CallReal("XInputGetKeystroke", user, reserved, keystroke).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

// Ordinal 100: the state with the Guide button. Without it the plain state.
DWORD WINAPI XInputGetStateEx(DWORD user, void* state)
{
	if (const std::optional<DWORD> answer = CallReal(MAKEINTRESOURCEA(100), user, state)) return *answer;
	return XInputGetState(user, state);
}

DWORD WINAPI XInputWaitForGuideButton(DWORD user, DWORD flags, void* listen)
{
	return CallReal(MAKEINTRESOURCEA(101), user, flags, listen).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD user)
{
	return CallReal(MAKEINTRESOURCEA(102), user).value_or(ERROR_DEVICE_NOT_CONNECTED);
}

DWORD WINAPI XInputPowerOffController(DWORD user)
{
	return CallReal(MAKEINTRESOURCEA(103), user).value_or(ERROR_DEVICE_NOT_CONNECTED);
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
