#pragma once

// Which C and C++ runtime this process runs the mod on. The mod is built
// against the dynamic runtime and loaded as an import of the game's exe, so
// the game's own folder is searched before System32: an older msvcp140.dll
// left there by another tool would be the one the mod gets, and it can fail
// before the mod's log opens. One line per runtime module, at the start,
// shows which copy it was.

#include <string>

namespace sfve::mod {

// The file version of the file at `path` ("14.38.33135.0"), or empty.
std::string FileVersionText(const std::wstring& path);

// Whether `path` lies directly in `folder` (case-insensitive, either slash).
bool InFolder(const std::wstring& path, const std::wstring& folder);

// The log line for the loaded module `name`: its path and version, and whether
// it is the game folder's own copy (`gameFolder`); or that it is not loaded.
std::string DescribeModule(const wchar_t* name, const std::wstring& gameFolder);

// Logs the runtime modules the mod imports (msvcp140, msvcp140_1, vcruntime140,
// vcruntime140_1) and the toolset it was built with.
void LogRuntimeModules();

} // namespace sfve::mod
