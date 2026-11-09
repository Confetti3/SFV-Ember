#include "mod/Log.hxx"

#include <windows.h>
#include <shlobj.h>

#include <stdarg.h>
#include <stdio.h>

#include <mutex>
#include <string>

namespace sfve::mod {

namespace {

std::mutex s_mutex;
HANDLE s_file = INVALID_HANDLE_VALUE;
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

std::wstring DataFolder()
{
	PWSTR appData = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &appData))) return {};
	std::wstring folder = std::wstring(appData) + L"\\SFVEmber";
	CoTaskMemFree(appData);
	return folder;
}

std::wstring LogFolder()
{
	const std::wstring data = DataFolder();
	return data.empty() ? std::wstring() : data + L"\\logs";
}

void OpenLog(int instance)
{
	std::lock_guard lock(s_mutex);
	if (s_file != INVALID_HANDLE_VALUE) return;
	const std::wstring root = DataFolder();
	if (root.empty()) return;
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW((root + L"\\logs").c_str(), nullptr);
	const std::wstring name = instance == 0 ? L"\\logs\\sfv_ember.log" : L"\\logs\\sfv_ember." + std::to_wstring(instance) + L".log";
	// Shared for writing: a second copy of the mod in the same game (the
	// proxy's, beside the launcher's) appends why it stays inert.
	s_file = CreateFileW((root + name).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
