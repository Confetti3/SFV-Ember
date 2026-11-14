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
#include "SfvRankNames.hxx"

#include <atomic>

namespace {

// SFV Ember's own words where the shared catalog names SF4 Ember on the
// screens SFV shows: the shell's header and Home, About and the version line,
// the room's build checks and the shortcut hint, the launcher's, recovery's,
// updater's and problem reports' words that name SF4, its files or its
// folders, and SF4's "Not owned on Steam." where SFV's answer is the game's
// own (EMBER_LIBRARY.md section 23), and the Replays screen's words that name
// SF4's Battle Log, its adding and its inputs reader, which SFV does not
// offer (section 26). Three of those notices, never shown otherwise in SFV,
// say what an SFV replay's playback needs said: a replay that cannot be
// played (inputs.unreadable_detail), one recorded on another version
// (replays.not_added_files, a library msgid SFV never shows for adding) and
// one that did not play to its end (replays.not_added).
// Training's words where SF4's name SF4's own (its
// Fight Request, a Practice room's 9999-second clock) say SFV's
// (EMBER_LIBRARY.md section 27).
// The catalogs: locales/<tag>.po, the same msgids in every language the
// library declares (EMBER_LIBRARY.md section 30), embedded by the build
// (CMakeLists.txt, sfve_product) as the kOverlay_<tag> texts and the
// LocaleOverlays table.
#include "SfvLocaleOverlays.inc"

// Gold, SFV Ember's accent: SFV's own menu gold (OKLCH 0.80 0.115 88).
constexpr unsigned kGold = 0xDDB961;

// Band: SFV's charcoal menu bands as Ember's grounds. One ramp at OKLCH hue
// 95 with chroma under 0.01 for the neutrals, the gold accent at hue 88, and
// danger, Ready and Pending moved to the accent's lightness (0.82) and a
// chroma near the gold's; LIVE stays a deeper red (0.66). As SFV marks a chosen tile,
// cards at rest are a step lighter than the panel. The focused card or row
// goes a step lighter than hover (header, 0x45433D) and is marked by the gold
// bar on its left alone: no box edge and no underline, which with the old
// darkest ground fringed the bar (2026-11-09, ShellTheme::barFocus). Square
// corners, no cut, upright titles in capitals, gold hairlines closing the
// header and opening the hint bar, and secondary buttons edged in the border
// tone. Every text pair is at least as far from its ground as SF4 Ember's
// lowest (section 17).
constexpr ember::ShellTheme Band = {
	0x100F0D, 0x282824, 0x191816, 0x2F2E2A, 0x45433D, 0x373530, 0x41403C, 0xF2DD9D, 0xEDECE0, 0xC0BEB4, 0xEE5A50, 0xEEB4AB, 0xA2D3A2,
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

// The public room classes (the library's docs/design/SKILL_BADGE.md) as SFV's
// own leagues: the bridge rates SFV Ember's public room games in a rating of
// their own, apart from USF4's, in the same ten bands, and SFV Ember names
// and draws each band as one of SFV's ten league tiers (its Super and Ultra
// steps left out): Rookie to Warlord, with the game's league icons
// (/Game/CommonAsset/LeagueIcon, read from the install as the fighters' faces
// are, SfvArt), drawn at their own shape. Their names are the game's own in
// the player's language once the game has given them (ranks::Name, read at
// the main menu); these English names stand in until then. The maintainer's decision:
// "The ranks for sfv will follow the ranks from the game visually"
// (section 28).
static_assert(sfve::ranks::kBandLeague.size() == ember::RankingClassCount, "a league for every band");
constexpr ember::RankingClass SfvRanks[ember::RankingClassCount] = {
	{"LI_rookie", "Rookie", nullptr},
	{"LI_bronze", "Bronze", nullptr},
	{"LI_silver", "Silver", nullptr},
	{"LI_gold", "Gold", nullptr},
	{"LI_platinum", "Platinum", nullptr},
	{"LI_diamond", "Diamond", nullptr},
	{"LI_master", "Master", nullptr},
	{"LI_grand_master", "Grand Master", nullptr},
	// The game's own spelling of its asset.
	{"LI_ultimet_grand_master", "Ultimate Grand Master", nullptr},
	{"LI_warlord", "Warlord", nullptr},
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
		// SFV's leagues for the public room classes (section 28).
		SfvRanks,
		// Their names as the game gives them.
		sfve::ranks::Name,
		// Rematch goes on over the room's results once the WIN is over, so a
		// press that skips SFV's scenes never sends the pair to Ready by hand.
		true,
		// Over the WIN, Back to room and Character select count only 2 s after
		// the prompt opens, so a press that skips SFV's scenes cannot decline
		// the rematch in place (EMBER_LIBRARY.md section 58).
		2.0,
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
	// Its public rooms are SFV's, apart from SF4 Ember's on the same bridge
	// (EMBER_LIBRARY.md section 24).
	"sfv",
	// Its rooms' short links open SFV Ember: the site's /v page hands the
	// code to ember-sfv://join/ (EMBER_LIBRARY.md section 46).
	"https://embernetplay.link/v#",
	// Stable and Beta releases come from the public repository (EMBER_LIBRARY.md
	// section 44). The update check refuses a redirect, so this must stay the
	// repository's exact name. SFV Ember has no Nightly line: that channel has
	// no repository, and its check offers nothing and makes no request. A
	// release package is named sfv-ember-<version>.zip (scripts/package.ps1).
	"Confetti3/SFV-Ember",
	"",
	"sfv-ember-",
	L"sfv-ember-update-",
	L"sfve-updater-",
	L"sfv-ember-update.log",
	// Launcher.exe starts the game with the mod injected (src/launcher);
	// Updater.exe installs a package.
	L"Launcher.exe",
	L"Updater.exe",
	L"sfv_ember.dll",
	L"StreetFighterV.exe",
	// Steam's app 310950. StreetFighterV.exe at the install folder's top is
	// UE4's small bootstrap, which starts the real game from
	// StreetFighterV\Binaries\Win64: that one is started, in its own folder.
	{"310950", L"StreetFighterV", L"StreetFighterV\\Binaries\\Win64"},
	// Discord's companion: the mod starts it beside the helper (EmberHost),
	// under SF4 Ember's Discord application (EMBER_LIBRARY.md section 33).
	L"ember-discord.exe",
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

namespace sfve::ranks {

namespace {
// Written once, before `s_published` is set, and never again.
std::array<std::string, 10> s_names;
std::atomic<bool> s_published{false};
std::atomic<bool> s_claimed{false};
} // namespace

const char* Name(int band)
{
	if (band < 1 || band > (int)s_names.size() || !s_published.load(std::memory_order_acquire)) return "";
	return s_names[(size_t)band - 1].c_str();
}

void Publish(const std::array<std::string, 10>& names)
{
	if (s_claimed.exchange(true)) return;
	s_names = names;
	s_published.store(true, std::memory_order_release);
}

} // namespace sfve::ranks
