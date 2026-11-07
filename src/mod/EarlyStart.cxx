// sfv_ember.dll started by Launcher.exe (src/launcher/SfvLaunch.cxx).
//
// The launcher starts StreetFighterV.exe suspended and Detours adds this DLL
// to the exe's imports, so Windows loads it with the game's own DLLs, before
// the exe's entry point runs. That entry point is SteamStub's (the .bind
// section; docs/design/EMBER_LIBRARY.md section 22), which checks Steam and
// then runs the game. The mod must start before it, outside the loader lock,
// as it does under the xinput1_3.dll proxy (src/proxy): so this file's static
// initializer, which runs in the DLL's process attach under the loader lock,
// only points the entry at EntryHook (12 bytes). EntryHook puts the bytes
// back, runs SfveStart(1) and then the entry point.
//
// It does so only when the launcher started the game (-sfve_loader=launcher);
// loaded by the proxy, the proxy starts the mod itself. Each patch saves the
// bytes it replaced, so a second hook on the same entry (the proxy's, still
// deployed beside the game) runs after this one and restores the original.

#include "mod/EarlyStart.hxx"

#include <windows.h>

#include <stdint.h>
#include <string.h>
#include <wchar.h>

extern "C" int SfveStart(int beforeGameCode);

namespace sfve::mod {

namespace {

uint8_t* s_entry;
uint8_t s_savedEntry[12];
bool s_byEntryHook;

DWORD WINAPI EntryHook(void* peb)
{
	DWORD old = 0;
	VirtualProtect(s_entry, sizeof(s_savedEntry), PAGE_EXECUTE_READWRITE, &old);
	memcpy(s_entry, s_savedEntry, sizeof(s_savedEntry));
	VirtualProtect(s_entry, sizeof(s_savedEntry), old, &old);
	FlushInstructionCache(GetCurrentProcess(), s_entry, sizeof(s_savedEntry));
	s_byEntryHook = true;
	// A refused start has already ended the process (the network guard).
	SfveStart(1);
	return reinterpret_cast<DWORD(WINAPI*)(void*)>(s_entry)(peb);
}

// True when the command line holds `-sfve_loader=launcher` as a whole word.
bool StartedByLauncher()
{
	const wchar_t* line = GetCommandLineW();
	static const wchar_t kFlag[] = L"-sfve_loader=launcher";
	const size_t n = wcslen(kFlag);
	for (const wchar_t* p = line ? wcsstr(line, kFlag) : nullptr; p; p = wcsstr(p + n, kFlag)) {
		const bool startsWord = p == line || p[-1] == L' ' || p[-1] == L'\t';
		const bool endsWord = p[n] == 0 || p[n] == L' ' || p[n] == L'\t';
		if (startsWord && endsWord) return true;
	}
	return false;
}

bool HookEntryPoint()
{
	auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
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

// Runs once, in this DLL's process attach. A copy the proxy loads in a game
// the launcher started hooks the entry too; the hooks chain, and that copy's
// SfveStart has already run, so its own hook only passes on.
struct EarlyStart {
	EarlyStart()
	{
		if (StartedByLauncher()) HookEntryPoint();
	}
} s_earlyStart;

} // namespace

bool StartedByEntryHook()
{
	return s_byEntryHook;
}

} // namespace sfve::mod
