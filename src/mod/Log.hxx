#pragma once

// The mod's log: <game folder>\SFVEmber\logs\sfv_ember.log, one line per call,
// any thread. Opened before anything else so a refused start leaves a reason.

#include <string>

namespace sfve::mod {

// Instance 0 logs to sfv_ember.log, instance N to sfv_ember.N.log.
void OpenLog(int instance);
void Log(const char* fmt, ...);
void LogLine(const char* line);
// The folder the logs go to (for other diagnostic files).
// The folder the mod was loaded from (the game's Win64 folder).
std::wstring ModuleFolder();
std::wstring LogFolder();

} // namespace sfve::mod
