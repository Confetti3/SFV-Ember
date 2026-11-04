#include "mod/Log.hxx"

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>

#include <mutex>
#include <string>

namespace sfve::mod {

namespace {

std::mutex s_mutex;
HANDLE s_file = INVALID_HANDLE_VALUE;

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

std::wstring LogFolder()
{
	return ModuleFolder() + L"\\SFVEmber\\logs";
}

void OpenLog(int instance)
{
	std::lock_guard lock(s_mutex);
	if (s_file != INVALID_HANDLE_VALUE) return;
	const std::wstring root = ModuleFolder() + L"\\SFVEmber";
	CreateDirectoryW(root.c_str(), nullptr);
	CreateDirectoryW((root + L"\\logs").c_str(), nullptr);
	const std::wstring name = instance == 0 ? L"\\logs\\sfv_ember.log" : L"\\logs\\sfv_ember." + std::to_wstring(instance) + L".log";
	s_file = CreateFileW((root + name).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void LogLine(const char* line)
{
	SYSTEMTIME t;
	GetLocalTime(&t);
	char text[1200];
	const int n = snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d.%03d [%lu] %s\r\n", t.wYear, t.wMonth,
		t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), line);
	if (n <= 0) return;
	std::lock_guard lock(s_mutex);
	if (s_file == INVALID_HANDLE_VALUE) return;
	DWORD written = 0;
	WriteFile(s_file, text, (DWORD)(n < (int)sizeof(text) ? n : (int)sizeof(text) - 1), &written, nullptr);
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
