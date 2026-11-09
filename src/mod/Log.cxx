#include "mod/Log.hxx"

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

std::mutex s_mutex;
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

void OpenLog(int instance)
{
	std::lock_guard lock(s_mutex);
	if (s_file != INVALID_HANDLE_VALUE) return;
	// The usual log; when another program holds it or it cannot be written,
	// a file of this process's own beside it, then under %LOCALAPPDATA%, then
	// in %TEMP%: a player's log is never silently lost.
	const std::wstring usual = instance == 0 ? L"sfv_ember.log" : L"sfv_ember." + std::to_wstring(instance) + L".log";
	const std::wstring own = L"sfv_ember-" + std::to_wstring(GetCurrentProcessId()) + L".log";
	const std::wstring logs = LogFolder();
	const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
	wchar_t temp[MAX_PATH + 1] = {};
	const DWORD tempLength = GetTempPathW(MAX_PATH + 1, temp);
	OpenedLog opened = OpenFirstLog({
		{logs.empty() ? L"" : logs + L"\\" + usual, false},
		{logs.empty() ? L"" : logs + L"\\" + own, true},
		{local.empty() ? L"" : local + L"\\SFVEmber\\logs\\" + own, true},
		{tempLength && tempLength <= MAX_PATH ? std::wstring(temp) + own : L"", true},
	});
	s_file = opened.file;
	s_target = {opened.path, opened.failedPath, opened.error};
	if (s_file == INVALID_HANDLE_VALUE || !opened.error) return;
	char line[1200];
	const int length = snprintf(line, sizeof(line), "log: could not open %ls (Win32 %lu); this process logs here instead\r\n",
		opened.failedPath.c_str(), opened.error);
	DWORD written = 0;
	if (length > 0) WriteFile(s_file, line, (DWORD)std::min(length, (int)sizeof(line) - 1), &written, nullptr);
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

void WriteLine(const char* text, int length)
{
	if (s_file == INVALID_HANDLE_VALUE) return;
	DWORD written = 0;
	WriteFile(s_file, text, (DWORD)length, &written, nullptr);
}

} // namespace

void LogLine(const char* line)
{
	char text[1200];
	const int n = FormatLine(text, line);
	if (!n) return;
	std::lock_guard lock(s_mutex);
	t_writing = true;
	WriteLine(text, n);
	t_writing = false;
}

void TryLogLine(const char* line)
{
	if (t_writing) return; // this thread faulted inside a write: it holds the lock
	char text[1200];
	const int n = FormatLine(text, line);
	if (!n) return;
	std::unique_lock lock(s_mutex, std::try_to_lock);
	if (lock.owns_lock()) WriteLine(text, n);
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
