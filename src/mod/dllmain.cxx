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
#include "mod/GameContent.hxx"
#include "mod/GameExit.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/HudText.hxx"
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

// The process's battle mode.
std::unique_ptr<sfve::mod::BattleMode> MakeMode(const sfve::mod::ModConfig& config)
{
	using namespace sfve::mod;
	switch (config.mode) {
	case ModMode::SyncTest: return std::make_unique<SyncTestMode>(config);
	case ModMode::Netplay: return std::make_unique<NetplayMode>(config);
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
	Log("sfv-ember %s starting in %ls (%s), instance %d, mode %s", SFVE_VERSION, exe,
		beforeGameCode ? "before game code" : "LATE: the game was already running", Config().instance,
		ModeName(Config().mode));
	if (const char* ignored = IgnoredSetting()) Log("config: %s", ignored);
	if (!UnownedProblem().empty()) {
		Log("config: -sfve_unowned=%s has an entry that is not CODE[:costume[:colour]]; left out", UnownedProblem().c_str());
	}
	Log("config: Ember's ownership gate %s; %zu -sfve_unowned entries", Config().ownershipGate ? "on" : "off (development)",
		Config().unowned.size());

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
	if (scene && InstallVersusLaunch(base, Config().gameOwnershipCheck)) RegisterVersusLaunchCommands();
	InstallMatchEnd(base);
	// The game's exit request: the point the netplay mode's runtime leaves
	// its room from (EmberHost), before the engine stops.
	InstallGameExit(base);
	// The HUD's plate labels: the netplay mode's shell names them (SfvHud).
	InstallHudText(base);
	// The host for Ember's interface (its shell is plugged in below, with
	// rooms on); the game's content lists, on the pipe.
	if (scene && sfve::overlay::InstallOverlay()) SetPadFilter(&sfve::overlay::FilterGamePad);
	RegisterCommand("content", [](const std::string&) {
		RunOnGameThread([] {
			ContentLists lists = ReadContentLists();
			MarkUnowned(lists, Config().unowned);
			const OwnershipSource& source = lists.ownership;
			Log("content: %zu fighters, %zu stages; ownership %s: setup restricted %d (%d entries), filter %d, "
				"logged in %d, %d marked unowned by -sfve_unowned",
				lists.fighters.size(), lists.stages.size(), source.read ? "read" : "not read", source.restricted,
				source.restrictionEntries, source.filter, source.loggedIn, source.markedUnowned);
			int unownedFighters = 0, unownedCostumes = 0, unownedColours = 0;
			for (const FighterContent& fighter : lists.fighters) {
				unownedFighters += !fighter.owned;
				Log("content: %s \"%s\"%s%s, %zu costumes", fighter.code.c_str(), fighter.name.c_str(),
					fighter.installed ? "" : " (not installed)", fighter.owned ? "" : " (not owned)", fighter.costumes.size());
				for (const CostumeContent& costume : fighter.costumes) {
					std::string colours;
					for (int colour : costume.colours) {
						const bool owned =
							std::find(costume.ownedColours.begin(), costume.ownedColours.end(), colour) != costume.ownedColours.end();
						unownedColours += !owned;
						colours += " " + std::to_string(colour) + (owned ? "" : "(not owned)");
					}
					unownedCostumes += !costume.owned;
					Log("content:   costume %d \"%s\"%s%s, colours%s", costume.id, costume.name.c_str(),
						costume.installed ? "" : " (not installed)", costume.owned ? "" : " (not owned)", colours.c_str());
				}
			}
			for (size_t i = 0; i < lists.stages.size(); i++) {
				const bool owned =
					std::find(lists.ownedStages.begin(), lists.ownedStages.end(), lists.stages[i]) != lists.ownedStages.end();
				Log("content: stage %s \"%s\"%s", lists.stages[i].c_str(),
					lists.stageNames.size() > i ? lists.stageNames[i].c_str() : "", owned ? "" : " (not owned)");
			}
			Log("content: not owned: %d fighters, %d costumes, %d colours, %zu stages", unownedFighters, unownedCostumes,
				unownedColours, lists.stages.size() - lists.ownedStages.size());
		});
		return std::string("ok queued (see the log)");
	});
	static BattleDriver driver(MakeMode(Config()));
	driver.RegisterControl();
	// The mode's own commands and hosts (the netplay mode's Ember runtime),
	// once the driver holds it for the process's life.
	driver.InstallMode();
	InstallGameHooks(base, &driver);
	if (Config().mode == ModMode::SyncTest || Config().mode == ModMode::Netplay) InstallDeterministicProjectileRelease(base);
	return 1;
}

// No DllMain of our own: GGPO's static library supplies the module's (it only
// seeds rand), and everything here starts from SfveStart.
