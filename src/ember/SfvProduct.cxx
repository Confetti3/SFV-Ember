// SFV Ember's identity (the library's common/ProductIdentity.hxx): its own
// names, folders, named objects and log names, so it never shows SF4 Ember's
// and never shares its settings folder, link mailboxes or logs with it.
// Constant-initialized, as SF4 Ember's is, so a crash handler or a static
// initializer can read it.
//
// The names come from what SFV Ember already calls itself: "SFV Ember" (the
// proxy's messages), the SFVEmber folder and sfv_ember.log (mod/Log.cxx),
// sfv_ember.dll and StreetFighterV.exe (proxy/xinput_proxy.cxx), and "sfve"
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
// the room's build checks and the shortcut hint. English replaces every translation of them
// too (Localization.cxx), until SFV Ember has translations of its own. The
// launcher's, updater's, recovery's and Training's words are not shown here.
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

msgid "runtime.invite_other_build"
msgstr "The host is on a different SFV Ember build or has other fighters and stages installed. Both players need the same."
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
	},
	L"SFVEmber",
	// Settings beside the mod in the game's folder (SFVEmber\settings.json),
	// as its logs are (mod/Log.cxx): SFV Ember has no installer of its own.
	true,
	L"SFVEmber",
	L"Local\\SFVEmber.Launcher.",
	L"Local\\sfve-video-",
	L".sfve-video-tmp",
	// A scheme of its own, so a link never opens SF4 Ember (section 5.4 of
	// EMBER_LIBRARY.md is still open).
	"ember-sfv",
	// No releases yet: nothing to update from, and no package to name.
	"",
	"",
	"sfv-ember-",
	L"sfv-ember-update-",
	L"sfve-updater-",
	L"sfv-ember-update.log",
	// No launcher, updater or Discord bridge yet: the xinput1_3.dll proxy
	// loads the mod when the game starts.
	L"",
	L"",
	L"sfv_ember.dll",
	L"StreetFighterV.exe",
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
