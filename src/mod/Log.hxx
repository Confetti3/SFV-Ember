#pragma once

// The mod's log: %APPDATA%\SFVEmber\logs\sfv_ember.log (the product's
// appDataFolder, where the launcher logs too), one line per call, any thread.
// Opened before anything else so a refused start leaves a reason.

#include <string>

namespace sfve::mod {

// Instance 0 logs to sfv_ember.log, instance N to sfv_ember.N.log. When that
// file cannot be opened (another program holds it, or writing it is
// refused), the process logs to sfv_ember-<pid>.log beside it, else under
// %LOCALAPPDATA%\SFVEmber\logs, else in %TEMP%, and the first line there
// names the file that failed and why (CurrentLogTarget, the pipe's status).
void OpenLog(int instance);

// Where this process logs: the file, and the first file that could not be
// opened on the way with its Win32 error (none: empty and 0). An empty path:
// it logs nowhere.
struct LogTarget {
	std::wstring path;
	std::wstring failedPath;
	unsigned long error = 0;
};
const LogTarget& CurrentLogTarget();
void Log(const char* fmt, ...);
void LogLine(const char* line);
// LogLine unless another thread is writing the log at that moment (then the
// line is dropped): for a crash handler, whose fault may have come from
// inside a write. Allocates nothing.
void TryLogLine(const char* line);
// The folder the mod was loaded from: the package folder beside Launcher.exe,
// or the game's Win64 folder under the xinput1_3.dll proxy. Its helper
// (sf4-net.exe) and its pictures (assets) are there.
std::wstring ModuleFolder();
// %APPDATA%\SFVEmber, which holds the settings and the logs; empty when
// Windows gives no roaming folder.
std::wstring DataFolder();
// The folder the logs go to (for other diagnostic files).
std::wstring LogFolder();

} // namespace sfve::mod
