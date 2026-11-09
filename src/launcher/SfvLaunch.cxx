// SFV Ember's launch steps (the Ember library's launcher/LaunchSteps.hxx):
// StreetFighterV.exe started suspended with sfv_ember.dll added to its
// imports by Detours, so the mod loads with the game's own DLLs and starts
// from the exe's entry point, before any game code runs (mod/EarlyStart.cxx).
// Nothing is written into the game folder. Steam starts nothing here: the
// game is told its app id through its environment, as Steam would.
//
// SFV Ember has no replays, no Discord companion and no game language of its
// own yet, so those steps do nothing. The mod starts Ember's networking
// helper itself (ember/EmberHost.cxx), so the launcher hands it none. It
// hands it the crash dump channel (InheritedDumpChannel below).
//
// Test copies, in a development build only (mod/BuildKind.hxx): --instance
// N (1-3) starts copy N of a two-copy test, with its own launcher log
// (launcher.instanceN.log; launcher.N.log are the log's own rotations) and
// its own single-instance lock, so the one-launcher-per-user rule for
// instance 0, what players run, is unchanged. --game-option passes one test
// option to the game: -sfve_* or the window's -windowed, -ResX=N and
// -ResY=N, nothing else. The game always gets -windowed (StartGame), so a
// test's own is not repeated. A player build has neither option.
//
// Before anything starts, the exe must be the build the mod was made for
// (ExecutableBuild): never another build, which would run as plain SFV, and
// never UE4's starter in the game's top folder, which would start the game
// without the mod. Then Steam's record is checked (SteamSession), and a
// restart of a running Steam is the player's to allow.

#include "launcher/ExecutableBuild.hxx"
#include "launcher/LaunchMessages.hxx"
#include "launcher/LaunchSteps.hxx"
#include "SteamSession.hxx"
#include "mod/BuildKind.hxx"
#include "mod/SfvBuild.hxx"

#include "common/CrashDump.hxx"
#include "common/Localization.hxx"
#include "common/ProductIdentity.hxx"
#include "launcher/netplay/netplay_persist.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>
#include <shellapi.h>

#include <CLI/CLI.hpp>
#include <detours/detours.h>
#include <spdlog/spdlog.h>

#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kMaxTestInstance = 3;

int s_instance = 0;
std::vector<std::string> s_gameOptions;

// --instance N from the raw command line; 0 when absent or out of range.
int InstanceArgument()
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

// The test instance, read before CLI11 parses the command line (the log and
// the lock are named first): a development build's --instance, whose bad
// value the parse then refuses; a player build is always copy 0.
int InstanceFromCommandLine()
{
	return sfve::kBuildKind == sfve::BuildKind::Development ? InstanceArgument() : 0;
}

// A test option the game may be given: the mod's own (-sfve_*, ModConfig)
// or the window's size. No spaces or quotes, so it stays one argument.
bool AllowedGameOption(const std::string& option)
{
	if (option.empty() || option.find_first_of(" \t\"") != std::string::npos) return false;
	// The dump channel is the launcher's own to name (InheritedDumpChannel);
	// the mod reads the first of each option, so a test's would win.
	if (option.rfind("-sfve_dump_channel", 0) == 0) return false;
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

// The launcher's crash dump channel (common/CrashDump.hxx) as the game gets
// it: inheritable copies of its two events and its mailbox, handed to the
// game alone through the start's handle list and named on its command line
// (-sfve_dump_channel, the mod's CrashRecord). The copies close when the
// start is over; the launcher's own handles stay uninheritable, so nothing
// else it starts gets them. Not Ready without an open channel or when a
// step fails: the game then writes its own dump.
class InheritedDumpChannel {
public:
	explicit InheritedDumpChannel(const sf4e::crash::DumpChannel* channel)
	{
		if (!channel || !channel->view) return;
		const HANDLE sources[] = {channel->request, channel->done, channel->mailbox};
		for (int i = 0; i < 3; i++) {
			if (!DuplicateHandle(GetCurrentProcess(), sources[i], GetCurrentProcess(), &handles_[i], 0, TRUE, DUPLICATE_SAME_ACCESS)) {
				spdlog::warn("Could not pass the crash dump channel to the game (Win32 {}); it writes its own dumps", GetLastError());
				return;
			}
		}
		SIZE_T size = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
		attributeBytes_.resize(size);
		auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBytes_.data());
		if (!size || !InitializeProcThreadAttributeList(list, 1, 0, &size)) {
			spdlog::warn("Could not pass the crash dump channel to the game (Win32 {}); it writes its own dumps", GetLastError());
			return;
		}
		attributes_ = list;
		if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles_, sizeof(handles_), nullptr, nullptr)) {
			spdlog::warn("Could not pass the crash dump channel to the game (Win32 {}); it writes its own dumps", GetLastError());
			return;
		}
		ready_ = true;
	}
	~InheritedDumpChannel()
	{
		if (attributes_) DeleteProcThreadAttributeList(attributes_);
		for (HANDLE handle : handles_) {
			if (handle) CloseHandle(handle);
		}
	}
	InheritedDumpChannel(const InheritedDumpChannel&) = delete;
	InheritedDumpChannel& operator=(const InheritedDumpChannel&) = delete;

	bool Ready() const { return ready_; }
	LPPROC_THREAD_ATTRIBUTE_LIST Attributes() const { return ready_ ? attributes_ : nullptr; }
	// "R:D:M": the handles' values, which the game's inherited handles share.
	std::wstring Option() const
	{
		wchar_t text[64] = {};
		swprintf_s(text, L"%llx:%llx:%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(handles_[0])),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(handles_[1])),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(handles_[2])));
		return text;
	}

private:
	HANDLE handles_[3] = {};
	std::vector<unsigned char> attributeBytes_;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes_ = nullptr;
	bool ready_ = false;
};

} // namespace

namespace ember { namespace launch {

void AddOptions(CLI::App& app)
{
	if constexpr (sfve::kBuildKind == sfve::BuildKind::Development) {
		s_instance = InstanceFromCommandLine();
		app.add_option("--instance", s_instance, "Test only: start copy N (1-3) of a two-copy test, beside the player's copy.")
			->check(CLI::Range(1, kMaxTestInstance));
		app.add_option("--game-option", s_gameOptions, "Test only: pass one -sfve_* option, -windowed, -ResX=N or -ResY=N to the game.")
			->check([](const std::string& option) { return AllowedGameOption(option) ? std::string() : "not a test option the game takes: " + option; });
	}
}

std::wstring LauncherLogName()
{
	const int instance = InstanceFromCommandLine();
	return instance == 0 ? std::wstring(Product().launcherLog) : L"launcher.instance" + std::to_wstring(instance) + L".log";
}

std::wstring InstanceScope()
{
	// The public launcher keeps the one lock per user; copy N has its own.
	return s_instance == 0 ? std::wstring() : L".instance" + std::to_wstring(s_instance);
}

void Prepare(sf4e::launcher::PersistedSettings& settings, bool showConsole)
{
	if (showConsole) spdlog::info("--console: SFV Ember has no game console; its log is sfv_ember.log");
	// A first start's name, unique per player as SF4 Ember's launcher gives
	// it; a name the player has is kept. The mod plays under the saved name
	// (EmberHost).
	sf4e::launcher::EnsureUniqueDisplayName(settings);
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

HANDLE StartGame(const StartRequest& request, StartFailure& failure)
{
	namespace sfv = sfve::mod::sfv;
	const std::optional<uint32_t> stamp = sfve::launcher::ExecutableTimestamp(request.executable);
	if (stamp != sfv::kExeTimestamp) {
		spdlog::error(L"{} is not {} (PE stamp {:#010x}, wanted {:#010x}); not started", request.executable,
			sf4e::platform::Utf8ToWide(sfv::kBuildLabel), stamp.value_or(0), sfv::kExeTimestamp);
		failure.message = sf4e::loc::Tf(messages::GameBuildUnsupported, Product().branding.productName, sfv::kBuildLabel,
			sf4e::platform::WideToUtf8(request.executable));
		return nullptr;
	}
	// The game's Steam start needs Steam's record of its client to be right,
	// or SFV crashes in its online code (launcher/SteamSession.hxx).
	const auto mayRestart = [] { return AskOkCancel(messages::SteamRestartAsk); };
	if (sfve::launcher::EnsureSteamSession(mayRestart) == sfve::launcher::SteamSession::Failed) {
		failure.message = sf4e::loc::T(messages::SteamNotReady);
		return nullptr;
	}
	// The game's command line: its own path, then the mod's settings
	// (mod/ModConfig.hxx). Rooms and Ember's runtime on; the ownership check
	// is the mod's own default. -sfve_loader=launcher has the mod start
	// itself from the exe's entry point. A test's options come first: the
	// mod reads the first of each, so they take the place of these.
	std::wstring commandLine = L"\"" + request.executable + L"\"";
	for (const std::string& option : s_gameOptions) commandLine += L" " + sf4e::platform::Utf8ToWide(option.c_str());
	commandLine += L" -sfve_loader=launcher -sfve_mode=netplay -sfve_room=1";
	// The game opens in a window at its saved resolution, never full screen:
	// SFV's own full screen changes the monitor's mode, which upset players'
	// displays. A plain window is the chosen default (2026-11-09).
	bool windowed = false;
	for (const std::string& option : s_gameOptions) windowed = windowed || option == "-windowed";
	if (!windowed) commandLine += L" -windowed";
	if (s_instance != 0) commandLine += L" -sfve_instance=" + std::to_wstring(s_instance);
	InheritedDumpChannel dumps(request.dumps);
	if (dumps.Ready()) commandLine += L" -sfve_dump_channel=" + dumps.Option();
	spdlog::info(L"Starting {} with {}: {}", request.executable, request.module, commandLine);

	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.lpAttributeList = dumps.Attributes();
	PROCESS_INFORMATION process{};
	LPCSTR modules[] = {request.moduleAnsi.c_str()};
	SetLastError(0);
	const DWORD flags = CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
		(dumps.Ready() ? EXTENDED_STARTUPINFO_PRESENT : 0);
	if (!DetourCreateProcessWithDllsW(request.executable.c_str(), commandLine.data(), nullptr, nullptr, dumps.Ready(), flags,
			nullptr, request.gameDirectory.c_str(), &startup.StartupInfo, &process, 1, modules, nullptr)) {
		failure.error = GetLastError();
		spdlog::error("Could not start the game with {} (Win32 {})", sf4e::platform::WideToUtf8(Product().gameModule), failure.error);
		return nullptr;
	}
	if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
		failure.error = GetLastError();
		spdlog::error("Could not resume the game (Win32 {})", failure.error);
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
