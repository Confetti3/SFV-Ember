// xinput1_3.dll proxy, placed next to StreetFighterV.exe.
//
// Two jobs:
// * forward every XInput export to the system's real XInput;
// * start sfv_ember.dll before any game code runs. When Windows loads this
//   proxy as one of the exe's imports, the exe has not started yet: the proxy
//   points the exe's entry point at EntryHook, which puts the original bytes
//   back, starts the mod (outside the loader lock) and then runs the game.
//   Loaded later by LoadLibrary, it starts the mod from a thread and the mod
//   logs that it started late.
//
// If sfv_ember.dll is missing or refuses to start, the game is not run: a
// proxy without its mod would be a game without its network guard.

#include <windows.h>

#include <stdint.h>
#include <string.h>

namespace {

HMODULE s_self;
HMODULE s_real;
uint8_t* s_entry;
uint8_t s_savedEntry[12];

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

void Refuse(const wchar_t* why)
{
	MessageBoxW(nullptr, why, L"SFV Ember", MB_OK | MB_ICONERROR);
	TerminateProcess(GetCurrentProcess(), 0x5F0E0002);
}

void StartMod(int beforeGameCode)
{
	wchar_t path[MAX_PATH];
	const DWORD n = GetModuleFileNameW(s_self, path, MAX_PATH);
	wchar_t* slash = n ? wcsrchr(path, L'\\') : nullptr;
	if (!slash || (slash - path) + 16 >= MAX_PATH) {
		Refuse(L"SFV Ember could not find its own folder. The game was not started.");
		return;
	}
	wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"sfv_ember.dll");
	// Launcher.exe injected the mod from its package, and that copy starts
	// from its own entry hook (src/mod/EarlyStart.cxx): this proxy loads no
	// second one. A second copy would only stay inert (src/mod/StartClaim.hxx).
	if (HMODULE injected = GetModuleHandleW(L"sfv_ember.dll")) {
		wchar_t injectedPath[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(injected, injectedPath, MAX_PATH);
		if (length && length < MAX_PATH && _wcsicmp(injectedPath, path) != 0) return;
	}
	HMODULE mod = LoadLibraryW(path);
	using Start = int (*)(int);
	Start start = mod ? reinterpret_cast<Start>(GetProcAddress(mod, "SfveStart")) : nullptr;
	if (!start) {
		Refuse(L"sfv_ember.dll is missing next to the game. The game was not started, so it cannot go online "
			   L"unguarded. Put the file back, or delete xinput1_3.dll from the game folder to play without SFV Ember.");
		return;
	}
	if (!start(beforeGameCode)) Refuse(L"SFV Ember refused to start; see %APPDATA%\\SFVEmber\\logs\\sfv_ember.log.");
}

DWORD WINAPI EntryHook(void* peb)
{
	DWORD old = 0;
	VirtualProtect(s_entry, sizeof(s_savedEntry), PAGE_EXECUTE_READWRITE, &old);
	memcpy(s_entry, s_savedEntry, sizeof(s_savedEntry));
	VirtualProtect(s_entry, sizeof(s_savedEntry), old, &old);
	FlushInstructionCache(GetCurrentProcess(), s_entry, sizeof(s_savedEntry));
	StartMod(1);
	return reinterpret_cast<DWORD(WINAPI*)(void*)>(s_entry)(peb);
}

DWORD WINAPI LateStart(void*)
{
	StartMod(0);
	return 0;
}

bool HookEntryPoint()
{
	auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.AddressOfEntryPoint) return false;
	s_entry = base + nt->OptionalHeader.AddressOfEntryPoint;

	// mov rax, EntryHook; jmp rax
	uint8_t patch[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
	const uintptr_t target = reinterpret_cast<uintptr_t>(&EntryHook);
	memcpy(patch + 2, &target, sizeof(target));

	DWORD old = 0;
	if (!VirtualProtect(s_entry, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(s_savedEntry, s_entry, sizeof(s_savedEntry));
	memcpy(s_entry, patch, sizeof(patch));
	VirtualProtect(s_entry, sizeof(patch), old, &old);
	FlushInstructionCache(GetCurrentProcess(), s_entry, sizeof(patch));
	return true;
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
	if (reserved && HookEntryPoint()) return TRUE;
	HANDLE thread = CreateThread(nullptr, 0, LateStart, nullptr, 0, nullptr);
	if (thread) CloseHandle(thread);
	return TRUE;
}
