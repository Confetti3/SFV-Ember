// SFV Ember's identity (the library's common/ProductIdentity.hxx): its own
// names, folders, named objects and log names, so it never shows SF4 Ember's
// and never shares its settings folder, link mailboxes or logs with it.
// Constant-initialized, as SF4 Ember's is, so a crash handler or a static
// initializer can read it.
//
// The names come from what SFV Ember already calls itself: "SFV Ember" (the
// proxy's messages), the SFVEmber folder and sfv_ember.log (mod/Log.cxx),
// sfv_ember.dll and StreetFighterV.exe (proxy/xinput_proxy.cxx), and "sfve"
// for its named objects (ControlPipe, MultiInstance). The header mark and the
// menu backdrop are Ember's own until SFV Ember has art of its own; the
// accent is Ember's orange, which SFV's branding keeps (EMBER_LIBRARY.md 2.1).

#include "common/ProductIdentity.hxx"
#include "EmbeddedBrand.hxx"

namespace {

const ember::LocaleOverlay NoLocaleOverlays[] = {{nullptr, nullptr}};

constexpr ember::ProductIdentity SfvEmber = {
	{
		"SFV Ember",
		"SFV Ember",
		"Street Fighter V",
		0xFF8738,
		sf4e::ui::brand::Pixels,
		sf4e::ui::brand::Size,
		L"brand/ember-menu-background.png",
	},
	L"SFVEmber",
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
	NoLocaleOverlays,
};

} // namespace

namespace ember {
const ProductIdentity& Product() { return SfvEmber; }
} // namespace ember
