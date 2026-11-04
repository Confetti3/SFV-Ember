// sfv_ember.dll: started by the proxy (src/proxy) through SfveStart, from the
// game exe's entry point, before any game code runs.
//
// Order matters:
//   1. the log, so a refused start leaves a reason;
//   2. the network guard; if it cannot be installed the process ends here and
//      the game never runs with network access;
//   3. the build check; on anything but a verified build the mod stays inert.

#include "core/CoreLog.hxx"
#include "mod/BackgroundPlay.hxx"
#include "mod/BattleDriver.hxx"
#include "mod/BuildCheck.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameScene.hxx"
#include "mod/VirtualPads.hxx"
#include "mod/Log.hxx"
#include "mod/ModConfig.hxx"
#include "mod/MultiInstance.hxx"
#include "mod/HelperLink.hxx"
#include "mod/NetplayMode.hxx"
#include "mod/NetGuard.hxx"
#include "mod/OfflineDialogs.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SyncTestMode.hxx"
#include "mod/VersusLaunch.hxx"

#include <windows.h>

#include <algorithm>

namespace {

constexpr UINT kExitGuardFailed = 0x5F0E0001;

bool s_started = false;

// The stage a linked match is played on when the host names none: both
// copies must load the same one, and their own defaults can differ.
constexpr const char* kDefaultLinkedStage = "S25";

sfve::mod::HelperLink* s_link = nullptr; // the netplay mode owns it for the process's life

// The linked match's battle, from both players' setups: the host's fighter
// as P1, the joiner's as P2, on the host's stage, launched from the main
// menu on both copies (each copy decodes the same two setups). Anything
// short of a fully named battle, or a launch that fails, abandons the match
// so the battle the players start runs offline rather than out of step.
void LaunchLinkedMatch(const sfve::MatchLink::Endpoints& match)
{
	using namespace sfve::mod;
	MatchPicks picks;
	std::wstring hostStage, joinerStage;
	if (!ParseSetup(match.setups[0], &picks.players[0], &hostStage) ||
		!ParseSetup(match.setups[1], &picks.players[1], &joinerStage) || picks.players[0].fighter.empty() ||
		picks.players[1].fighter.empty() || hostStage.empty()) {
		s_link->Abandon("the players' setups do not name both fighters and a stage (" + JsonString(match.setups[0]) +
			" / " + JsonString(match.setups[1]) + ")");
		return;
	}
	picks.stage = hostStage;
	Log("netplay: launching %s vs %s once on the main menu", JsonString(match.setups[0]).c_str(),
		JsonString(match.setups[1]).c_str());
	LaunchOnMainMenu(
		picks, [] { return s_link->Ready().has_value() && !s_link->Failed(); },
		[](const std::string& why) { s_link->Abandon("the battle could not be launched: " + why); });
}

// This copy's setup: its fighter and, for when it hosts, the stage.
std::string LocalSetup(const sfve::mod::ModConfig& config)
{
	using namespace sfve::mod;
	const std::string setup = (config.fighter.empty() ? "-" : config.fighter) + " " +
		(config.stage.empty() ? kDefaultLinkedStage : config.stage);
	FighterPick fighter;
	std::wstring stage;
	if (config.fighter.empty() || !ParseSetup(setup, &fighter, &stage)) {
		Log("netplay: -sfve_fighter / -sfve_stage do not name a fighter and a stage (%s); a linked match will not start",
			JsonString(setup).c_str());
	}
	return setup;
}

std::unique_ptr<sfve::mod::BattleMode> MakeMode(const sfve::mod::ModConfig& config)
{
	using namespace sfve::mod;
	switch (config.mode) {
	case ModMode::SyncTest: return std::make_unique<SyncTestMode>(config.distance, config.learn, config.perturb);
	case ModMode::Netplay: {
		std::unique_ptr<HelperLink> link;
		if (config.helperLink) {
			link = std::make_unique<HelperLink>(config.delay, LocalSetup(config), LaunchLinkedMatch);
			s_link = link.get();
			link->Start(); // a failure shows in the status; the battle then waits
			link->RegisterCommands();
		}
		auto mode = std::make_unique<NetplayMode>(config, std::move(link));
		mode->RegisterCommands();
		return mode;
	}
	default: return std::make_unique<PassThroughMode>();
	}
}

} // namespace

extern "C" __declspec(dllexport) int SfveStart(int beforeGameCode)
{
	using namespace sfve::mod;
	if (s_started) return 1;
	s_started = true;

	LoadConfigFromCommandLine(GetCommandLineW());
	OpenLog(Config().instance);
	sfve::CoreLogSink() = &LogLine;
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	Log("sfv-ember 0.0.1 starting in %ls (%s), instance %d, mode %s", exe,
		beforeGameCode ? "before game code" : "LATE: the game was already running", Config().instance,
		ModeName(Config().mode));

	if (!InstallNetGuard()) {
		Log("start refused: the network guard is incomplete; closing the game so it never runs online");
		TerminateProcess(GetCurrentProcess(), kExitGuardFailed);
		return 0;
	}

	StartGuardedLagSim(Config().lagMs);
	RegisterStatus("guardRefusals", [] { return std::to_string(NetGuardRefusals()); });
	RegisterStatus("mode", [] { return JsonString(ModeName(Config().mode)); });
	if (Config().backgroundPlay) InstallBackgroundPlay();
	InstallMultiInstance(Config().instance);
	InstallVirtualPads();
	for (int slot = 0; slot < kPadSlots; slot++) {
		if (Config().virtualPadMask & (1u << slot)) SetVirtualPad(slot, true);
	}
	RegisterPadCommands();
	StartControlPipe(Config().instance);

	std::string detail;
	const BuildState build = CheckBuild(GetModuleHandleW(nullptr), sfv::VerifiedBuild(), &detail);
	Log("build: %s (%s)", BuildStateName(build), detail.c_str());
	if (build != BuildState::Verified) {
		Log("inert: only the network guard is active");
		return 1;
	}
	if (Config().mode == ModMode::Inert) {
		Log("inert by request: only the network guard and the pads are active");
		return 1;
	}
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	// The scene manager's tick runs the game-thread queue; without it the
	// title cannot be moved on, so the offline notice must stay.
	const bool scene = InstallGameScene(base);
	OfflineDialogs dialogs = static_cast<OfflineDialogs>(std::clamp(Config().offlineDialogs, 0, 2));
	if (!scene) dialogs = std::min(dialogs, OfflineDialogs::HideDownloadFails);
	InstallOfflineDialogs(base, dialogs);
	RegisterStatus("suppressedDialogs", [] { return std::to_string(SuppressedDialogs()); });
	if (scene && InstallVersusLaunch(base, Config().ownershipCheck)) RegisterVersusLaunchCommands();
	static BattleDriver driver(MakeMode(Config()));
	driver.RegisterControl();
	InstallGameHooks(base, &driver);
	return 1;
}

// No DllMain of our own: GGPO's static library supplies the module's (it only
// seeds rand), and everything here starts from SfveStart.
