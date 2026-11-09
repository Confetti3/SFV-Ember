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

bool Run(const std::wstring& exe, const wchar_t* arguments)
{
	std::wstring commandLine = L"\"" + exe + L"\"" + (arguments ? std::wstring(L" ") + arguments : std::wstring());
	const std::wstring folder = exe.substr(0, exe.find_last_of(L'\\'));
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, folder.c_str(),
			&startup, &process)) {
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
