// sfv_ember.dll started by Launcher.exe (src/launcher/SfvLaunch.cxx).
//
// The launcher starts StreetFighterV.exe suspended and Detours adds this DLL
// to the exe's imports, so Windows loads it with the game's own DLLs, before
// the exe's entry point runs. That entry point is SteamStub's (the .bind
// section; docs/design/EMBER_LIBRARY.md section 22), which checks Steam and
// then runs the game. The mod must start before it, outside the loader lock,
// as it does under the xinput1_3.dll proxy (src/proxy): so this file's static
// initializer, which runs in the DLL's process attach under the loader lock,
// only points the entry at the start (mod/GuardedStart.hxx), which runs
// SfveStart(1) and then the entry point. If the entry cannot be pointed there,
// the process ends: nothing else would start the guard before the game.
//
// It does so only when the launcher started the game (-sfve_loader=launcher);
// loaded by the proxy, the proxy starts the mod itself.

#include "mod/EarlyStart.hxx"

#include "mod/GuardedStart.hxx"

#include <windows.h>

#include <wchar.h>

extern "C" int SfveStart(int beforeGameCode);

namespace sfve::mod {

namespace {

bool s_byEntryHook;

void StartFromEntry()
{
	s_byEntryHook = true;
	// A refused start has already ended the process (the network guard).
	SfveStart(1);
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

// Runs once, in this DLL's process attach. A copy the proxy loads in a game
// the launcher started hooks the entry too; the hooks chain, and whichever
// start runs second finds the mod started and only passes on.
struct EarlyStart {
	EarlyStart()
	{
		if (StartedByLauncher()) start::StartAtEntry(&StartFromEntry);
	}
} s_earlyStart;

} // namespace

bool StartedByEntryHook()
{
	return s_byEntryHook;
}

} // namespace sfve::mod
