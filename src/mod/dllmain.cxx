// sfv_ember.dll: started by the proxy (src/proxy) through SfveStart, from the
// game exe's entry point, before any game code runs.
//
// Order matters:
//   1. the log, so a refused start leaves a reason;
//   2. the network guard; if it cannot be installed the process ends here and
//      the game never runs with network access;
//   3. the build check; on anything but a verified build the mod stays inert.

#include "core/CoreLog.hxx"
#include "ember/EmberHost.hxx"
#include "ember/RoomCommands.hxx"
#include "mod/BackgroundPlay.hxx"
#include "mod/BattleDriver.hxx"
#include "mod/BuildCheck.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/ModConfig.hxx"
#include "mod/MultiInstance.hxx"
#include "mod/NetGuard.hxx"
#include "mod/NetplayMode.hxx"
#include "mod/OfflineDialogs.hxx"
#include "mod/ProjectileRelease.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SyncTestMode.hxx"
#include "mod/VersusLaunch.hxx"
#include "mod/VirtualPads.hxx"
#include "overlay/Overlay.hxx"

#include <windows.h>

#include <algorithm>

namespace {

constexpr UINT kExitGuardFailed = 0x5F0E0001;

bool s_started = false;

// The process's battle mode; `netplay` is the netplay mode (Ember's tick
// polls it), null for the other modes.
std::unique_ptr<sfve::mod::BattleMode> MakeMode(const sfve::mod::ModConfig& config, sfve::mod::NetplayMode** netplay)
{
	using namespace sfve::mod;
	switch (config.mode) {
	case ModMode::SyncTest: return std::make_unique<SyncTestMode>(config);
	case ModMode::Netplay: {
		auto mode = std::make_unique<NetplayMode>(config);
		mode->RegisterCommands();
		*netplay = mode.get();
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
	InstallMatchEnd(base);
	// The host for Ember's interface (plugged in once the shared Ember shell
	// is in); the game's content lists for it, meanwhile on the pipe.
	if (scene && sfve::overlay::InstallOverlay()) SetPadFilter(&sfve::overlay::FilterGamePad);
	RegisterCommand("content", [](const std::string&) {
		RunOnGameThread([] {
			const ContentLists lists = ReadContentLists();
			std::string fighters, stages;
			for (const std::string& f : lists.fighters) fighters += " " + f;
			for (const std::string& st : lists.stages) stages += " " + st;
			Log("content: %zu fighters:%s", lists.fighters.size(), fighters.c_str());
			Log("content: %zu stages:%s", lists.stages.size(), stages.c_str());
		});
		return std::string("ok queued (see the log)");
	});
	static NetplayMode* netplay = nullptr;
	static BattleDriver driver(MakeMode(Config(), &netplay));
	driver.RegisterControl();
	// Ember's runtime, hosted once the driver and its mode exist (EmberHost
	// ends the room link before they go).
	if (netplay && Config().room) {
		sfve::adapter::RegisterRoomCommands();
		sfve::adapter::InstallEmberHost({Config().instance}, *netplay);
	}
	InstallGameHooks(base, &driver);
	if (Config().mode == ModMode::SyncTest || Config().mode == ModMode::Netplay) InstallDeterministicProjectileRelease(base);
	return 1;
}

// No DllMain of our own: GGPO's static library supplies the module's (it only
// seeds rand), and everything here starts from SfveStart.
