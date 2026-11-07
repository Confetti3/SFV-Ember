// SFV Ember's launch steps (the Ember library's launcher/LaunchSteps.hxx):
// StreetFighterV.exe started suspended with sfv_ember.dll added to its
// imports by Detours, so the mod loads with the game's own DLLs and starts
// from the exe's entry point, before any game code runs (mod/EarlyStart.cxx).
// Nothing is written into the game folder. Steam starts nothing here: the
// game is told its app id through its environment, as Steam would.
//
// SFV Ember has no replays, no Discord companion and no game language of its
// own yet, so those steps do nothing. The mod starts Ember's networking
// helper itself (ember/EmberHost.cxx), so the launcher hands it none.
//
// Test copies: --instance N (1-3) starts copy N of a two-copy test, with its
// own launcher log (launcher.N.log) and its own single-instance lock, so the
// one-launcher-per-user rule for instance 0, what players run, is unchanged.
// --game-option passes one test option to the game: -sfve_* or the window's
// -windowed, -ResX=N and -ResY=N, nothing else.

#include "launcher/LaunchSteps.hxx"

#include "common/ProductIdentity.hxx"
#include "launcher/netplay/netplay_persist.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>
#include <shellapi.h>

#include <CLI/CLI.hpp>
#include <detours/detours.h>
#include <spdlog/spdlog.h>

#include <string>
#include <vector>

namespace {

constexpr int kMaxTestInstance = 3;

int s_instance = 0;
std::vector<std::string> s_gameOptions;

// The test instance from the raw command line, read before CLI11 parses it
// (the log and the lock are named first). 0 when absent or out of range; the
// parse then refuses a bad value.
int InstanceFromCommandLine()
{
	int count = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
	if (!arguments) return 0;
	int instance = 0;
	for (int i = 1; i + 1 < count; i++) {
		if (wcscmp(arguments[i], L"--instance") != 0) continue;
		const int value = _wtoi(arguments[i + 1]);
		if (value >= 1 && value <= kMaxTestInstance) instance = value;
		break;
	}
	LocalFree(arguments);
	return instance;
}

// A test option the game may be given: the mod's own (-sfve_*, ModConfig)
// or the window's size. No spaces or quotes, so it stays one argument.
bool AllowedGameOption(const std::string& option)
{
	if (option.empty() || option.find_first_of(" \t\"") != std::string::npos) return false;
	if (option.rfind("-sfve_", 0) == 0) return true;
	if (option == "-windowed") return true;
	for (const char* size : {"-ResX=", "-ResY="}) {
		const std::string prefix = size;
		if (option.rfind(prefix, 0) == 0 && option.size() > prefix.size() && option.size() <= prefix.size() + 5 &&
			option.find_first_not_of("0123456789", prefix.size()) == std::string::npos) {
			return true;
		}
	}
	return false;
}

} // namespace

namespace ember { namespace launch {

void AddOptions(CLI::App& app)
{
	s_instance = InstanceFromCommandLine();
	app.add_option("--instance", s_instance, "Test only: start copy N (1-3) of a two-copy test, beside the player's copy.")
		->check(CLI::Range(1, kMaxTestInstance));
	app.add_option("--game-option", s_gameOptions, "Test only: pass one -sfve_* option, -windowed, -ResX=N or -ResY=N to the game.")
		->check([](const std::string& option) { return AllowedGameOption(option) ? std::string() : "not a test option the game takes: " + option; });
}

std::wstring LauncherLogName()
{
	const int instance = InstanceFromCommandLine();
	return instance == 0 ? std::wstring(Product().launcherLog) : L"launcher." + std::to_wstring(instance) + L".log";
}

std::wstring InstanceScope()
{
	// The public launcher keeps the one lock per user; copy N has its own.
	return s_instance == 0 ? std::wstring() : L".instance" + std::to_wstring(s_instance);
}

void Prepare(sf4e::launcher::PersistedSettings&, bool showConsole)
{
	if (showConsole) spdlog::info("--console: SFV Ember has no game console; its log is sfv_ember.log");
}

std::string GameLanguage(const std::wstring&) { return {}; }

std::vector<const wchar_t*> RuntimeLibraries()
{
	// sfv_ember.dll links GGPO, spdlog, fmt and zlib statically; it imports
	// only Windows' own DLLs and the Visual C++ runtime, which RuntimeIsCurrent
	// checks.
	return {};
}

std::string ResolveReplayLink(const std::string&) { return {}; }

void BeforeStart(const std::wstring&)
{
	// What Steam sets for a game it starts; without them the game's Steam
	// check exits with 0x35 (tools/sfvelib.py). The game folder gets no
	// steam_appid.txt: nothing is written there.
	SetEnvironmentVariableA("SteamAppId", Product().steam.appId);
	SetEnvironmentVariableA("SteamGameId", Product().steam.appId);
}

HANDLE StartGame(const StartRequest& request, DWORD& startError)
{
	// The game's command line: its own path, then the mod's settings
	// (mod/ModConfig.hxx). Rooms and Ember's runtime on; the ownership check
	// is the mod's own default. -sfve_loader=launcher has the mod start
	// itself from the exe's entry point. A test's options come first: the
	// mod reads the first of each, so they take the place of these.
	std::wstring commandLine = L"\"" + request.executable + L"\"";
	for (const std::string& option : s_gameOptions) commandLine += L" " + sf4e::platform::Utf8ToWide(option.c_str());
	commandLine += L" -sfve_loader=launcher -sfve_mode=netplay -sfve_room=1";
	if (s_instance != 0) commandLine += L" -sfve_instance=" + std::to_wstring(s_instance);
	spdlog::info(L"Starting {} with {}: {}", request.executable, request.module, commandLine);

	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	LPCSTR modules[] = {request.moduleAnsi.c_str()};
	SetLastError(0);
	if (!DetourCreateProcessWithDllsW(request.executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
			CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
			request.gameDirectory.c_str(), &startup, &process, 1, modules, nullptr)) {
		startError = GetLastError();
		spdlog::error("Could not start the game with {} (Win32 {})", sf4e::platform::WideToUtf8(Product().gameModule), startError);
		return nullptr;
	}
	if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
		startError = GetLastError();
		spdlog::error("Could not resume the game (Win32 {})", startError);
		TerminateProcess(process.hProcess, 9007);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return nullptr;
	}
	spdlog::info("Game started, pid={}", process.dwProcessId);
	CloseHandle(process.hThread);
	return process.hProcess;
}

void GameEnded(bool) {}

} } // namespace ember::launch
