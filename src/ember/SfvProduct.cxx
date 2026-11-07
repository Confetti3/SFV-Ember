// SFV Ember's identity (the library's common/ProductIdentity.hxx): its own
// names, folders, named objects and log names, so it never shows SF4 Ember's
// and never shares its settings folder, link mailboxes or logs with it.
// Constant-initialized, as SF4 Ember's is, so a crash handler or a static
// initializer can read it.
//
// The names come from what SFV Ember already calls itself: "SFV Ember" (the
// proxy's messages), the SFVEmber folder and sfv_ember.log (mod/Log.cxx),
// sfv_ember.dll and StreetFighterV.exe (src/launcher, proxy/xinput_proxy.cxx), and "sfve"
// for its named objects (ControlPipe, MultiInstance). Its look is Ember's,
// set in Street Fighter V's own terms (EMBER_LIBRARY.md section 17): the
// Band theme and its gold below, Ember's own mark in those colours
// (art/brand, embedded by the build) and a flat ground (assets/brand). Where the shared words name SF4
// Ember, SFV Ember's own replace them (LocaleOverlays).

#include "common/ProductIdentity.hxx"
#include "EmbeddedBrand.hxx"

namespace {

// SFV Ember's own words where the shared catalog names SF4 Ember on the
// screens SFV shows: the shell's header and Home, About and the version line,
// the room's build checks and the shortcut hint, the launcher's, recovery's,
// updater's and problem reports' words that name SF4, its files or its
// folders, and SF4's "Not owned on Steam." where SFV's answer is the game's
// own (EMBER_LIBRARY.md section 23). English replaces every translation of
// them too (Localization.cxx), until SFV Ember has translations of its own.
// Training's words are not shown here.
const char kEnglish[] = R"po(
msgid "app.name"
msgstr "SFV Ember"

msgid "shell.home_title"
msgstr "SFV EMBER"

msgid "screen.game"
msgstr "SFV"

msgid "about.version_line"
msgstr "SFV Ember {0} · {1}"

msgid "brand.subtitle"
msgstr "SFV  /  PRIVATE ROOMS & ROLLBACK NETPLAY"

msgid "about.ember_detail"
msgstr "Unofficial Street Fighter V mod, built on Ember (based on sf4e by Anthony Danducci). Iroh / QUIC, GGPO, Dear ImGui and Inter typography. Build: {0}"

msgid "runtime.open_shortcut"
msgstr "SFV Ember  /  F10 or Start"

msgid "runtime.build_mismatch"
msgstr "Build mismatch. Both players must use the same SFV Ember build and the same installed fighters and stages."

msgid "runtime.room_join_failed"
msgstr "Could not join the room. Check the invitation and that both players use the same SFV Ember build."

msgid "selection.not_owned"
msgstr "Not owned in this copy of Street Fighter V."

msgid "runtime.invite_other_build"
msgstr "The host is on a different SFV Ember build or has other fighters and stages installed. Both players need the same."

msgid "launcher.update_failed"
msgstr "The update could not be installed. Close the game and previous launcher before retrying. See %TEMP%\\sfv-ember-update.log; preserved product copies are in .ember-update-backups."

msgid "launcher.recovery_opened"
msgstr "Networking was unavailable, so Street Fighter V closed for recovery. Retry launch starts it again."

msgid "launcher.game_not_found"
msgstr "Street Fighter V could not be found."

msgid "launcher.sidecar_missing"
msgstr "sfv_ember.dll is missing or its path cannot be used by the injector. Extract the full package to a simple local path."

msgid "launcher.runtime_outdated"
msgstr "Your Microsoft Visual C++ runtime is out of date, and the game would crash on start. Install the latest Visual C++ Redistributable (x64), then start SFV Ember again.\n\nOpen the download page now?"

msgid "launcher.already_running"
msgstr "SFV Ember is already running. If you cannot see it, end Launcher.exe and StreetFighterV.exe in Task Manager, then start SFV Ember again."

msgid "launcher.start_failed"
msgstr "Game startup or injection failed. Check the launcher log at %APPDATA%\\SFVEmber\\logs\\launcher.log, then retry."

msgid "launcher.game_runs_as_admin"
msgstr "StreetFighterV.exe is set to run as administrator, so SFV Ember cannot start it. Right-click StreetFighterV.exe in the game's StreetFighterV\\Binaries\\Win64 folder, open Properties, then Compatibility, and clear \"Run this program as an administrator\", also under \"Change settings for all users\". Then retry."

msgid "launcher.game_error"
msgstr "The game exited with an error. Inspect the launcher log at %APPDATA%\\SFVEmber\\logs\\launcher.log, then retry."

msgid "launcher.game_crashed"
msgstr "The game crashed. To help fix it, choose Send this report below. Or send the files in %APPDATA%\\SFVEmber\\logs yourself."

msgid "launcher.game_wrong_dll"
msgstr "The game stopped before SFV Ember could start because a DLL it loaded is missing a function. Send the launcher log at %APPDATA%\\SFVEmber\\logs\\launcher.log."

msgid "launcher.game_crashed_files"
msgstr "The game crashed. Please send the files in %APPDATA%\\SFVEmber\\logs (sfv_ember.log and launcher.log, and sfv_ember-crash-*.dmp if there is one) to the SFV Ember team, then retry."

msgid "launcher.game_crashed_sending"
msgstr "The game crashed. Send problem reports is on, so its report is being sent, with the small dump if the crash wrote one. The files are also in %APPDATA%\\SFVEmber\\logs."

msgid "recovery.d3d_failed"
msgstr "The recovery interface could not initialize Direct3D 9. See %APPDATA%\\SFVEmber\\logs\\launcher.log."

msgid "recovery.choose_folder_title"
msgstr "Select the folder containing StreetFighterV.exe (StreetFighterV\\Binaries\\Win64)"

msgid "recovery.folder_invalid"
msgstr "That folder does not contain StreetFighterV.exe. Choose the game's StreetFighterV\\Binaries\\Win64 folder."

msgid "recovery.choose_folder_detail"
msgstr "Select the installed Street Fighter V folder containing StreetFighterV.exe: StreetFighterV\\Binaries\\Win64 inside the game's folder. Game files are not included in this package."

msgid "recovery.close_detail"
msgstr "Close recovery without starting Street Fighter V. Any running operation is cancelled."

msgid "updates.install_detail"
msgstr "Close Street Fighter V before installing. The package is verified before installation. Your personal settings are preserved."

msgid "updates.close_detail"
msgstr "Close the updater without starting Street Fighter V. Any running operation is cancelled."

msgid "updates.start_game"
msgstr "Start Street Fighter V"

msgid "updates.start_game_detail"
msgstr "Close the updater and start Street Fighter V with SFV Ember."

msgid "services.diagnostics_saved"
msgstr "Saved %APPDATA%\\SFVEmber\\diagnostics\\ember-diagnostics.txt"

msgid "services.update_prepared"
msgstr "Update prepared. Restarting SFV Ember..."

msgid "update.no_zip_asset"
msgstr "Latest release has no SFV Ember ZIP asset."

msgid "update.close_sf4"
msgstr "Close Street Fighter V before installing an update."

msgid "update.download_failed"
msgstr "Download failed ({0}). Manual download: {1} (details in %TEMP%\\sfv-ember-update.log)"

msgid "update.missing_binaries"
msgstr "Downloaded package is missing Launcher.exe or sfv_ember.dll."

msgid "update.notice.waiting_launcher"
msgstr "Waiting for SFV Ember to close..."

msgid "update.notice.reopens"
msgstr "SFV Ember opens again when it is done."

msgid "reports.payload"
msgstr "Reports go to embernetplay.link. Each holds the SFV Ember version and build, the Windows version, the crash details, redacted tails of sfv_ember.log, launcher.log and sf4-net.log, and the small crash dump when the crash wrote one."

msgid "reports.history_unreadable_detail"
msgstr "sent-reports.json in %APPDATA%\\SFVEmber cannot be read, so no report is sent without asking. Delete the file to start a new record."
)po";

const ember::LocaleOverlay LocaleOverlays[] = {{"en", kEnglish}, {nullptr, nullptr}};

// Gold, SFV Ember's accent: SFV's own menu gold (OKLCH 0.80 0.115 88).
constexpr unsigned kGold = 0xDDB961;

// Band: SFV's charcoal menu bands as Ember's grounds. One ramp at OKLCH hue
// 95 with chroma under 0.01 for the neutrals, the gold accent at hue 88, and
// danger, Ready and Pending moved to the accent's lightness (0.82) and a
// chroma near the gold's; LIVE stays a deeper red (0.66). As SFV marks a chosen tile,
// cards at rest are a step lighter than the panel and the focused one goes
// darkest, marked by a gold bar and a gold hairline at its foot. Square
// corners, no cut, upright titles in capitals, gold hairlines closing the
// header and opening the hint bar, and secondary buttons edged in the border
// tone. Every text pair is at least as far from its ground as SF4 Ember's
// lowest (section 17).
constexpr ember::ShellTheme Band = {
	0x100F0D, 0x282824, 0x191816, 0x2F2E2A, 0x060604, 0x373530, 0x41403C, 0xF2DD9D, 0xEDECE0, 0xC0BEB4, 0xEE5A50, 0xEEB4AB, 0xA2D3A2,
	0x31302C, 0x0D0B07, 0x050402, 0x57554F, 0x73726B, 0xE8CF8C, 0xDFC398,
	0x080706, 0x1E1D1A, 0x34332F, 0xEDECE0, 0x999892,
	0.f, 0.f, 0.f,
	0x7B6633, true, true, true,
	// The post-match prompt on the shell's own ground, as the room is: SFV's
	// bright RESULT behind a veil turned to a muddy smear (section 20).
	true,
};

// SFV's battle HUD (EMBER_LIBRARY.md section 18), measured on its fights at
// 1280x720, 1920x1080 (every point 1.5 times) and 2560x1080 (the game's 16:9
// picture pillarboxed, its HUD inside it): a 1280x720 frame, scaled evenly to
// fit and centred. The plates are SFV's "PLAYER 1"/"PLAYER 2" labels in the
// life bars (x 113-171, y 63-73; the bars x 105-593 and 686-1174, y 56-82),
// where the game writes the room's names itself (mod/HudText). Each name's
// marks go in the free stretch of the row under its bar, between the
// fighter's name (to y 99) and the stun gauge (x 446-594, y 85-95), against
// the gauge. Across the top the HUD ends with that row, at y 100 (the
// portraits end at 95, the timer at 91); in the bottom corners it starts with
// the V-Skill marks (y 627), and between the EX gauges (to x 367, from 912)
// the bottom is clear.
constexpr ember::BattleHud SfvBattleHud = {
	1280.f, 720.f,
	{{111.f, 61.f, 173.f, 75.f}, {1107.f, 61.f, 1169.f, 75.f}},
	true,
	{{200.f, 83.f, 440.f, 99.f}, {840.f, 83.f, 1080.f, 99.f}},
	100.f,
	624.f,
	// SFV has no option of its own that moves its HUD.
	false,
};

constexpr ember::ProductIdentity SfvEmber = {
	{
		"SFV Ember",
		"SFV Ember",
		"Street Fighter V",
		kGold,
		sf4e::ui::brand::Pixels,
		sf4e::ui::brand::Size,
		L"brand/sfv-ember-menu-background.png",
		&Band,
		&SfvBattleHud,
		// The room shows a fighter only where its player shared a pick. A
		// profile's main is fighter 0 (RYU) until the player sets one, and
		// nothing in SFV Ember asks them to, so a main would put a face nobody
		// picked on the Members rows and the unpicked seats (EMBER_LIBRARY.md
		// section 20).
		true,
		// The opponent's Rematch or decline shows on the post-match prompt as
		// soon as the held frames carry it (section 20).
		true,
	},
	L"SFVEmber",
	// Settings and logs under %APPDATA%\SFVEmber, as SF4 Ember's are under
	// %APPDATA%\sf4e: the launcher and the mod share them, wherever the
	// package is. The mod copies a settings.json left beside it by the
	// xinput1_3.dll proxy's installs once (mod/dllmain.cxx, MigrateSettings).
	false,
	L"SFVEmber",
	L"Local\\SFVEmber.Launcher.",
	L"Local\\sfve-video-",
	L".sfve-video-tmp",
	// A scheme of its own, so a link never opens SF4 Ember (section 5.4 of
	// EMBER_LIBRARY.md is still open).
	"ember-sfv",
	// No public release repository yet (EMBER_LIBRARY.md section 22): the
	// update check offers nothing and makes no request. A release package is
	// named sfv-ember-<version>.zip (scripts/package.ps1).
	"",
	"",
	"sfv-ember-",
	L"sfv-ember-update-",
	L"sfve-updater-",
	L"sfv-ember-update.log",
	// Launcher.exe starts the game with the mod injected (src/launcher);
	// Updater.exe installs a package. No Discord companion yet.
	L"Launcher.exe",
	L"Updater.exe",
	L"sfv_ember.dll",
	L"StreetFighterV.exe",
	// Steam's app 310950. StreetFighterV.exe at the install folder's top is
	// UE4's small bootstrap, which starts the real game from
	// StreetFighterV\Binaries\Win64: that one is started, in its own folder.
	{"310950", L"StreetFighterV", L"StreetFighterV\\Binaries\\Win64"},
	L"",
	L"sfv_ember.log",
	L"launcher.log",
	L"sfv_ember-crash.log",
	L"sfv_ember-crash-",
	LocaleOverlays,
};

} // namespace

namespace ember {
const ProductIdentity& Product() { return SfvEmber; }
} // namespace ember
