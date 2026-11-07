// sfv_ember.dll: started through SfveStart from the game exe's entry point,
// before any game code runs: by its own entry hook when Launcher.exe injected
// it (mod/EarlyStart.cxx), or by the xinput1_3.dll proxy (src/proxy).
//
// Order matters:
//   1. the log, and the claim of the process (mod/StartClaim.hxx): a second
//      copy of the mod in the same game stays inert and says why;
//   2. the network guard; if it cannot be installed the process ends here and
//      the game never runs with network access;
//   3. the build check; on anything but a verified build the mod stays inert.

#include "core/CoreLog.hxx"
#include "mod/BackgroundPlay.hxx"
#include "mod/BattleDriver.hxx"
#include "mod/BuildCheck.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/EarlyStart.hxx"
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
#include "mod/StartClaim.hxx"
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

// SFV Ember's settings moved from beside the mod (SFVEmber\settings.json in
// the game's folder, under the proxy) to %APPDATA%\SFVEmber, where the
// launcher's are. The first start that finds no settings there copies the
// old file once: from beside this module, else from beside the game's exe
// (where the proxy loaded the mod from). The old file stays; nothing in the
// game folder is written.
void MigrateSettings()
{
	using namespace sfve::mod;
	const std::wstring data = DataFolder();
	if (data.empty()) return;
	const std::wstring target = data + L"\\settings.json";
	if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) return;
	wchar_t exe[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring exeFolder = length && length < MAX_PATH ? std::wstring(exe, length) : std::wstring();
	const size_t slash = exeFolder.find_last_of(L"\\/");
	exeFolder = slash == std::wstring::npos ? std::wstring() : exeFolder.substr(0, slash);
	for (const std::wstring& folder : {ModuleFolder(), exeFolder}) {
		if (folder.empty()) continue;
		const std::wstring old = folder + L"\\SFVEmber\\settings.json";
		if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
		CreateDirectoryW(data.c_str(), nullptr);
		if (CopyFileW(old.c_str(), target.c_str(), TRUE)) Log("settings: copied %ls to %ls", old.c_str(), target.c_str());
		else Log("settings: could not copy %ls to %ls (Win32 %lu)", old.c_str(), target.c_str(), GetLastError());
		return;
	}
}

} // namespace

extern "C" int SfveStart(int beforeGameCode)
{
	using namespace sfve::mod;
	if (s_started) return 1;
	s_started = true;

	LoadConfigFromCommandLine(GetCommandLineW());
	OpenLog(Config().instance);
	const char* loader = StartedByEntryHook() ? "launcher" : "proxy";
	const StartClaimResult claim = ClaimStart(loader);
	if (!claim.won) {
		Log("sfv-ember %s at %ls (started by the %s) stays inert: %ls (started by the %s) started first in this game",
			SFVE_VERSION, claim.module.c_str(), loader, claim.winnerModule.c_str(), claim.winnerLoader.c_str());
		return 1;
	}
	sfve::CoreLogSink() = &LogLine;
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	Log("sfv-ember %s starting in %ls (%s), instance %d, mode %s", SFVE_VERSION, exe,
		beforeGameCode ? "before game code" : "LATE: the game was already running", Config().instance,
		ModeName(Config().mode));
	Log("loaded from %ls, started by the %s%s", claim.module.c_str(), loader,
		LauncherHookPlanted() && !StartedByEntryHook() ? " (the launcher's entry hook came second)" : "");
	if (claim.error) Log("could not claim the process for this copy (Win32 %lu): a second copy would not be refused", claim.error);
	if (const char* ignored = IgnoredSetting()) Log("config: %s", ignored);
	MigrateSettings();

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
			const ContentLists lists = ReadContentLists();
			Log("content: %zu fighters, %zu stages", lists.fighters.size(), lists.stages.size());
			for (const FighterContent& fighter : lists.fighters) {
				Log("content: %s \"%s\"%s, %zu costumes", fighter.code.c_str(), fighter.name.c_str(),
					fighter.installed ? "" : " (not installed)", fighter.costumes.size());
				for (const CostumeContent& costume : fighter.costumes) {
					std::string colours;
					for (int colour : costume.colours) colours += " " + std::to_string(colour);
					Log("content:   costume %d \"%s\"%s, colours%s", costume.id, costume.name.c_str(),
						costume.installed ? "" : " (not installed)", colours.c_str());
				}
			}
			for (size_t i = 0; i < lists.stages.size(); i++) {
				Log("content: stage %s \"%s\"", lists.stages[i].c_str(), lists.stageNames.size() > i ? lists.stageNames[i].c_str() : "");
			}
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
