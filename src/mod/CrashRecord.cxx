#include "mod/CrashRecord.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/ModConfig.hxx"

#include "common/CrashDump.hxx"
#include "common/CrashReport.hxx"
#include "common/ProductIdentity.hxx"

#include <windows.h>
#include <dbghelp.h>

#include <MinHook.h>
#include <ggponet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <exception>

namespace sfve::mod {

namespace {

using DumpWriter = sf4e::crash::DumpWriter;

CrashRecordState s_state;
DumpWriter s_originalWriter = nullptr; // MinHook's trampoline once hooked
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
sf4e::crash::DumpClient s_dumpClient;
// The thread recording the process's one crash, 0 until one does: claimed
// once, so a fault on that same thread (inside the record) returns at once
// and one on another thread waits for it.
std::atomic<DWORD> s_recorder(0);
// Set once the first crash's record and dump are done; a second fault waits
// for it (at most kSecondFaultWaitMs, the launcher's whole budget and a bit).
HANDLE s_recorded = nullptr;
constexpr DWORD kSecondFaultWaitMs = sf4e::crash::DumpRequestWaitMs + 5000;
// Static, so a record needs neither the heap nor much stack.
wchar_t s_recordPath[MAX_PATH] = {};
wchar_t s_logsFolder[MAX_PATH] = {};
wchar_t s_dumpPath[sf4e::crash::DumpPathSize] = {};
char s_header[1024];
char s_module[MAX_PATH];
char s_detail[96];
char s_line[1200];

void RecordFirstCrash(const char* kind, EXCEPTION_POINTERS* pointers, const char* message, DWORD threadId);

DumpWriter Writer()
{
	return s_originalWriter ? s_originalWriter : &MiniDumpWriteDump;
}

// Names the module that holds `address` into s_module; its base, or 0.
uintptr_t ModuleBaseOf(uintptr_t address)
{
	s_module[0] = '\0';
	HMODULE module = nullptr;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(address), &module) ||
		!module) {
		return 0;
	}
	char path[MAX_PATH];
	if (GetModuleFileNameA(module, path, MAX_PATH)) {
		const char* name = strrchr(path, '\\');
		snprintf(s_module, sizeof(s_module), "%s", name ? name + 1 : path);
	}
	return reinterpret_cast<uintptr_t>(module);
}

sf4e::crash::CrashFacts FactsFor(const char* kind, const EXCEPTION_POINTERS* pointers, const char* message, DWORD threadId)
{
	sf4e::crash::CrashFacts facts = {kind, 0, 0, "", 0, threadId, message ? message : ""};
	if (!pointers || !pointers->ExceptionRecord) return facts;
	const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
	facts.code = record.ExceptionCode;
	facts.address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
	const uintptr_t base = ModuleBaseOf(static_cast<uintptr_t>(facts.address));
	facts.module = s_module;
	facts.moduleOffset = base ? facts.address - base : 0;
	if ((record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
		record.NumberParameters >= 2) {
		const ULONG_PTR access = record.ExceptionInformation[0];
		snprintf(s_detail, sizeof(s_detail), "%s of 0x%016llX", access == 0 ? "read" : access == 1 ? "write" : "execute",
			static_cast<unsigned long long>(record.ExceptionInformation[1]));
		facts.message = s_detail;
	}
	return facts;
}

void WriteText(HANDLE file, const char* text, size_t length)
{
	DWORD written = 0;
	WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
}

// The code of a fault that ends the game, which the game's reporter is
// dumping: an error (0xC...), a C++ exception nothing caught, a breakpoint,
// or 1, which the engine raises for its own fatal errors (an assertion,
// LowLevelFatalError, a fatal log line). Its other use, ensure, is compiled
// out of this shipping build: the exe has the fatal paths' texts and none of
// ensure's. 0 for anything else, which is not a crash.
DWORD FatalCode(const MINIDUMP_EXCEPTION_INFORMATION* exception)
{
	if (!exception || !exception->ExceptionPointers) return 0;
	EXCEPTION_RECORD record = {};
	if (exception->ClientPointers) {
		// The pointers are in this process anyway (the game reports itself);
		// read them as the target's, which tolerates a bad pointer.
		EXCEPTION_POINTERS pointers = {};
		SIZE_T read = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), exception->ExceptionPointers, &pointers, sizeof(pointers), &read) ||
			!pointers.ExceptionRecord ||
			!ReadProcessMemory(GetCurrentProcess(), pointers.ExceptionRecord, &record, sizeof(record), &read)) {
			return 0;
		}
	} else if (exception->ExceptionPointers->ExceptionRecord) {
		record = *exception->ExceptionPointers->ExceptionRecord;
	} else {
		return 0;
	}
	const DWORD code = record.ExceptionCode;
	return (code & 0xC0000000u) == 0xC0000000u || code == 0xE06D7363u || code == EXCEPTION_BREAKPOINT || code == 1 ? code : 0;
}

// The game's MiniDumpWriteDump: every crash its reporter handles comes here
// first, with the fault's pointers.
BOOL WINAPI OnMiniDumpWriteDump(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
	PMINIDUMP_EXCEPTION_INFORMATION exception, PMINIDUMP_USER_STREAM_INFORMATION user,
	PMINIDUMP_CALLBACK_INFORMATION callback)
{
	const DWORD code = processId == GetCurrentProcessId() ? FatalCode(exception) : 0;
	if (!code) return s_originalWriter(process, processId, file, type, exception, user, callback);
	RecordCrash("unhandled_exception", exception->ExceptionPointers, "the game's crash reporter", exception->ThreadId);
	const BOOL written = s_originalWriter(process, processId, file, type, exception, user, callback);
	// Without a launcher (the proxy's starts) nothing else would show the
	// crash: the reporter's own handling goes on.
	if (!s_state.launcherChannel) return written;
	// After its dump the reporter waits on an error box of its own, and the
	// launcher, which has the dump already, waits on the game. End it here so
	// the launcher sees the crash at once and offers its report: with the
	// fault's code when the launcher reads that as a crash, else with 3 (an
	// abort's).
	const UINT exitCode = sf4e::crash::IsCrashExit(code) ? code : 3;
	snprintf(s_line, sizeof(s_line), "crash: the game's own dump %s; ending the game with code 0x%08X",
		written ? "written" : "not written", exitCode);
	TryLogLine(s_line);
	TerminateProcess(GetCurrentProcess(), exitCode);
	return written;
}

// Threads the game did not start (the mod's, the helper link's, a driver's)
// are not guarded by its reporter: a fault there reaches this filter.
LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* pointers)
{
	// A filter that chains back to this one (it was set over ours, and ours
	// was then taken back over it) would otherwise call it forever.
	thread_local bool inside = false;
	if (inside) return EXCEPTION_CONTINUE_SEARCH;
	inside = true;
	RecordCrash("unhandled_exception", pointers, nullptr, GetCurrentThreadId());
	const LONG result = s_previousFilter ? s_previousFilter(pointers) : EXCEPTION_CONTINUE_SEARCH;
	inside = false;
	return result;
}

// The filter is the process's last one set; the engine may set its own
// after the mod starts. Taken back (chaining to the engine's) once its
// first frame runs.
void TakeFilter(const char* when)
{
	const LPTOP_LEVEL_EXCEPTION_FILTER current = SetUnhandledExceptionFilter(&OnUnhandledException);
	if (current == &OnUnhandledException) return;
	if (current) s_previousFilter = current;
	Log("crash record: the unhandled-exception filter %s%s", when, current ? ", chaining to the one that was set" : "");
}

// Heap corruption ends the process without reaching the reporter; ntdll
// raises it once first, and only a vectored handler sees that.
LONG CALLBACK OnVectoredException(EXCEPTION_POINTERS* pointers)
{
	if (sf4e::crash::IsHeapCorruption(pointers)) RecordCrash("heap_corruption", pointers, nullptr, GetCurrentThreadId());
	return EXCEPTION_CONTINUE_SEARCH;
}

void OnTerminate()
{
	RecordCrash("terminate", nullptr, "std::terminate", GetCurrentThreadId());
	abort();
}

void OnPureCall()
{
	RecordCrash("purecall", nullptr, "pure virtual call", GetCurrentThreadId());
}

// The handler is the shared C runtime's (ucrtbase), so every module of the
// process that uses it gets it. The default ends the process; so does this,
// after the record, with the status the default reports.
void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
	RecordCrash("invalid_parameter", nullptr, "invalid CRT parameter", GetCurrentThreadId());
	TerminateProcess(GetCurrentProcess(), STATUS_INVALID_CRUNTIME_PARAMETER);
}

// GGPO exits the process after an assertion; the record says which.
void __cdecl OnGgpoAssertion(const char* message)
{
	RecordCrash("ggpo_assertion", nullptr, message, GetCurrentThreadId());
}

bool HookDumpWriter()
{
	void* target = nullptr;
	MH_STATUS status = MH_CreateHookApiEx(L"dbghelp", "MiniDumpWriteDump", reinterpret_cast<void*>(&OnMiniDumpWriteDump),
		reinterpret_cast<void**>(&s_originalWriter), &target);
	if (status == MH_OK) status = MH_EnableHook(target);
	if (status != MH_OK) {
		Log("crash record: MiniDumpWriteDump not hooked (MinHook %d); the game's crashes leave only its own dump", (int)status);
		s_originalWriter = nullptr;
		return false;
	}
	return true;
}

// "crash game|thread": a write to an address nothing maps.
void Fault()
{
	*reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x10)) = 1;
}

DWORD WINAPI FaultThread(void*)
{
	Fault();
	return 0;
}

} // namespace

void RecordCrash(const char* kind, EXCEPTION_POINTERS* pointers, const char* message, DWORD threadId)
{
	const DWORD self = GetCurrentThreadId();
	DWORD recorder = 0;
	if (!s_recorder.compare_exchange_strong(recorder, self)) {
		// A second fault while the first is recorded, on another thread: wait
		// for its record and dump, so whatever ends the process next does not
		// cut them off.
		if (recorder != self && s_recorded) WaitForSingleObject(s_recorded, kSecondFaultWaitMs);
		return;
	}
	RecordFirstCrash(kind, pointers, message, threadId);
	if (s_recorded) SetEvent(s_recorded);
}

namespace {

void RecordFirstCrash(const char* kind, EXCEPTION_POINTERS* pointers, const char* message, DWORD threadId)
{
	// With the heap corrupt, anything that allocates may fault again and end
	// the process before the dump, so the launcher is asked first; the
	// request allocates nothing.
	const bool heapCorrupt = sf4e::crash::IsHeapCorruption(pointers);
	const bool dumpedFirst = heapCorrupt && s_dumpClient.Request(pointers, threadId);
	const sf4e::crash::CrashFacts facts = FactsFor(kind, pointers, message, threadId);
	const size_t headerLength = sf4e::crash::FormatCrashHeader(s_header, sizeof(s_header), facts);
	// The record holds this crash only: a report sends it, and one that also
	// carried earlier crashes would be grouped with them.
	if (s_recordPath[0]) {
		HANDLE file = CreateFileW(s_recordPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			WriteText(file, s_header, headerLength);
			CloseHandle(file);
		}
	}
	// The log, unless another thread holds it (it may be the one that faulted).
	snprintf(s_line, sizeof(s_line), "crash: %.*s", headerLength ? (int)headerLength - 1 : 0, s_header);
	TryLogLine(s_line);
	if (!pointers || dumpedFirst) return;
	if (!heapCorrupt && s_dumpClient.Request(pointers, threadId)) {
		TryLogLine("crash: the launcher wrote the dump");
		return;
	}
	if (!s_logsFolder[0]) return;
	const bool written = sf4e::crash::WriteDump(GetCurrentProcess(), GetCurrentProcessId(), s_logsFolder, threadId, pointers, false,
		s_dumpPath, Writer());
	TryLogLine(written ? "crash: wrote the dump in the game (no launcher)" : "crash: no dump was written");
}

} // namespace

CrashRecordState InstallCrashRecord(const CrashRecordSetup& setup)
{
	if (s_state.installed) return s_state;
	s_recorded = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!setup.logsFolder.empty()) {
		wcsncpy_s(s_logsFolder, setup.logsFolder.c_str(), _TRUNCATE);
		swprintf_s(s_recordPath, L"%s\\%s", s_logsFolder, ember::Product().crashLog);
	}
	if (setup.channel.Present()) {
		const HANDLE handles[] = {reinterpret_cast<HANDLE>(static_cast<uintptr_t>(setup.channel.request)),
			reinterpret_cast<HANDLE>(static_cast<uintptr_t>(setup.channel.done)),
			reinterpret_cast<HANDLE>(static_cast<uintptr_t>(setup.channel.mailbox))};
		// Inherited from the launcher, so inheritable: nothing the game or
		// the mod starts (the helper) gets them in turn.
		for (HANDLE handle : handles) SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0);
		s_state.launcherChannel = s_dumpClient.Configure(handles[0], handles[1], handles[2]);
		if (!s_state.launcherChannel) Log("crash record: the launcher's dump channel did not open (Win32 %lu)", GetLastError());
	}
	if (setup.hookDumpWriter) {
		s_state.dumpWriterHooked = HookDumpWriter();
		// Room to write the record from a stack overflow on the installing thread.
		ULONG reserve = 16 * 1024;
		SetThreadStackGuarantee(&reserve);
		AddVectoredExceptionHandler(1, OnVectoredException);
		TakeFilter("is set at the start");
		RunOnGameThread([] { TakeFilter("is taken back at the game's first frame"); });
		std::set_terminate(OnTerminate);
		_set_purecall_handler(OnPureCall);
		_set_invalid_parameter_handler(OnInvalidParameter);
		ggpo_set_assert_handler(OnGgpoAssertion);
	}
	s_state.installed = true;
	Log("crash record: %ls and %ls*.dmp in %ls; game's dump writer %s; dumps by %s", ember::Product().crashLog,
		ember::Product().crashDumpPrefix, s_logsFolder, s_state.dumpWriterHooked ? "hooked" : "not hooked",
		s_state.launcherChannel ? "the launcher" : "the game itself");
	return s_state;
}

void RegisterCrashCommand()
{
	RegisterStatus("crashRecord", [] {
		const CrashRecordState& state = s_state;
		return std::string("{\"installed\":") + (state.installed ? "true" : "false") +
			",\"dumpWriterHooked\":" + (state.dumpWriterHooked ? "true" : "false") +
			",\"launcherChannel\":" + (state.launcherChannel ? "true" : "false") + "}";
	});
	if (!Config().crashCommand) return;
	RegisterCommand("crash", [](const std::string& where) {
		if (where == "game") {
			Log("crash: faulting on the game thread, as asked on the pipe");
			RunOnGameThread(&Fault);
			return std::string("ok faulting on the game thread");
		}
		if (where == "thread") {
			Log("crash: faulting on a thread of the mod's, as asked on the pipe");
			const HANDLE thread = CreateThread(nullptr, 0, &FaultThread, nullptr, 0, nullptr);
			if (!thread) return std::string("error no thread");
			CloseHandle(thread);
			return std::string("ok faulting on a thread of the mod's");
		}
		return std::string("error crash game|thread");
	});
}

} // namespace sfve::mod
