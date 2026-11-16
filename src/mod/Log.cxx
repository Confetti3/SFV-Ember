#include "mod/Log.hxx"
#include "mod/LogFile.hxx"

#include <windows.h>
#include <shlobj.h>

#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::mod {

namespace {

// A slim reader/writer lock, not std::mutex: the first lock the mod takes is
// the log's, and with an older msvcp140.dll in the game's folder std::mutex's
// first lock can fault before the log exists (mod/RuntimeModules.hxx). This
// one needs no runtime (SRWLOCK_INIT is constant) and is a Lockable.
class LogMutex {
public:
	void lock() { AcquireSRWLockExclusive(&lock_); }
	bool try_lock() { return TryAcquireSRWLockExclusive(&lock_) != 0; }
	void unlock() { ReleaseSRWLockExclusive(&lock_); }

private:
	SRWLOCK lock_ = SRWLOCK_INIT;
};

LogMutex s_mutex;
RunLogCap s_cap(kRunLogLimit);
HANDLE s_file = INVALID_HANDLE_VALUE;
LogTarget s_target;
// This thread holds s_mutex for a write: a fault inside that write reaches
// the crash handler on the same thread, where TryLogLine must not touch the
// mutex again.
thread_local bool t_writing = false;

} // namespace

std::wstring ModuleFolder()
{
	wchar_t path[MAX_PATH] = {};
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&ModuleFolder), &self);
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring folder(path);
	const size_t slash = folder.find_last_of(L"\\/");
	return slash == std::wstring::npos ? L"." : folder.substr(0, slash);
}

namespace {

std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
	PWSTR path = nullptr;
	if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) return {};
	std::wstring folder = path;
	CoTaskMemFree(path);
	return folder;
}

} // namespace

std::wstring DataFolder()
{
	const std::wstring appData = KnownFolder(FOLDERID_RoamingAppData);
	return appData.empty() ? std::wstring() : appData + L"\\SFVEmber";
}

std::wstring LogFolder()
{
	const std::wstring data = DataFolder();
	return data.empty() ? std::wstring() : data + L"\\logs";
}

OpenedLog OpenFirstLog(const std::vector<LogCandidate>& candidates)
{
	OpenedLog opened;
	for (const LogCandidate& candidate : candidates) {
		if (candidate.path.empty()) continue;
		std::error_code ignored; // a folder that cannot be made fails the open below
		std::filesystem::create_directories(std::filesystem::path(candidate.path).parent_path(), ignored);
		// Shared for writing: a second copy of the mod in the same game (the
		// proxy's, beside the launcher's) appends why it stays inert.
		const HANDLE file = CreateFileW(candidate.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, candidate.own ? CREATE_NEW : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			opened.file = file;
			opened.path = candidate.path;
			return opened;
		}
		if (!opened.error) {
			opened.error = GetLastError();
			opened.failedPath = candidate.path;
		}
	}
	return opened;
}

std::vector<LogCandidate> LogCandidates(int instance, unsigned long pid, const std::wstring& logs, const std::wstring& localLogs,
	const std::wstring& temp)
{
	const std::wstring usual = instance == 0 ? L"sfv_ember.log" : L"sfv_ember." + std::to_wstring(instance) + L".log";
	const std::wstring own = L"sfv_ember-" + std::to_wstring(pid) + L".log";
	const auto in = [](const std::wstring& folder, const std::wstring& name) {
		if (folder.empty()) return std::wstring();
		const bool slash = folder.back() == L'\\' || folder.back() == L'/';
		return slash ? folder + name : folder + L"\\" + name;
	};
	return {
		{in(logs, usual), false},
		{in(logs, own), true},
		{in(localLogs, own), true},
		{in(temp, own), true},
	};
}

bool RotateLog(const std::wstring& path, int keep, const FILETIME& runStart)
{
	WIN32_FILE_ATTRIBUTE_DATA info;
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return false;
	if (!info.nFileSizeHigh && !info.nFileSizeLow) return false;
	if (CompareFileTime(&info.ftLastWriteTime, &runStart) >= 0) return false;
	const size_t dot = path.find_last_of(L'.');
	const std::wstring stem = path.substr(0, dot), extension = dot == std::wstring::npos ? std::wstring() : path.substr(dot);
	const auto old = [&](int n) { return stem + L".old" + std::to_wstring(n) + extension; };
	DeleteFileW(old(keep).c_str());
	for (int n = keep - 1; n >= 1; n--) MoveFileExW(old(n).c_str(), old(n + 1).c_str(), MOVEFILE_REPLACE_EXISTING);
	return MoveFileExW(path.c_str(), old(1).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

OpenedLog OpenRotatedLog(const std::vector<LogCandidate>& candidates, int keep, const FILETIME& runStart)
{
	for (const LogCandidate& candidate : candidates) {
		if (candidate.own || candidate.path.empty()) continue;
		RotateLog(candidate.path, keep, runStart);
		break;
	}
	return OpenFirstLog(candidates);
}

namespace {

// When this process was created: a log written since is this run's.
FILETIME ProcessStart()
{
	FILETIME created{}, exited{}, kernel{}, user{};
	if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) GetSystemTimeAsFileTime(&created);
	return created;
}

} // namespace

void OpenLog(int instance)
{
	OpenedLog opened;
	{
		std::lock_guard lock(s_mutex);
		if (s_file != INVALID_HANDLE_VALUE) return;
		// The usual log; when another program holds it or it cannot be written,
		// a file of this process's own beside it, then under %LOCALAPPDATA%, then
		// in %TEMP%: a player's log is never silently lost.
		const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
		wchar_t temp[MAX_PATH + 1] = {};
		const DWORD tempLength = GetTempPathW(MAX_PATH + 1, temp);
		// The log of an earlier run is set aside first (kLogRunsKept of them are
		// kept), so each run starts a log of its own.
		opened = OpenRotatedLog(LogCandidates(instance, GetCurrentProcessId(), LogFolder(),
			local.empty() ? std::wstring() : local + L"\\SFVEmber\\logs",
			tempLength && tempLength <= MAX_PATH ? std::wstring(temp) : std::wstring()), kLogRunsKept, ProcessStart());
		s_file = opened.file;
		s_target = {opened.path, opened.failedPath, opened.error};
	}
	if (opened.file == INVALID_HANDLE_VALUE) return;
	// The log's first line says which file it is, so a log that is not where
	// a reader looks (another copy of the folder, a viewer that shows a
	// stale or redirected one) is told apart from one that was not written.
	Log("log: pid %lu writes %ls", GetCurrentProcessId(), opened.path.c_str());
	if (opened.error) {
		Log("log: could not open %ls (Win32 %lu); this process logs here instead", opened.failedPath.c_str(), opened.error);
	}
}

const LogTarget& CurrentLogTarget()
{
	return s_target;
}

namespace {

// The line as the log writes it, with its time and thread; its length, or 0.
int FormatLine(char (&text)[1200], const char* line)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	const int n = snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %s\r\n", t.wYear, t.wMonth,
		t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), line);
	return n <= 0 ? 0 : n < (int)sizeof(text) ? n : (int)sizeof(text) - 1;
}

void WriteRaw(const char* text, int length)
{
	DWORD written = 0;
	WriteFile(s_file, text, (DWORD)length, &written, nullptr);
}

// `force`: a crash handler's line, written whatever this run has logged.
void WriteLine(const char* text, int length, bool force)
{
	if (s_file == INVALID_HANDLE_VALUE) return;
	const RunLogCap::Take take = force ? RunLogCap::Take::Write : s_cap.Admit((uint64_t)length);
	if (take == RunLogCap::Take::Drop) return;
	WriteRaw(text, length);
	if (take != RunLogCap::Take::WriteThenNotice) return;
	char notice[1200];
	const int n = FormatLine(notice, "log: this run's log reached its size limit; the lines after this one are dropped");
	if (n) WriteRaw(notice, n);
}

} // namespace

void LogLine(const char* line)
{
	char text[1200];
	const int n = FormatLine(text, line);
	if (!n) return;
	std::lock_guard lock(s_mutex);
	t_writing = true;
	WriteLine(text, n, false);
	t_writing = false;
}

void TryLogLine(const char* line)
{
	if (t_writing) return; // this thread faulted inside a write: it holds the lock
	char text[1200];
	const int n = FormatLine(text, line);
	if (!n) return;
	std::unique_lock lock(s_mutex, std::try_to_lock);
	if (lock.owns_lock()) WriteLine(text, n, true);
}

void Log(const char* fmt, ...)
{
	char line[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	LogLine(line);
}

} // namespace sfve::mod
