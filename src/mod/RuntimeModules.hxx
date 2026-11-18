#pragma once

// Which C and C++ runtime this process runs the mod on. The mod is built
// against the dynamic runtime and loaded as an import of the game's exe, so
// the game's own folder is searched before System32: an older msvcp140.dll
// left there by another tool would be the one the mod gets, and it can fail
// before the mod's log opens. One line per runtime module, at the start,
// shows which copy it was.

#include <string>
#include <vector>

namespace sfve::mod {

// The file version of the file at `path` ("14.38.33135.0"), or empty.
std::string FileVersionText(const std::wstring& path);

// Whether `path` lies directly in `folder` (case-insensitive, either slash).
bool InFolder(const std::wstring& path, const std::wstring& folder);

// The log line for the loaded module `name`: its path and version, and whether
// it is the game folder's own copy (`gameFolder`); or that it is not loaded.
std::string DescribeModule(const wchar_t* name, const std::wstring& gameFolder);

// The process's modules besides Windows' own (anything under its folder:
// System32, WinSxS and the rest): the game's, Ember's, and whatever else a
// player runs inside the game, an overlay or another mod (Polarity's
// polarity.live dll was seen in an Ember game, EMBER-123), which may change
// fight state. A desync between copies whose lists differ may be that
// module's, so a match's start logs them and a desync report carries them.
// File names only: a path holds the player's user name.
struct OtherModules {
	std::vector<std::string> game;      // under the game's install folder (Ember's are there too)
	std::vector<std::string> elsewhere; // anywhere else: an overlay's, another mod's
};
// Sorts `paths` (case and slash direction aside) into the game's install
// folder `gameRoot` and elsewhere, leaving out Windows' (`windows`); each
// list sorted, without repeats. Pure.
OtherModules SortModules(const std::vector<std::wstring>& paths, const std::wstring& windows, const std::wstring& gameRoot);
// This process's modules, sorted so (the game's install folder: three above
// its exe's, StreetFighterV\Binaries\Win64).
OtherModules LoadedOtherModules();
// "elsewhere: a.dll, b.dll; in the game's folder: c.dll, d.dll", "none" for
// a list that is empty.
std::string OtherModulesText(const OtherModules& modules);

// Logs the runtime modules the mod imports (msvcp140, msvcp140_1, vcruntime140,
// vcruntime140_1) and the toolset it was built with.
void LogRuntimeModules();

} // namespace sfve::mod
