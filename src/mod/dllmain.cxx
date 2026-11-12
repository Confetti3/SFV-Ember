// sfv_ember.dll: started through SfveStart from the game exe's entry point,
// before any game code runs: by its own entry hook when Launcher.exe injected
// it (mod/EarlyStart.cxx), or by the xinput1_3.dll proxy (src/proxy).
//
// Order matters:
//   1. the log, and the claim of the process (mod/StartClaim.hxx): a second
//      copy of the mod in the same game stays inert and says why;
//   2. the network guard; if it cannot be installed the process ends here and
//      the game never runs with network access;
//   3. the crash record (it hooks the game's own dump writer);
//   4. the build check; on anything but a verified build the mod stays inert;
//   5. the battle hooks, all of them or none: Ember's netplay (the mode's
//      install, with its rooms) only when all are in, and otherwise the game
//      plays offline as SFV.
//
// Every hook the mod installs, from the network guard on, goes in as part of
// a set through CommitHookSet (mod/GameHooks.hxx): the whole set or none of
// it, and the process ends when a set that failed cannot be taken back out.
// The background play, multi-instance, pad and battle sets each have a status
// field (RegisterHookSetStatus); the crash record's is crashRecord's
// dumpWriterHooked.

#include "core/CoreLog.hxx"
#include "ember/SfvGame.hxx"
#include "mod/BackgroundPlay.hxx"
#include "mod/BattleDriver.hxx"
#include "mod/BuildCheck.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/CrashRecord.hxx"
#include "mod/EarlyStart.hxx"
#include "mod/EntitlementGrant.hxx"
#include "mod/GameContent.hxx"
#include "mod/GameExit.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameScene.hxx"
#include "mod/GameThread.hxx"
#include "mod/GuardedStart.hxx"
#include "mod/HudText.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/ModConfig.hxx"
#include "mod/SocketProbe.hxx"
#include "mod/TinyFighters.hxx"
#include "mod/MultiInstance.hxx"
#include "mod/NetGuard.hxx"
#include "mod/NetplayMode.hxx"
#include "mod/OfflineDialogs.hxx"
#include "mod/ProjectileRelease.hxx"
#include "mod/SdlPads.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/StartClaim.hxx"
#include "mod/SyncTestMode.hxx"
#include "mod/VersusLaunch.hxx"
#include "mod/VirtualPads.hxx"
#include "mod/XInputHooks.hxx"
#include "overlay/Overlay.hxx"

#include "platform/Utf8.hxx"

#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>

namespace {

using sfve::start::Refusal;

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

// A hook set's field in the status: "off" when this start does not use it,
// "ready" when every hook of it is in, or "missing <hook>" when it failed, and
// then none of it is in (CommitHookSet).
void RegisterHookSetStatus(const char* key, std::string state)
{
	sfve::mod::RegisterStatus(key, [state = std::move(state)] { return sfve::mod::JsonString(state); });
}

std::string HookSetState(const std::optional<sfve::mod::HookFailure>& missing)
{
	return missing ? std::string("missing ") + missing->site : std::string("ready");
}

// The fight's sinf, cosf, atan2f, acosf, tanf and powf are msvcr120.dll's
// (the VS2013 runtime, from System32). Its x64 math takes FMA3 code on a CPU
// that has FMA3 and SSE2 code on one that has not, and the two can round a
// result differently. Reported, and set when a test asks (-sfve_fma3).
void SetCrtFma3(int wanted)
{
	using namespace sfve::mod;
	HMODULE crt = GetModuleHandleW(L"msvcr120.dll");
	using SetFma3 = int(__cdecl*)(int);
	const auto set = crt ? reinterpret_cast<SetFma3>(GetProcAddress(crt, "_set_FMA3_enable")) : nullptr;
	if (!set) {
		Log("crt: msvcr120.dll's _set_FMA3_enable not found; its math stays as it started");
		return;
	}
	// It takes FMA3 only where the CPU and Windows support it, so asking for
	// it tells whether this copy can use it (and changes nothing: the CRT
	// chose it at start wherever it could).
	const int available = set(1) != 0;
	if (wanted == 0) set(0);
	const bool now = available && wanted != 0;
	Log("crt: this CPU's msvcr120 math %s FMA3; it now uses %s", available ? "can use" : "cannot use",
		now ? "FMA3" : "SSE2");
}

bool RollsBack(sfve::mod::ModMode mode)
{
	return mode == sfve::mod::ModMode::SyncTest || mode == sfve::mod::ModMode::Netplay;
}

// Ember's netplay needs every battle hook: the driver's frames, inputs and
// flow, the allocator that zeroes the battle's memory, the match end and, in
// the modes that roll back, the projectile release. Without any one of them
// two copies would drift apart or leave a match their own ways, so they go
// in together or not at all; a set that could not be taken back out ends the
// game (CommitHookSet).
std::optional<sfve::mod::HookFailure> InstallBattleHooks(uintptr_t base, sfve::mod::BattleDriver& driver)
{
	using namespace sfve::mod;
	HookTransaction hooks;
	AddGameHooks(hooks, base, &driver);
	AddMatchEndHook(hooks, base);
	if (RollsBack(Config().mode)) AddProjectileReleaseHook(hooks, base);
	return CommitHookSet(hooks, "battle");
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
	// Both loaders start the mod from the exe's entry point
	// (mod/GuardedStart.hxx); an older proxy still in the game folder starts
	// it from a thread when it could not, after game code ran unguarded.
	if (!beforeGameCode) {
		Log("start refused: the game was already running when sfv-ember %s came to start; closing it so it never "
			"runs online unguarded", SFVE_VERSION);
		sfve::start::EndProcess(Refusal::StartedLate);
	}
	sfve::CoreLogSink() = &LogLine;
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	Log("sfv-ember %s (%s build) starting in %ls (before game code), instance %d, mode %s", SFVE_VERSION,
		sfve::BuildKindName(sfve::kBuildKind), exe, Config().instance, ModeName(Config().mode));
	Log("loaded from %ls, started by the %s", claim.module.c_str(), loader);
	if (claim.error) Log("could not claim the process for this copy (Win32 %lu): a second copy would not be refused", claim.error);
	if (const char* ignored = IgnoredSetting()) Log("config: %s", ignored);
	SetCrtFma3(Config().crtFma3);
	if (!PlayerBuildIgnored().empty()) Log("config: a player build takes no test options; ignored %s", PlayerBuildIgnored().c_str());
	if (!UnownedProblem().empty()) {
		Log("config: -sfve_unowned=%s has an entry that is not CODE[:costume[:colour]]; left out", UnownedProblem().c_str());
	}
	Log("config: Ember's ownership gate %s; %zu -sfve_unowned entries", Config().ownershipGate ? "on" : "off",
		Config().unowned.size());
	if (!UninstalledProblem().empty()) {
		Log("config: -sfve_uninstalled=%s has an entry that is not a stage code; left out", UninstalledProblem().c_str());
	}
	if (!Config().uninstalled.empty()) {
		Log("config: %zu stage(s) treated as not installed on this copy (-sfve_uninstalled, development)",
			Config().uninstalled.size());
	}
	MigrateSettings();

	if (!InstallNetGuard()) {
		Log("start refused: the network guard is incomplete; closing the game so it never runs online");
		sfve::start::EndProcess(Refusal::GuardIncomplete);
	}

	// After the guard, whose start initialized MinHook: the record hooks the
	// game's dump writer, inert or not.
	InstallCrashRecord({LogFolder(), Config().dumpChannel, true});
	RegisterCrashCommand();
	StartGuardedLagSim(Config().lagMs);
	RegisterStatus("guardRefusals", [] { return std::to_string(NetGuardRefusals()); });
	// Where the log went, and what failed on the way (Log.hxx's OpenLog).
	RegisterStatus("log", [] {
		const LogTarget& log = CurrentLogTarget();
		return "{\"path\":" + JsonString(sf4e::platform::WideToUtf8(log.path)) +
			",\"failed\":" + JsonString(sf4e::platform::WideToUtf8(log.failedPath)) + ",\"error\":" + std::to_string(log.error) + "}";
	});
	RegisterStatus("mode", [] { return JsonString(ModeName(Config().mode)); });
	// Asked for at launch (-sfve_background), or the player's saved Play in
	// the background (Settings > Player), read once here, after the settings'
	// move: a change to the setting takes effect at the next start.
	const bool backgroundPlay = Config().backgroundPlay ||
		sfve::adapter::SavedBoolPreference(&sf4e::netplay::PlayerPreferences::backgroundPlay, "background play");
	std::string backgroundPlayState = "off";
	if (backgroundPlay) backgroundPlayState = HookSetState(InstallBackgroundPlay());
	RegisterHookSetStatus("backgroundPlay", std::move(backgroundPlayState));
	// Instance 0 is a player's game, whose mutexes keep their names.
	const int instance = Config().instance;
	std::string multiInstanceState = "off";
	if (instance > 0) multiInstanceState = HookSetState(InstallMultiInstance(instance));
	RegisterHookSetStatus("multiInstance", std::move(multiInstanceState));
	// Without them every slot is the physical pad's, whatever -sfve_pads asked.
	const std::optional<HookFailure> padHooksMissing = InstallXInputHooks();
	RegisterHookSetStatus("padHooks", HookSetState(padHooksMissing));
	for (int slot = 0; slot < kPadSlots; slot++) {
		if (Config().virtualPadMask & (1u << slot)) SetVirtualPad(slot, true);
	}
	RegisterPadReadStatus();
	RegisterPadCommands();
	// The controller layer (EMBER_LIBRARY.md section 49): pads the game
	// cannot read through XInput, read by SDL on a thread of its own and
	// answered through the pad hooks, so it needs them. The player's saved
	// setting, read once here like Play in the background; whatever the
	// build check below says, as the pads' hooks are.
	const bool controllerLayer = !padHooksMissing &&
		sfve::adapter::SavedBoolPreference(&sf4e::netplay::PlayerPreferences::controllerLayer, "the controller layer");
	StartSdlPads(padHooksMissing ? PadLayerStart::NoHooks : controllerLayer ? PadLayerStart::On : PadLayerStart::Off);
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
	// SFV grants every player everything it has installed, in its own modes
	// too (EMBER_LIBRARY.md section 57); the account's own answer stays
	// Ember's to judge by. The spec's refresh runs once, at boot, so this
	// goes in now, before the game's code runs.
	InstallEntitlementGrant(base, Config().grantAll);
	if (scene && InstallVersusLaunch(base, Config().gameOwnershipCheck)) RegisterVersusLaunchCommands();
	// The game's exit request: the point the netplay mode's runtime leaves
	// its room from (EmberHost), before the engine stops.
	InstallGameExit(base);
	// The HUD's plate labels: the netplay mode's shell names them (SfvHud).
	InstallHudText(base);
	// A Tiny fighters room's tiny sides are drawn small (EMBER_LIBRARY.md
	// section 59); with no tiny side the hooks change nothing.
	InstallTinyFighters(base);
	// Diagnostics (development, -sfve_socket_log): the fight's reads of the
	// fighters' models, logged (docs/design/beta8/desync.md).
	if (Config().socketLog) InstallSocketProbe(base);
	// The host for Ember's interface (its shell is plugged in below, with
	// rooms on); the game's content lists, on the pipe. Its swap chain hooks
	// are one set, so without the overlay none of them is live and the pads
	// stay the game's.
	if (scene && sfve::overlay::InstallOverlay()) SetPadFilter(&sfve::overlay::FilterGamePad);
	RegisterCommand("content", [](const std::string&) {
		RunOnGameThread([] {
			ContentLists lists = ReadContentLists();
			if (!lists.error.empty()) Log("content: no lists: %s", lists.error.c_str());
			MarkUnowned(lists, Config().unowned);
			MarkUninstalled(lists, Config().uninstalled);
			const OwnershipSource& source = lists.ownership;
			Log("content: %zu fighters, %zu stages; ownership %s, looks by %s, stages by %s: setup restricted %d (%d entries), "
				"filter %d, logged in %d, %d marked unowned by -sfve_unowned",
				lists.fighters.size(), lists.stages.size(), source.read ? "read" : "not read", LooksRule(source),
				ContentOwnershipName(source.stages), source.restricted, source.restrictionEntries, source.filter,
				source.loggedIn, source.markedUnowned);
			for (const FighterContent& fighter : lists.fighters) {
				Log("content: %s \"%s\"%s%s%s, %zu costumes", fighter.code.c_str(), fighter.name.c_str(),
					fighter.installed ? "" : " (not installed)", fighter.owned ? "" : " (not owned)",
					fighter.entitled ? "" : " (not in the account cache)", fighter.costumes.size());
				Log("content:   %s", ChoicesText(fighter).c_str());
				for (const CostumeContent& costume : fighter.costumes) {
					std::string colours;
					for (const ColourContent& colour : costume.colours) {
						colours += " " + std::to_string(colour.id) + (colour.owned ? "" : "(not owned)") +
							(colour.entitled ? "" : "(not in the account cache)");
					}
					Log("content:   costume %d \"%s\"%s%s%s, colours%s", costume.id, costume.name.c_str(),
						costume.installed ? "" : " (not installed)", costume.owned ? "" : " (not owned)",
						costume.entitled ? "" : " (not in the account cache)", colours.c_str());
				}
			}
			for (const StageContent& stage : lists.stages) {
				Log("content: stage %s \"%s\"%s%s%s", stage.code.c_str(), stage.name.c_str(),
					stage.installed ? "" : " (not installed)", stage.installed && !stage.entitled ? " (not in the account cache)" : "",
					stage.owned ? "" : " (not owned)");
			}
			const ContentCounts counts = CountContent(lists);
			Log("content: not owned: %d fighters, %d costumes, %d colours, %d stages; %d stages installed but not in the "
				"account cache", counts.fightersNotOwned, counts.costumesNotOwned, counts.coloursNotOwned, counts.stagesNotOwned,
				counts.stagesNotEntitled);
			// Development: the battle setup asset's select-grid list in its own
			// order (GameContent's ReadSelectGridList), to compare with the
			// game's select screen. Its entries are codes only.
			if constexpr (sfve::kBuildKind == sfve::BuildKind::Development) {
				const SelectGridList grid = ReadSelectGridList();
				if (!grid.error.empty()) Log("content: select-grid list: none: %s", grid.error.c_str());
				else Log("content: select-grid list (setup asset +48): %zu entries", grid.codes.size());
				for (size_t i = 0; i < grid.codes.size(); i++) Log("content:   select-grid %zu %s", i, grid.codes[i].c_str());
			}
		});
		return std::string("ok queued (see the log)");
	});
	static BattleDriver driver(MakeMode(Config()));
	driver.RegisterControl();
	const std::optional<HookFailure> missing = InstallBattleHooks(base, driver);
	RegisterHookSetStatus("battleHooks", HookSetState(missing));
	if (missing) {
		Log("battle hooks: missing %s, so Ember's netplay is off and the game plays offline as SFV", missing->site);
		return 1;
	}
	Log("battle hooks: battle frame and its constructor, input record, rollback unit, battle flow, allocator and "
		"match end%s hooked", RollsBack(Config().mode) ? ", and dead projectiles released on the battle's clock and thread" : "");
	// The mode's own commands and hosts (the netplay mode's Ember runtime,
	// whose rooms launch linked matches), once the driver holds it for the
	// process's life and only on battles it can drive.
	driver.InstallMode();
	return 1;
}

// No DllMain of our own: GGPO's static library supplies the module's (it only
// seeds rand), and everything here starts from SfveStart.
