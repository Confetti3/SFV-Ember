// SFV Ember's identity (SfvProduct.cxx), checked without the game: its
// names, look and words, its launcher, updater and folders, and its public
// room classes. None of it names SF4 Ember.

#include <doctest/doctest.h>

#include "ember/SfvRankNames.hxx"

#include "common/Localization.hxx"
#include "common/JoinLink.hxx"
#include "common/ProductIdentity.hxx"
#include "launcher/update/github_release_client.hxx"
#include "ui/NameMarks.hxx"

#include <array>
#include <cstdint>
#include <string>

namespace {

bool HasSf4(const char* text)
{
	return text && std::string(text).find("SF4") != std::string::npos;
}

bool HasSf4(const wchar_t* text)
{
	return text && (std::wstring(text).find(L"SF4") != std::wstring::npos ||
					   std::wstring(text).find(L"sf4") != std::wstring::npos);
}

} // namespace

TEST_CASE("the product is SFV Ember, with names of its own")
{
	const ember::ProductIdentity& product = ember::Product();
	CHECK(std::string(product.branding.productName) == "SFV Ember");
	CHECK(std::string(product.branding.shortName) == "SFV Ember");
	CHECK(std::string(product.branding.gameName) == "Street Fighter V");
	// Its own look (EMBER_LIBRARY.md section 17): the gold accent, its theme and ground.
	CHECK(product.branding.accent == 0xDDB961u);
	CHECK((product.branding.theme != nullptr && product.branding.theme->barFocus));
	// The focused ground is a step above hover, not the darkest tone (section 36).
	CHECK(product.branding.theme->header == 0x45433Du);
	CHECK(std::wstring(product.branding.menuBackdrop) == L"brand/sfv-ember-menu-background.png");
	CHECK(product.branding.markSize == 128);
	CHECK(product.branding.mark != nullptr);
	CHECK(std::wstring(product.appDataFolder) == L"SFVEmber");
	CHECK(std::wstring(product.gameModule) == L"sfv_ember.dll");
	CHECK(std::wstring(product.gameExe) == L"StreetFighterV.exe");
	CHECK(std::wstring(product.gameLog) == L"sfv_ember.log");
	CHECK(std::string(product.urlScheme) == "ember-sfv");
	CHECK(std::string(product.roomGame) == "sfv");
	// A room's short link is SFV Ember's /v# form, which the site opens in
	// SFV Ember; the helper's /j# link is shown in it.
	CHECK(std::string(product.roomLinkPrefix) == "https://embernetplay.link/v#");
	CHECK(sf4e::join_link::ShortLink("7K3M0X1RT9PZ") == "https://embernetplay.link/v#7K3M-0X1R-T9PZ");
	CHECK(sf4e::join_link::ProductShortLink("https://embernetplay.link/j#7K3M-0X1R-T9PZ") == "https://embernetplay.link/v#7K3M-0X1R-T9PZ");
	CHECK(ember::KernelObjectName(L"JoinLink") == L"Local\\SFVEmberJoinLink");
	CHECK(ember::SchemeLink("join/ABC") == "ember-sfv://join/ABC");
	for (const char* text : {product.branding.productName, product.branding.shortName, product.urlScheme,
			 product.stableRepo, product.nightlyRepo, product.releaseZipPrefix}) {
		CHECK_FALSE(HasSf4(text));
	}
	for (const wchar_t* text : {product.appDataFolder, product.objectStem, product.launcherInstancePrefix,
			 product.videoLinkPrefix, product.videoTemporarySuffix, product.updateTempPrefix, product.updaterCopyPrefix,
			 product.updateLog, product.gameModule, product.gameExe, product.gameLog, product.crashLog,
			 product.crashDumpPrefix}) {
		CHECK_FALSE(HasSf4(text));
	}
	// SFV Ember's English over the shared words that name SF4 Ember on the
	// screens SFV shows; the catalog takes them.
	REQUIRE(product.localeOverlays[0].tag != nullptr);
	CHECK(std::string(product.localeOverlays[0].tag) == "en"); // and every other language (LocaleOverlayTests)
	std::string error;
	CHECK(sf4e::loc::testing::LayerOverlays(product.localeOverlays, error));
	CHECK(error.empty());
	for (const char* id : {"app.name", "shell.home_title", "screen.game", "brand.subtitle", "about.ember_detail", "runtime.open_shortcut",
			 "runtime.build_mismatch", "runtime.room_join_failed", "runtime.invite_other_build"}) {
		CHECK_FALSE(HasSf4(sf4e::loc::T(id)));
	}
	CHECK(std::string(sf4e::loc::T("brand.subtitle")).rfind("SFV", 0) == 0);
	// The launcher's, recovery's, updater's and reports' words: none names SF4,
	// its game, its files or its folders.
	for (const char* id : {"launcher.update_failed", "launcher.recovery_opened", "launcher.game_not_found",
			 "launcher.sidecar_missing", "launcher.runtime_outdated", "launcher.already_running", "launcher.start_failed",
			 "launcher.game_runs_as_admin", "launcher.game_error", "launcher.game_crashed", "launcher.game_wrong_dll",
			 "launcher.game_crashed_files", "launcher.game_crashed_sending", "recovery.d3d_failed",
			 "recovery.choose_folder_title", "recovery.folder_invalid", "recovery.choose_folder_detail",
			 "recovery.close_detail", "updates.install_detail", "updates.close_detail", "updates.start_game",
			 "updates.start_game_detail", "services.diagnostics_saved", "services.update_prepared", "update.no_zip_asset",
			 "update.close_sf4", "update.download_failed", "update.missing_binaries", "update.notice.waiting_launcher",
			 "update.notice.reopens", "reports.payload", "reports.history_unreadable_detail"}) {
		const std::string text = sf4e::loc::T(id);
		INFO(id << ": " << text);
		for (const char* sf4 : {"SF4", "sf4e", "SSFIV", "Ultra Street Fighter IV", "Sidecar", "(x86)", "sf4-netplay"}) {
			CHECK(text.find(sf4) == std::string::npos);
		}
	}
	CHECK(std::string(sf4e::loc::T("launcher.runtime_outdated")).find("(x64)") != std::string::npos);
	CHECK(std::string(sf4e::loc::T("launcher.start_failed")).find("%APPDATA%\\SFVEmber\\logs\\launcher.log") != std::string::npos);
}

TEST_CASE("SFV Ember's launcher, updater and folders")
{
	const ember::ProductIdentity& product = ember::Product();
	// Settings and logs under %APPDATA%\SFVEmber, as SF4 Ember's are under %APPDATA%\sf4e.
	CHECK_FALSE(product.settingsBesideModule);
	CHECK(std::wstring(product.launcherExe) == L"Launcher.exe");
	CHECK(std::wstring(product.updaterExe) == L"Updater.exe");
	CHECK(std::wstring(product.discordExe) == L"ember-discord.exe");
	CHECK(std::wstring(product.launcherLog) == L"launcher.log");
	// Steam's app 310950; the real game, not UE4's bootstrap at the install folder's top.
	CHECK(std::string(product.steam.appId) == "310950");
	CHECK(std::wstring(product.steam.installDir) == L"StreetFighterV");
	CHECK(std::wstring(product.steam.exeFolder) == L"StreetFighterV\\Binaries\\Win64");
	// Stable and Beta from the public repository (section 44), named exactly:
	// the check refuses a redirect.
	CHECK(std::string(product.stableRepo) == "Confetti3/SFV-Ember");
	CHECK(std::string(product.releaseZipPrefix) == "sfv-ember-");
	// No Nightly line: that channel's check succeeds, offers nothing and asks
	// GitHub nothing.
	CHECK(std::string(product.nightlyRepo).empty());
	const auto nightly = sf4e::launcher::CheckForUpdate(sf4e::launcher::UpdateChannel::Nightly);
	CHECK(nightly.ok);
	CHECK(nightly.error.empty());
	CHECK_FALSE(nightly.updateAvailable);
	CHECK(nightly.latestVersion.empty());
}

TEST_CASE("SFV Ember's public room classes are SFV's ten league tiers, each with its league icon")
{
	// EMBER_LIBRARY.md section 28: the bridge's ten bands, band 1 first.
	const ember::RankingClass* ranks = ember::Product().branding.rankingClasses;
	REQUIRE(ranks != nullptr);
	const char* names[ember::RankingClassCount] = {"Rookie", "Bronze", "Silver", "Gold", "Platinum", "Diamond", "Master",
		"Grand Master", "Ultimate Grand Master", "Warlord"};
	const char* icons[ember::RankingClassCount] = {"LI_rookie", "LI_bronze", "LI_silver", "LI_gold", "LI_platinum", "LI_diamond",
		"LI_master", "LI_grand_master", "LI_ultimet_grand_master", "LI_warlord"};
	for (int band = 0; band < ember::RankingClassCount; band++) {
		INFO("band " << band + 1);
		REQUIRE(ranks[band].name != nullptr);
		CHECK(std::string(ranks[band].name) == names[band]);
		CHECK(ranks[band].msgid == nullptr);
		CHECK(std::string(ranks[band].emblem) == icons[band]);
		// The shell names each band as SFV does.
		CHECK(sf4e::ui::ClassName(static_cast<std::uint8_t>(band + 1)) == names[band]);
	}
	CHECK(sf4e::ui::ClassName(0).empty());
	CHECK(sf4e::ui::ClassName(ember::RankingClassCount + 1).empty());
	// The game's own names come from the game at the main menu (none runs
	// in a test): until then the English names stand, and the bands map to
	// the game's leagues 0, 1, 4, ... 19 (ID_CMN_LEAGUE_00NN).
	CHECK(ember::Product().branding.rankingClassName == &sfve::ranks::Name);
	for (int band = 0; band <= ember::RankingClassCount + 1; band++) CHECK(std::string(sfve::ranks::Name(band)).empty());
	CHECK(sfve::ranks::kBandLeague == std::array<int, 10>{0, 1, 4, 7, 10, 13, 16, 17, 18, 19});
	CHECK(sfve::ranks::kBandLeague.back() == sfve::ranks::kLeagues - 1);
}
