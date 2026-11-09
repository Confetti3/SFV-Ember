#pragma once

// The start contract both of sfv_ember.dll's loaders keep: the xinput1_3.dll
// proxy (src/proxy) and the mod's own entry hook when Launcher.exe injected it
// (mod/EarlyStart.cxx). SFV must never reach Capcom's network, so the game's
// code runs only after the mod's network guard is in, and the guard goes in
// before the exe's entry point (SteamStub's, then the game's) runs anything:
//
// * at the loader's process attach, under the loader lock, StartAtEntry points
//   the exe's entry point at the loader's start (12 bytes, saved);
// * the entry puts those bytes back, runs the start, which installs the guard
//   through SfveStart or ends the process, and then the original entry.
//
// Whatever breaks that chain ends the process with its own exit code before
// any game code runs, and leaves a line in the mod's log when it can. Two
// loaders in one game (a launcher start with the proxy still deployed) chain:
// the patch applied second saved the first one's bytes, so its start runs
// first and the other's after it, and whichever start comes second finds the
// mod started and only passes on.
//
// Header only: the proxy and the mod are separate modules, each with its own
// copy of this state, and the proxy links nothing of the mod's.

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace sfve::start {

// Why the process ended before the game ran: its exit code.
enum class Refusal : UINT {
	GuardIncomplete = 0x5F0E0001,  // SfveStart could not install the network guard
	ModUnavailable = 0x5F0E0002,   // the proxy found no sfv_ember.dll to start, or the mod refused
	EntryPatchFailed = 0x5F0E0003, // a loader could not point the exe's entry point at its start, or put it back
	StartedLate = 0x5F0E0004,      // the mod was to start after game code had run
	HooksStuck = 0x5F0E0005,       // a hook set that failed could not be taken back out (mod/GameHooks.hxx, CommitHookSet)
};

// One line in the mod's log (mod/Log.hxx), in the same file and format, with
// kernel32 alone: a refusal under the loader lock comes before the mod's log
// is open, and the proxy has no log of its own. Log.cxx finds the folder
// through the shell, which must not load under the loader lock; %APPDATA% is
// the same folder. The instance is not known this early, so the line goes to
// instance 0's log, where a player looks.
inline void LogStartLine(const wchar_t* text)
{
	wchar_t path[MAX_PATH];
	const DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
	if (n == 0 || n + 32 >= MAX_PATH) return;
	wcscat_s(path, L"\\SFVEmber");
	CreateDirectoryW(path, nullptr);
	wcscat_s(path, L"\\logs");
	CreateDirectoryW(path, nullptr);
	wcscat_s(path, L"\\sfv_ember.log");
	SYSTEMTIME t;
	GetLocalTime(&t);
	char line[600];
	const int length = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %ls\r\n", t.wYear,
		t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), text);
	if (length <= 0 || length >= (int)sizeof(line)) return;
	const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return;
	DWORD written = 0;
	WriteFile(file, line, (DWORD)length, &written, nullptr);
	CloseHandle(file);
}

// Ends the process for `why`; the caller has logged the reason.
[[noreturn]] inline void EndProcess(Refusal why)
{
	TerminateProcess(GetCurrentProcess(), static_cast<UINT>(why));
	// Ending its own process, TerminateProcess does not return; this keeps
	// the promise if it ever did.
	ExitProcess(static_cast<UINT>(why));
}

// Logs `text` (LogStartLine) and ends the process for `why`.
[[noreturn]] inline void Refuse(Refusal why, const wchar_t* text)
{
	wchar_t line[400];
	// Cut short rather than the CRT's abort: the exit code must be `why`.
	_snwprintf_s(line, _TRUNCATE, L"start refused (exit code 0x%08X): %ls", static_cast<unsigned>(why), text);
	LogStartLine(line);
	EndProcess(why);
}

// A loader's start, run from the exe's entry point before any game code: it
// returns only with the network guard in (its own, or another copy's), and
// otherwise ends the process.
using Start = void (*)();

namespace detail {

constexpr size_t kPatchSize = 12;
inline Start s_start;
inline uint8_t* s_entry;
inline uint8_t s_saved[kPatchSize];

// Writes `bytes` over the entry point; false when its page cannot be made
// writable.
inline bool WriteEntry(const uint8_t* bytes)
{
	DWORD old = 0;
	if (!VirtualProtect(s_entry, kPatchSize, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(s_entry, bytes, kPatchSize);
	VirtualProtect(s_entry, kPatchSize, old, &old);
	FlushInstructionCache(GetCurrentProcess(), s_entry, kPatchSize);
	return true;
}

inline DWORD WINAPI EntryHook(void* peb)
{
	// Left patched, the entry would come back here instead of the game's.
	if (!WriteEntry(s_saved)) Refuse(Refusal::EntryPatchFailed, L"the exe's entry point could not be put back");
	s_start();
	return reinterpret_cast<DWORD(WINAPI*)(void*)>(s_entry)(peb);
}

enum class EntryPatch {
	Patched,
	NoEntryPoint, // the process's exe has no PE headers or no entry point
	NotWritable,
};

inline EntryPatch PatchEntry()
{
	auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return EntryPatch::NoEntryPoint;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || !nt->OptionalHeader.AddressOfEntryPoint) return EntryPatch::NoEntryPoint;
	s_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
	// mov rax, EntryHook; jmp rax
	uint8_t patch[kPatchSize] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
	const uintptr_t target = reinterpret_cast<uintptr_t>(&EntryHook);
	memcpy(patch + 2, &target, sizeof(target));
	memcpy(s_saved, s_entry, kPatchSize);
	return WriteEntry(patch) ? EntryPatch::Patched : EntryPatch::NotWritable;
}

} // namespace detail

// At a loader's process attach, before the exe's entry point has run: points
// that entry at `start`, or ends the process. Once per module.
inline void StartAtEntry(Start start)
{
	detail::s_start = start;
	switch (detail::PatchEntry()) {
	case detail::EntryPatch::Patched: return;
	case detail::EntryPatch::NoEntryPoint:
		Refuse(Refusal::EntryPatchFailed, L"the game's exe has no entry point to start SFV Ember from");
	case detail::EntryPatch::NotWritable:
		Refuse(Refusal::EntryPatchFailed, L"the exe's entry point could not be made writable");
	}
}

} // namespace sfve::start
