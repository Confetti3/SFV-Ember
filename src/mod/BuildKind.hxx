#pragma once

// Which kind of build this is (EMBER_LIBRARY.md section 42). A development
// build, what scripts\build.ps1 makes by default for the test tools, takes
// the test switches: Launcher.exe's --instance and --game-option, the mod's
// -sfve_* test options (the ownership gate off, scripted inputs, lag, a
// second copy's ports and pads) and the control pipe's commands. A player
// build (scripts\build.ps1 -Player, the only kind scripts\package.ps1
// packages) takes none of them: its launcher passes the game only its own
// options, its mod reads only those, and its pipe answers ping and status.
//
// CMakeLists.txt's SFVE_DEV_BUILD option sets it for every target.

namespace sfve {

enum class BuildKind { Player, Development };

#if !defined(SFVE_DEV_BUILD)
#error SFVE_DEV_BUILD must be defined (CMakeLists.txt's option of that name)
#elif SFVE_DEV_BUILD
constexpr BuildKind kBuildKind = BuildKind::Development;
#else
constexpr BuildKind kBuildKind = BuildKind::Player;
#endif

constexpr const char* BuildKindName(BuildKind kind)
{
	return kind == BuildKind::Development ? "development" : "player";
}

} // namespace sfve
