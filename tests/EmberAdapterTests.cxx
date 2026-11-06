// SFV's game definitions for Ember (src/ember/), checked without the game:
// the catalog filled from a content list, its pick and stage rules and the
// battle a room's match launches, the product's names, the pick labels and
// pictures, the adapter with no battle in play, and the desync check over two
// real rollback sessions.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvVerification.hxx"

#include "common/SelectionPick.hxx"
#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/ProductIdentity.hxx"
#include "common/StageCatalog.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::testbed;
using sf4e::SessionProtocol::BattleHashV2;
using sf4e::selection::Pick;

namespace {

mod::CostumeContent Costume(int id, std::vector<int> colours, const char* name = "", bool installed = true)
{
	return {id, std::move(colours), name, installed};
}

mod::FighterContent Fighter(const char* code, std::vector<mod::CostumeContent> costumes, const char* name = "",
	bool installed = true)
{
	return {code, std::move(costumes), name, installed};
}

// A content list shaped as the game's: fighters in the game's order with
// their costume numbers (with gaps, as the spec leaves some out) and each
// costume's colours (EX colours from 100); names where the game gives one;
// one fighter and one costume this copy has not installed; the stages' names
// where the game gives one (none for the second).
mod::ContentLists TestContent()
{
	return {{Fighter("RYU", {Costume(0, {0, 1, 2}, "Story"), Costume(1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}),
					 Costume(4, {0, 1, 100, 101}, "Nostalgia")},
				 "Ryu"),
				Fighter("CNL", {Costume(0, {0, 1}), Costume(2, {0})}, "Chun-Li"),
				Fighter("KEN", {Costume(0, {0})}, "Ken"),
				Fighter("CMY", {Costume(3, {5, 6})}, "", false),
				Fighter("BSN", {Costume(0, {0, 1}), Costume(1, {0, 1}, "", false)}, "M. Bison")},
		{"S01", "S02", "S25"}, {"Stage One", "", "Stage Twenty-five"}};
}

// Lists with only the codes given, each fighter in one costume and colour.
mod::ContentLists CodesOnly(std::vector<std::string> fighters, std::vector<std::string> stages)
{
	mod::ContentLists lists;
	for (const std::string& code : fighters) lists.fighters.push_back(Fighter(code.c_str(), {Costume(0, {0})}));
	lists.stages = std::move(stages);
	return lists;
}

void FillTestCatalog()
{
	REQUIRE(FillCatalog(TestContent()));
}

Pick PickOf(int fighter, int costume = 0, int color = 0)
{
	Pick pick;
	pick.fighter = fighter;
	pick.costume = costume;
	pick.color = color;
	return pick;
}

// A match of the fighters in their first costume and colour.
RoomMatch MatchOf(int p1, int p2, std::int64_t stage, int rounds = 3, int seconds = 99)
{
	RoomMatch match;
	match.picks[0] = PickOf(p1);
	match.picks[1] = PickOf(p2);
	for (Pick& pick : match.picks) ember::Catalog().normalizePick(pick, false, nullptr, nullptr);
	match.stage = stage;
	match.rounds = rounds;
	match.roundSeconds = seconds;
	return match;
}

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

// Declared first: the catalog is filled once per process, and this case
// checks it before that.
TEST_CASE("the catalog is empty until it is filled from the game's content lists")
{
	CHECK_FALSE(ember::Usable(ember::Catalog()));
	CHECK_FALSE(ember::ValidFighter(0));
	// The shell names fighter 0 before the lists are filled.
	CHECK(std::string(ember::Catalog().fighterName(0)) == "?");
	CHECK_FALSE(FillCatalog(CodesOnly({}, {"S01"})));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {})));
	// A pick space needs a costume for every fighter and a colour for every
	// costume, and numbers a room's byte carries.
	mod::ContentLists bare = CodesOnly({"RYU"}, {"S01"});
	bare.fighters[0].costumes.clear();
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(0, {})};
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(256, {0})};
	CHECK_FALSE(FillCatalog(bare));
	bare.fighters[0].costumes = {Costume(0, {256})};
	CHECK_FALSE(FillCatalog(bare));
	CHECK_FALSE(ember::Usable(ember::Catalog()));

	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(ember::Usable(catalog));
	CHECK(catalog.fighterCount == 5);
	// One past the highest costume (RYU's 4) and colour (RYU's EX02, 101).
	CHECK(catalog.costumeLimit == 5);
	CHECK(catalog.colorLimit == 102);
	CHECK(catalog.ultraChoices == 1);
	// The table rules the shell offers: the room's that SFV plays.
	CHECK_FALSE(catalog.editions);
	CHECK_FALSE(catalog.rematchInPlace); // no prompt over SFV's WIN yet
	REQUIRE(catalog.roundCountChoices == 4);
	REQUIRE(catalog.roundTimeChoices == 4);
	const int rounds[] = {1, 3, 5, 7}, times[] = {30, 60, 99, 300};
	for (int i = 0; i < 4; i++) {
		CHECK(catalog.roundCounts[i] == rounds[i]);
		CHECK(catalog.roundTimes[i] == times[i]);
	}
	CHECK_FALSE(ember::PlayableRoundCount(15));
	CHECK_FALSE(ember::PlayableRoundTime(9999));
	CHECK(FighterCode(0) == "RYU");
	CHECK(FighterCode(4) == "BSN");
	CHECK(FighterCode(5).empty());
	CHECK(FighterCode(-1).empty());
	CHECK(StageCode(2) == "S25");
	CHECK(StageCode(3).empty());
	// The shell names a stage by the game's name, by its code where it has none.
	CHECK(StageName(0) == "Stage One");
	CHECK(StageName(1) == "S02");
	CHECK(StageName(2) == "Stage Twenty-five");
	CHECK(StageName(3).empty());
	CHECK(FighterId(L"KEN") == 2);
	CHECK_FALSE(FighterId(L"ZZZ").has_value());
	CHECK(StageId(L"S25") == 2);
	CHECK_FALSE(StageId(L"RYU").has_value());
	// Each fighter's costumes in the game's order, and each costume's colours.
	CHECK(CostumeIds(0, false) == std::vector<int>{0, 1, 4});
	CHECK(CostumeIds(4, false) == std::vector<int>{0, 1});
	CHECK(CostumeIds(4, true) == std::vector<int>{0}); // BSN's second is not installed here
	CHECK(CostumeIds(5, false).empty());
	CHECK(ColourIds(0, 4) == std::vector<int>{0, 1, 100, 101});
	CHECK(ColourIds(1, 2) == std::vector<int>{0});
	CHECK(ColourIds(1, 1).empty()); // CNL has no costume 1

	// Filled once: the same lists again are fine, other lists are refused.
	CHECK(FillCatalog(TestContent()));
	mod::ContentLists renamed = TestContent(); // names and installs are this copy's
	renamed.fighters[0].name = "Ryu in another language";
	renamed.fighters[3].installed = true;
	CHECK(FillCatalog(renamed));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {"S01"})));
	mod::ContentLists recoloured = TestContent();
	recoloured.fighters[2].costumes[0].colours.push_back(1);
	CHECK_FALSE(FillCatalog(recoloured));
	CHECK(ember::Catalog().fighterCount == 5);
}

TEST_CASE("a pick names a fighter of the roster, one of its costumes and one of that costume's colours")
{
	FillTestCatalog();
	const auto valid = ember::Catalog().validPick;
	CHECK(valid(PickOf(0), false));
	CHECK(valid(PickOf(0, 4, 101), true)); // a room's edition rule means nothing to SFV
	CHECK(valid(PickOf(3, 3, 6), false));  // the spec's, though this copy lacks it
	CHECK_FALSE(valid(PickOf(5), false));
	CHECK_FALSE(valid(PickOf(-1), false));
	CHECK_FALSE(valid(PickOf(0, 2, 0), false));   // RYU has no costume 2
	CHECK_FALSE(valid(PickOf(0, 0, 3), false));   // nor colour 3 in costume 0
	CHECK_FALSE(valid(PickOf(0, 4, 2), false));   // nor colour 2 in costume 4
	CHECK_FALSE(valid(PickOf(1, 1, 0), false));   // a costume of another fighter
	CHECK_FALSE(valid(PickOf(3, 0, 0), false));   // CMY's only costume is 3
	CHECK_FALSE(valid(PickOf(0, -1, 0), false));
	Pick ultra = PickOf(0);
	ultra.ultra = 1;
	CHECK_FALSE(valid(ultra, false));
	Pick handicap = PickOf(0);
	handicap.handicap = 2;
	CHECK_FALSE(valid(handicap, false));
	// SF4's edition, personal action and win quote ride along unread.
	Pick sf4Fields = PickOf(1);
	sf4Fields.edition = 14;
	sf4Fields.personalAction = 255;
	sf4Fields.winQuote = 3;
	CHECK(valid(sf4Fields, false));
}

TEST_CASE("a stage is one of the game's stages, by its place in the list")
{
	FillTestCatalog();
	const auto valid = ember::Catalog().validStage;
	CHECK(valid(0));
	CHECK(valid(2));
	CHECK_FALSE(valid(3));
	CHECK_FALSE(valid(-1));
}

TEST_CASE("a room's match launches its fighters, stage, rounds and time by the game's codes")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(2, 0, 1, 5, 60);
	match.picks[1] = PickOf(0, 4, 101); // RYU's EX02 in costume 4
	const std::optional<mod::MatchPicks> picks = LaunchPicks(match);
	REQUIRE(picks);
	CHECK(picks->target == mod::LaunchTarget::Versus);
	CHECK(picks->players[0].fighter == L"KEN");
	CHECK(picks->players[0].costume == 0);
	CHECK(picks->players[0].colour == 0);
	CHECK(picks->players[1].fighter == L"RYU");
	CHECK(picks->players[1].costume == 4);
	CHECK(picks->players[1].colour == 101);
	CHECK(picks->rules.stage == L"S02");
	CHECK(picks->rules.rounds == 5);
	CHECK(picks->rules.timeFrames == 60 * 60);

	match.picks[1] = PickOf(0, 4, 2);
	CHECK_FALSE(LaunchPicks(match));                // a colour the costume lacks
	match.picks[1] = PickOf(5);
	CHECK_FALSE(LaunchPicks(match));                // a fighter outside the roster
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 3)));     // a stage outside the list
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 99))); // a room's endless rounds
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, 9999))); // longer than SFV's clock holds
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 7, 300)));
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 1, 30)));
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 3, 546)));
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 2)));       // an even round count
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, 29)));   // shorter than SFV's shortest round
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, -1)));   // no time SFV plays
}

TEST_CASE("the build a room checks carries a fingerprint of the game's content lists")
{
	const std::string fingerprint = ContentFingerprint(TestContent());
	CHECK(fingerprint.size() == 16);
	CHECK(ContentFingerprint(TestContent()) == fingerprint);
	mod::ContentLists reordered = TestContent();
	std::swap(reordered.fighters[0], reordered.fighters[1]);
	CHECK(ContentFingerprint(reordered) != fingerprint);
	mod::ContentLists extra = TestContent();
	extra.stages.push_back("S26");
	CHECK(ContentFingerprint(extra) != fingerprint);
	// A costume or a colour more, less or moved is another pick space.
	mod::ContentLists costume = TestContent();
	costume.fighters[1].costumes.push_back(Costume(5, {0}));
	CHECK(ContentFingerprint(costume) != fingerprint);
	mod::ContentLists colour = TestContent();
	colour.fighters[0].costumes[2].colours.pop_back();
	CHECK(ContentFingerprint(colour) != fingerprint);
	mod::ContentLists moved = TestContent();
	moved.fighters[0].costumes[0].colours = {0, 1};
	moved.fighters[0].costumes[1].colours.push_back(2);
	CHECK(ContentFingerprint(moved) != fingerprint);
	// Names follow each copy's language, and what is installed is each
	// copy's own: neither changes the build.
	mod::ContentLists translated = TestContent();
	translated.stageNames = {"Etappe eins", "", ""};
	translated.fighters[0].name = "Ryu, translated";
	translated.fighters[0].costumes[0].name = "Story, translated";
	translated.fighters[3].installed = true;
	translated.fighters[4].costumes[1].installed = true;
	CHECK(ContentFingerprint(translated) == fingerprint);
	// A code that moved from one list to the other.
	CHECK(ContentFingerprint(CodesOnly({"RYU", "S01"}, {"S02"})) != ContentFingerprint(CodesOnly({"RYU"}, {"S01", "S02"})));
	CHECK(ContentFingerprint(CodesOnly({"RY", "U"}, {"S01"})) != ContentFingerprint(CodesOnly({"R", "YU"}, {"S01"})));

	FillTestCatalog();
	CHECK(MatchBuildId() == std::string(kMatchBuild) + "/" + fingerprint);
	CHECK(ember::game::Game().state.BuildId() == MatchBuildId());
	CHECK(MatchBuildId().size() <= 128); // the room's limit for a build
}

TEST_CASE("a pick is available when this copy has its fighter and costume installed")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	const ember::game::GameState& state = ember::game::Game().state;
	const sf4e::selection::Availability availability = state.FighterAvailability(4);
	CHECK(availability.ready);
	// The runtime's Ready check (NetplayRuntime__Room) asks the catalog.
	CHECK_FALSE(catalog.fighterLocked(4, availability));
	CHECK(catalog.availablePick(PickOf(4, 0, 1), false, availability, nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(4, 1, 1), false, availability, nullptr)); // costume not installed
	CHECK(catalog.fighterLocked(3, state.FighterAvailability(3)));                       // fighter not installed
	CHECK_FALSE(catalog.availablePick(PickOf(3, 3, 5), false, state.FighterAvailability(3), nullptr));
	CHECK(catalog.fighterLocked(5, availability));
	Pick noAction = PickOf(0);
	noAction.personalAction = 255;
	CHECK(catalog.availablePick(noAction, false, state.FighterAvailability(0), nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(5), false, availability, nullptr));
	CHECK_FALSE(catalog.availablePick(PickOf(4, 0, 2), false, availability, nullptr));
}

TEST_CASE("a pick is fitted to the spec: the fighter's first costume, then that costume's first colour")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	Pick pick = PickOf(5, 40, 99);
	pick.ultra = 2;
	CHECK(catalog.normalizePick(pick, false, nullptr, nullptr));
	CHECK(pick.fighter == 0);
	CHECK(pick.costume == 0);
	CHECK(pick.color == 0);
	CHECK(pick.ultra == 0);
	CHECK_FALSE(catalog.normalizePick(pick, false, nullptr, nullptr));
	// A fighter with few costumes at a high costume number, and a costume
	// with few colours at a high colour number.
	Pick few = PickOf(1, 40, 120);
	CHECK(catalog.normalizePick(few, false, nullptr, nullptr));
	CHECK(few.costume == 0);
	CHECK(few.color == 0);
	few = PickOf(1, 2, 120);
	CHECK(catalog.normalizePick(few, false, nullptr, nullptr));
	CHECK(few.costume == 2);
	CHECK(few.color == 0);
	// The first of each is the game's first, not number 0.
	Pick firsts = PickOf(3, 0, 0);
	CHECK(catalog.normalizePick(firsts, false, nullptr, nullptr));
	CHECK(firsts.costume == 3);
	CHECK(firsts.color == 5);
	// A colour of the costume stays, an EX one included.
	Pick ex = PickOf(0, 4, 101);
	CHECK_FALSE(catalog.normalizePick(ex, false, nullptr, nullptr));
	ex.color = 9; // costume 1's, not costume 4's
	CHECK(catalog.normalizePick(ex, false, nullptr, nullptr));
	CHECK(ex.color == 0);
	// What this copy has not installed is not the spec's business.
	Pick uninstalled = PickOf(4, 1, 1);
	CHECK_FALSE(catalog.normalizePick(uninstalled, false, nullptr, nullptr));
	CHECK(catalog.randomStage == -1);
	const sf4e::selection::RandomPool pool; // the facade carries SF4's pool type; SFV reads none
	CHECK(catalog.resolveStage(2, 7, pool, nullptr) == 2);
	CHECK(catalog.resolveStage(255, 7, pool, nullptr) == 0);
}

TEST_CASE("the shared screens name and list SFV's fighters by the game's names, in its order")
{
	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(std::string(catalog.fighterName(0)) == "Ryu");
	CHECK(std::string(catalog.fighterName(4)) == "M. Bison");
	CHECK(std::string(catalog.fighterName(3)) == "CMY"); // the game gave no name
	CHECK(std::string(catalog.fighterName(5)) == "?");
	CHECK(std::string(catalog.fighterName(-1)) == "?");
	// One past the highest costume number: every costume the spec has is known.
	CHECK(catalog.costumeCount(0) == 5);
	CHECK(catalog.costumeCount(3) == 4);
	CHECK(catalog.costumeCount(5) == 0);
	for (int at = 0; at < catalog.fighterCount; at++) CHECK(catalog.rosterOrder[at] == at);
}

TEST_CASE("a room's launch goes ahead only while its entry is armed")
{
	LaunchArming arming;
	CHECK_FALSE(arming.Armed(LaunchArming::Ticket::None));
	const LaunchArming::Ticket first = arming.Arm();
	CHECK(arming.Armed(first));
	arming.Drop(); // the player left the room
	CHECK_FALSE(arming.Armed(first));
	const LaunchArming::Ticket second = arming.Arm();
	CHECK((second != first));
	CHECK(arming.Armed(second));
	CHECK_FALSE(arming.Armed(first));
	const LaunchArming::Ticket third = arming.Arm(); // a later entry
	CHECK_FALSE(arming.Armed(second));
	CHECK(arming.Armed(third));
}

TEST_CASE("the product is SFV Ember, with names of its own")
{
	const ember::ProductIdentity& product = ember::Product();
	CHECK(std::string(product.branding.productName) == "SFV Ember");
	CHECK(std::string(product.branding.shortName) == "SFV Ember");
	CHECK(std::string(product.branding.gameName) == "Street Fighter V");
	// Its own look (EMBER_LIBRARY.md section 16): the gold accent, its theme and ground.
	CHECK(product.branding.accent == 0xDDB961u);
	CHECK((product.branding.theme != nullptr && product.branding.theme->underlineFocus));
	CHECK(std::wstring(product.branding.menuBackdrop) == L"brand/sfv-ember-menu-background.png");
	CHECK(product.branding.markSize == 128);
	CHECK(product.branding.mark != nullptr);
	CHECK(std::wstring(product.appDataFolder) == L"SFVEmber");
	CHECK(std::wstring(product.gameModule) == L"sfv_ember.dll");
	CHECK(std::wstring(product.gameExe) == L"StreetFighterV.exe");
	CHECK(std::wstring(product.gameLog) == L"sfv_ember.log");
	CHECK(std::string(product.urlScheme) == "ember-sfv");
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
	CHECK(std::string(product.localeOverlays[0].tag) == "en");
	CHECK(product.localeOverlays[1].tag == nullptr);
	std::string error;
	CHECK(sf4e::loc::testing::LayerOverlays(product.localeOverlays, error));
	CHECK(error.empty());
	for (const char* id : {"app.name", "shell.home_title", "screen.game", "brand.subtitle", "about.ember_detail", "runtime.open_shortcut",
			 "runtime.build_mismatch", "runtime.room_join_failed", "runtime.invite_other_build"}) {
		CHECK_FALSE(HasSf4(sf4e::loc::T(id)));
	}
	CHECK(std::string(sf4e::loc::T("brand.subtitle")).rfind("SFV", 0) == 0);
}

TEST_CASE("the pick labels are the game's names, from the catalog")
{
	FillTestCatalog();
	using namespace sf4e::ui;
	CHECK(StageLabel(0) == "Stage One");
	CHECK(StageLabel(1) == "S02"); // the game gave no name
	CHECK(StageLabel(2) == "Stage Twenty-five");
	CHECK(StageLabel(9) == "9");
	CHECK(CostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(CostumeLabel(PickOf(0, 1)) == "1"); // the game gave no name
	CHECK(ShortCostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(ShortAppearanceLabel(PickOf(0, 4, 100)) == "Nostalgia, color EX01");
	CHECK(ColourName(0) == "01");
	CHECK(ColourName(9) == "10");
	CHECK(ColourName(100) == "EX01");
	CHECK(ColourName(127) == "EX28");
	CHECK(std::string(UltraLabel(0)).empty());
	CHECK(std::string(ShortUltraLabel(0)).empty());
}

TEST_CASE("every picture of the game reports itself missing until SFV art exists")
{
	struct NoTextures final : sf4e::ui::TextureUploader {
		ImTextureID Create(unsigned, unsigned, const unsigned char*, std::string& failure) override
		{
			failure = "no renderer in a test";
			return 0;
		}
		void Release(ImTextureID) override {}
	};
	sf4e::ui::SelectionArt art(std::make_unique<NoTextures>(), L"", L"");
	CHECK(art.Portrait(0).missing);
	CHECK(art.Portrait(0, true).missing);
	CHECK(art.Character(0, 0, 0).missing);
	CHECK(art.Appearance(0, 0, 0).missing);
	CHECK(art.Ultra(0, 0).missing);
	CHECK(art.Stage(0).missing);
	CHECK(art.RankingEmblem("class_B.dds").missing);
}

TEST_CASE("the game offers no optional feature and answers as no battle while none is in play")
{
	const ember::game::Adapter& game = ember::game::Game();
	CHECK_FALSE(game.caps.training);
	CHECK_FALSE(game.caps.replays);
	CHECK_FALSE(game.caps.spectators);
	CHECK_FALSE(game.caps.padSetup);
	CHECK_FALSE(game.caps.steamProfile);
	CHECK_FALSE(game.caps.customContent);
	CHECK(game.state.Language().empty());
	CHECK_FALSE(game.state.MainMenuInFront()); // no scene manager seen
	CHECK(game.state.ForegroundScreen().empty());
	CHECK(game.battle.Enter() == ember::game::Battle::Entry::Deferred); // no room
	CHECK_FALSE(game.battle.Live());
	CHECK_FALSE(game.battle.ResultHeld());
	CHECK(game.battle.GameResult() == sf4e::room::MatchResult::Abort); // no match end taken
	game.battle.DropNextBattle(); // nothing armed
	CHECK(game.battle.RematchChoices() == 0u);
	int frame = -1;
	CHECK_FALSE(game.battle.FightRunning(frame));
	CHECK(frame == -1);
	CHECK(game.battle.Sample().pingMs == -1);
	unsigned mapped = 1, raw = 1;
	CHECK_FALSE(game.input.ReadAssignedInput({}, 0, mapped, raw));
	CHECK(mapped == 0u);
	CHECK(raw == 0u);
	CHECK_FALSE(game.replays.Ready());
	CHECK(game.replays.ArchivePath().empty());
	CHECK_FALSE(game.training.ControlsAvailable());
}

namespace {

// Both peers' checks, each publishing into the other's remote hashes.
struct Exchanges {
	CheckpointExchange a, b;
	std::map<int, BattleHashV2> toA, toB; // what each has received
	std::vector<BattleHashV2> publishedByA;
	std::optional<CheckpointExchange::Mismatch> mismatchA, mismatchB;

	void Step(const LoopbackPair& pair)
	{
		const CheckpointExchange::Publish fromA = [this](const BattleHashV2& hash) {
			publishedByA.push_back(hash);
			toB[hash.frameIdx] = hash;
			return true;
		};
		const CheckpointExchange::Publish fromB = [this](const BattleHashV2& hash) {
			toA[hash.frameIdx] = hash;
			return true;
		};
		if (auto m = a.Step(pair.a, &fromA, toA); m && !mismatchA) mismatchA = m;
		if (auto m = b.Step(pair.b, &fromB, toB); m && !mismatchB) mismatchB = m;
	}

	// Plays both peers to `frames`, stepping the checks after every frame, as
	// the room client does.
	void RunTo(LoopbackPair& pair, int frames)
	{
		for (int target = pair.a.Frame() + 1; target <= frames; target++) {
			REQUIRE(pair.RunTo(target));
			Step(pair);
		}
		pair.RunTo(frames + 20); // the last frames confirmed
		Step(pair);
		Step(pair); // hashes that crossed in the last step
	}
};

} // namespace

TEST_CASE("players publish one hash per confirmed checkpoint and agree while their states do")
{
	// With an input delay GGPO confirms frames before they are run and saved;
	// no checkpoint may be lost to that.
	int delay = 0;
	SUBCASE("no input delay") { delay = 0; }
	SUBCASE("input delay 2") { delay = 2; }
	LoopbackPair pair(kBugNone, delay);
	Exchanges exchanges;
	exchanges.RunTo(pair, 400);
	CHECK_FALSE(exchanges.mismatchA);
	CHECK_FALSE(exchanges.mismatchB);
	REQUIRE(exchanges.publishedByA.size() >= 6);
	for (size_t i = 0; i < exchanges.publishedByA.size(); i++) {
		CHECK(exchanges.publishedByA[i].frameIdx == (int)(i + 1) * RollbackSession::kCheckpointInterval);
	}
	// Each sent hash is the session's own, with nothing per character.
	const BattleHashV2& sent = exchanges.publishedByA.back();
	uint64_t hash = 0;
	REQUIRE(pair.a.HashAt(sent.frameIdx, &hash));
	CHECK(sent.overall == hash);
	CHECK(sent.flow == 0u);
	CHECK(sent.chara0 == 0u);
	CHECK(sent.chara1 == 0u);
	CHECK(sent.fromPlayer);
	// Compared hashes are erased; only ones not yet confirmed here may wait.
	for (const auto& [frame, waiting] : exchanges.toA) CHECK(frame > pair.a.LastConfirmedFrame() + 1);
}

TEST_CASE("a real desync shows as a mismatch at the first checkpoint after it, on both players")
{
	constexpr int kDesyncFrom = 250;
	LoopbackPair pair(kBugNone, 0);
	pair.gameB.DesyncFrom(kDesyncFrom);
	Exchanges exchanges;
	exchanges.RunTo(pair, 420);
	REQUIRE(exchanges.mismatchA);
	REQUIRE(exchanges.mismatchB);
	CHECK(exchanges.mismatchA->frame == 300);
	CHECK(exchanges.mismatchB->frame == 300);
	CHECK(exchanges.mismatchA->local == exchanges.mismatchB->remote);
	CHECK(exchanges.mismatchA->remote == exchanges.mismatchB->local);
}

TEST_CASE("a client that watches publishes nothing, and a watcher's hash never counts as a mismatch")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(200));
	pair.RunTo(220);
	CheckpointExchange watcher;
	std::map<int, BattleHashV2> remote;
	BattleHashV2 watched;
	watched.frameIdx = 60;
	watched.overall = 0x1234;
	watched.fromPlayer = false;
	remote[60] = watched;
	CHECK_FALSE(watcher.Step(pair.a, nullptr, remote));
	CHECK(remote.empty()); // compared, so erased
}

TEST_CASE("a check that starts late publishes from the oldest checkpoint the session still keeps")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	CheckpointExchange exchange;
	std::vector<int> published;
	const CheckpointExchange::Publish send = [&published](const BattleHashV2& hash) {
		published.push_back(hash.frameIdx);
		return true;
	};
	std::map<int, BattleHashV2> remote;
	exchange.Step(pair.a, &send, remote);
	// The pair hashes every frame (verifyLoads) and the session keeps the
	// last 256 of them, 165 to 420 here: 60 and 120 are gone.
	REQUIRE(published.size() >= 4);
	for (size_t i = 0; i < published.size(); i++) CHECK(published[i] == 180 + (int)i * 60);
}

namespace {

BattleHashV2 PlayerHash(int frame, std::uint64_t overall)
{
	BattleHashV2 hash;
	hash.frameIdx = frame;
	hash.overall = overall;
	hash.fromPlayer = true;
	return hash;
}

} // namespace

TEST_CASE("the check starts over when the session's confirmed frame goes back")
{
	constexpr std::uint64_t kWrong = 0xBAD;
	CheckpointExchange exchange;
	std::vector<int> published;
	const CheckpointExchange::Publish send = [&published](const BattleHashV2& hash) {
		published.push_back(hash.frameIdx);
		return true;
	};
	std::map<int, BattleHashV2> remote;
	{
		LoopbackPair first(kBugNone, 0);
		REQUIRE(first.RunTo(140));
		first.RunTo(160);
		remote[60] = PlayerHash(60, kWrong);
		CHECK(exchange.Step(first.a, &send, remote));
		// One mismatch a battle: the next is not reported again.
		remote[120] = PlayerHash(120, kWrong);
		CHECK_FALSE(exchange.Step(first.a, &send, remote));
	}
	REQUIRE(!published.empty());
	CHECK(published.front() == 60);
	published.clear();
	LoopbackPair second(kBugNone, 0);
	REQUIRE(second.RunTo(80));
	second.RunTo(100);
	// A hash of the battle before, still waiting, is dropped, not compared
	// with this battle's checkpoint of the same frame.
	remote[60] = PlayerHash(60, kWrong);
	CHECK_FALSE(exchange.Step(second.a, &send, remote));
	CHECK(remote.empty());
	REQUIRE(!published.empty());
	CHECK(published.front() == 60);
	// The new battle's own mismatch is reported.
	remote[60] = PlayerHash(60, kWrong);
	const auto mismatch = exchange.Step(second.a, &send, remote);
	REQUIRE(mismatch);
	CHECK(mismatch->frame == 60);
	CHECK(mismatch->remote == kWrong);
}


// Declared last: sealing is for the rest of the process, and every case above
// fills the catalog.
TEST_CASE("once Ember's runtime starts, the catalog refuses every fill")
{
	FillTestCatalog();
	SealCatalog();
	CHECK_FALSE(FillCatalog(TestContent()));
	CHECK_FALSE(FillCatalog(CodesOnly({"RYU"}, {"S01"})));
	CHECK(ember::Catalog().fighterCount == 5);
}