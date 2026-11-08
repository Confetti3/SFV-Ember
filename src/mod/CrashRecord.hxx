#pragma once

// SFV Ember's crash record (EMBER_LIBRARY.md section 29): what a crash of the
// game leaves for the player's report and for us. SFV's own crash reporter
// (Unreal's) handles every crash the game catches: its guarded main loop's
// and its unhandled-exception filter's both end in one call of the
// MiniDumpWriteDump it imports, writing Dump<n>.dmp under the game's
// Saved\Logs. The mod hooks that function, so each crash, on whichever thread
// the reporter runs, reaches it with the fault's exception pointers. There it
//   1. writes the record, Product().crashLog in the logs folder: the fault,
//      its module and offset, the faulting thread (one record per process,
//      from static buffers, never the log's lock);
//   2. asks the launcher for the dump (common/CrashDump.hxx), named after the
//      faulting thread, and waits for it; the launcher writes it with its own
//      heap, as Product().crashDumpPrefix + time + pid in the logs folder;
//   3. without a launcher (the proxy's starts, tests) writes that dump
//      itself, through the original MiniDumpWriteDump;
//   4. lets the game's own call go on, so its Dump<n>.dmp is written as before;
//   5. ends the game with the fault's code. The reporter would otherwise wait
//      on an error box of its own, and the launcher on the game, so the
//      player saw no crash window until they closed that box.
// Threads the game did not start (the mod's own among them) are not guarded
// by its reporter: the mod's unhandled-exception filter records a fault
// there and chains to the filter it replaced. It is set at the start and
// taken back at the game's first frame, in case the engine set its own.
// Heap corruption ends the process without reaching any filter; ntdll raises
// it once, so a vectored handler asks the launcher first, then records it.
// The mod's own C runtime faults (a pure call, an invalid parameter,
// std::terminate on the thread that installed the record) and a GGPO
// assertion are recorded too. The game's C runtime (MSVCR120) is its own and
// is not hooked.

#include "mod/ModConfig.hxx"

#include <windows.h>

#include <string>

namespace sfve::mod {

struct CrashRecordSetup {
	std::wstring logsFolder;   // where the record and an in-process dump go
	DumpChannelHandles channel;
	bool hookDumpWriter = true; // false in tests: nothing in the process is hooked
};

// What InstallCrashRecord could set up, for the log and the pipe's status.
struct CrashRecordState {
	bool installed = false;    // the handlers and the record's path
	bool dumpWriterHooked = false;
	bool launcherChannel = false; // the launcher's channel is open
};

// Once, after the log is open and MinHook is initialized (the network guard).
// A second call returns the first's state.
CrashRecordState InstallCrashRecord(const CrashRecordSetup& setup);

// Records a crash, kind as CrashReport.hxx names them, and takes its dump:
// the hook's path, callable on its own (tests). Once per process; later
// calls return at once. `pointers` may be null (a CRT fault: no dump).
void RecordCrash(const char* kind, EXCEPTION_POINTERS* pointers, const char* message, DWORD threadId);

// The pipe's status field "crashRecord" (what InstallCrashRecord set up), and
// its "crash game|thread" (only with -sfve_crash_command=1): faults
// on the game thread, or on a thread of the mod's own, to try the record end
// to end.
void RegisterCrashCommand();

} // namespace sfve::mod
