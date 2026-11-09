#include "SteamSession.hxx"

#include <windows.h>
#include <tlhelp32.h>

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

namespace sfve::launcher {

namespace {

constexpr wchar_t kActiveProcess[] = L"Software\\Valve\\Steam\\ActiveProcess";
constexpr DWORD kExitWaitMs = 60'000;   // the old client's exit after -shutdown
constexpr DWORD kRecordWaitMs = 90'000; // the new client's record and sign-in
constexpr DWORD kPollMs = 500;

struct Record {
	DWORD pid = 0;
	DWORD user = 0;
};

Record ReadRecord()
{
	Record record;
	DWORD size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, kActiveProcess, L"pid", RRF_RT_REG_DWORD, nullptr, &record.pid, &size);
	size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, kActiveProcess, L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &record.user, &size);
	return record;
}

// Every running steam.exe (the client; its helpers are steamwebhelper.exe
// and SteamService.exe, which do not count).
std::vector<DWORD> SteamProcesses()
{
	std::vector<DWORD> found;
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return found;
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
		if (lstrcmpiW(entry.szExeFile, L"steam.exe") == 0) found.push_back(entry.th32ProcessID);
	}
	CloseHandle(snapshot);
	return found;
}

bool Names(const std::vector<DWORD>& running, DWORD pid)
{
	for (const DWORD p : running) {
		if (p == pid) return true;
	}
	return false;
}

// The client Steam itself records for starting it again.
std::wstring SteamExecutable()
{
	wchar_t path[MAX_PATH] = {};
	DWORD size = sizeof(path);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe", RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS) {
		return {};
	}
	std::wstring exe = path;
	for (wchar_t& c : exe) {
		if (c == L'/') c = L'\\';
	}
	return exe;
}

// The explorer process that owns the desktop, opened to be named as a new
// process's parent; null when there is no shell (a service, a session
// without a desktop).
HANDLE OpenShellProcess()
{
	DWORD pid = 0;
	if (const HWND shell = GetShellWindow()) GetWindowThreadProcessId(shell, &pid);
	return pid ? OpenProcess(PROCESS_CREATE_PROCESS, FALSE, pid) : nullptr;
}

// The launcher's environment as a player's Steam would not have it: the game
// variables BeforeStart set for the game (SteamAppId, SteamGameId) tell a
// client it was started by a game. The block ends with the extra NUL.
std::wstring EnvironmentForSteam()
{
	std::wstring block;
	if (wchar_t* const all = GetEnvironmentStringsW()) {
		for (const wchar_t* entry = all; *entry; entry += lstrlenW(entry) + 1) {
			const bool game = _wcsnicmp(entry, L"SteamAppId=", 11) == 0 || _wcsnicmp(entry, L"SteamGameId=", 12) == 0;
			if (!game) block.append(entry, lstrlenW(entry) + 1);
		}
		FreeEnvironmentStringsW(all);
	}
	block.push_back(L'\0');
	return block;
}

// Starts Steam so that it is not the launcher's child. Steam is shared: it
// outlives the game and the launcher, and the test tools (tools/sfvelib.py)
// and anything else that ends the launcher's process tree must not take it
// with them. A player who starts Steam gets explorer as its parent, so the
// process is created with explorer named as its parent
// (PROC_THREAD_ATTRIBUTE_PARENT_PROCESS), which is what the tree shows
// afterwards. ShellExecuteExW does not do this: it creates the process in
// the caller (or through DDE in an arbitrary running instance). Without a
// shell to name, or when Windows refuses it, Steam is started the plain way
// and the launcher is logged as its parent; the tools never end Steam's
// processes either way.
bool Run(const std::wstring& exe, const wchar_t* arguments)
{
	std::wstring commandLine = L"\"" + exe + L"\"" + (arguments ? std::wstring(L" ") + arguments : std::wstring());
	const std::wstring folder = exe.substr(0, exe.find_last_of(L'\'));
	std::wstring environment = EnvironmentForSteam();
	const DWORD flags = DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;

	PROCESS_INFORMATION process{};
	bool started = false;
	if (const HANDLE shell = OpenShellProcess()) {
		SIZE_T size = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
		std::vector<char> storage(size);
		const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
		if (InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
			HANDLE parent = shell;
			if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent, sizeof(parent), nullptr,
					nullptr)) {
				STARTUPINFOEXW startup{};
				startup.StartupInfo.cb = sizeof(startup);
				startup.lpAttributeList = attributes;
				started = CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, flags | EXTENDED_STARTUPINFO_PRESENT,
					environment.data(), folder.c_str(), &startup.StartupInfo, &process);
				if (!started) spdlog::warn(L"Steam check: could not start {} under the shell (Win32 {})", exe, GetLastError());
			}
			DeleteProcThreadAttributeList(attributes);
		}
		CloseHandle(shell);
	}
	if (!started) {
		spdlog::warn("Steam check: starting Steam as the launcher's own child");
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		started = CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, flags, environment.data(), folder.c_str(),
			&startup, &process);
	}
	if (!started) {
		spdlog::error(L"Steam check: could not run {} {} (Win32 {})", exe, arguments ? arguments : L"", GetLastError());
		return false;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

template <typename Done>
bool WaitFor(DWORD limitMs, Done done)
{
	for (DWORD waited = 0; waited < limitMs; waited += kPollMs) {
		if (done()) return true;
		Sleep(kPollMs);
	}
	return done();
}

bool Usable(const Record& record)
{
	return record.pid && record.user && Names(SteamProcesses(), record.pid);
}

} // namespace

SteamSession EnsureSteamSession()
{
	const Record record = ReadRecord();
	if (Usable(record)) return SteamSession::Ready;

	const std::vector<DWORD> running = SteamProcesses();
	// The record names the running client: Steam is up and only waits for
	// its account, so the sign-in is waited for, not restarted over.
	if (record.pid && Names(running, record.pid)) {
		spdlog::info("Steam check: Steam (pid {}) has nobody signed in yet; waiting for the sign-in", record.pid);
		if (WaitFor(kRecordWaitMs, [] { return Usable(ReadRecord()); })) return SteamSession::Ready;
		spdlog::error("Steam check: nobody signed in to Steam in time");
		return SteamSession::Failed;
	}

	const std::wstring exe = SteamExecutable();
	if (exe.empty()) {
		spdlog::error("Steam check: Steam's record is stale (pid {}) and Steam's own path is not recorded", record.pid);
		return SteamSession::Failed;
	}
	if (!running.empty()) {
		spdlog::warn("Steam check: Steam's record names pid {} but Steam runs as {}; restarting Steam", record.pid, running.front());
		if (!Run(exe, L"-shutdown")) return SteamSession::Failed;
		if (!WaitFor(kExitWaitMs, [] { return SteamProcesses().empty(); })) {
			spdlog::error("Steam check: Steam did not exit within {} s of -shutdown", kExitWaitMs / 1000);
			return SteamSession::Failed;
		}
	} else {
		spdlog::info("Steam check: Steam is not running; starting it");
	}
	if (!Run(exe, nullptr)) return SteamSession::Failed;
	if (!WaitFor(kRecordWaitMs, [] { return Usable(ReadRecord()); })) {
		const Record late = ReadRecord();
		spdlog::error("Steam check: Steam started but its record did not name it with an account in time (pid {}, user {})",
			late.pid, late.user);
		return SteamSession::Failed;
	}
	spdlog::info("Steam check: Steam restarted; its record names pid {}", ReadRecord().pid);
	return SteamSession::Restarted;
}

} // namespace sfve::launcher
