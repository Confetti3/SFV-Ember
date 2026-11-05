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
#include "mod/HelperLink.hxx"

#include "common/GameCatalog.hxx"
#include "common/ProductIdentity.hxx"
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

// A content list as GameContent reads one: codes in the game's order.
mod::ContentLists TestContent()
{
	return {{"RYU", "CNL", "KEN", "CMY", "BSN"}, {"S01", "S02", "S25"}};
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

RoomMatch MatchOf(int p1, int p2, std::int64_t stage, int rounds = 3, int seconds = 99)
{
	RoomMatch match;
	match.picks[0] = PickOf(p1, 2, 7);
	match.picks[1] = PickOf(p2);
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
	CHECK_FALSE(FillCatalog({{}, {"S01"}}));
	CHECK_FALSE(FillCatalog({{"RYU"}, {}}));

	FillTestCatalog();
	const ember::GameCatalog& catalog = ember::Catalog();
	CHECK(ember::Usable(catalog));
	CHECK(catalog.fighterCount == 5);
	CHECK(catalog.costumeLimit == kVariantLimit);
	CHECK(catalog.colorLimit == kVariantLimit);
	CHECK(catalog.ultraChoices == 1);
	CHECK(FighterCode(0) == "RYU");
	CHECK(FighterCode(4) == "BSN");
	CHECK(FighterCode(5).empty());
	CHECK(FighterCode(-1).empty());
	CHECK(StageCode(2) == "S25");
	CHECK(StageCode(3).empty());

	// Filled once: the same lists again are fine, other lists are refused.
	CHECK(FillCatalog(TestContent()));
	CHECK_FALSE(FillCatalog({{"RYU"}, {"S01"}}));
	CHECK(ember::Catalog().fighterCount == 5);
}

TEST_CASE("a pick names a fighter of the roster, a costume and a colour, and nothing SFV lacks")
{
	FillTestCatalog();
	const auto valid = ember::Catalog().validPick;
	CHECK(valid(PickOf(0), false));
	CHECK(valid(PickOf(4, 99, 99), true)); // a room's edition rule means nothing to SFV
	CHECK_FALSE(valid(PickOf(5), false));
	CHECK_FALSE(valid(PickOf(-1), false));
	CHECK_FALSE(valid(PickOf(0, kVariantLimit, 0), false));
	CHECK_FALSE(valid(PickOf(0, 0, kVariantLimit), false));
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
	const std::optional<mod::MatchPicks> picks = LaunchPicks(MatchOf(2, 0, 1, 5, 60));
	REQUIRE(picks);
	CHECK(picks->target == mod::LaunchTarget::Versus);
	CHECK(picks->players[0].fighter == L"KEN");
	CHECK(picks->players[0].costume == 2);
	CHECK(picks->players[0].colour == 7);
	CHECK(picks->players[1].fighter == L"RYU");
	CHECK(picks->players[1].costume == 0);
	CHECK(picks->rules.stage == L"S02");
	CHECK(picks->rules.rounds == 5);
	CHECK(picks->rules.timeFrames == 60 * 60);
	CHECK(picks->rules.afterMatch == mod::MatchRules::AfterMatch::Default);

	CHECK_FALSE(LaunchPicks(MatchOf(5, 0, 1)));     // a fighter outside the roster
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 3)));     // a stage outside the list
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 99))); // a room's endless rounds
	CHECK_FALSE(LaunchPicks(MatchOf(0, 0, 0, 3, 9999))); // longer than SFV's clock holds
	CHECK(LaunchPicks(MatchOf(0, 0, 0, 7, 300)));
}

TEST_CASE("the product is SFV Ember, with names of its own")
{
	const ember::ProductIdentity& product = ember::Product();
	CHECK(std::string(product.branding.productName) == "SFV Ember");
	CHECK(std::string(product.branding.shortName) == "SFV Ember");
	CHECK(std::string(product.branding.gameName) == "Street Fighter V");
	CHECK(product.branding.accent == 0xFF8738u);
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
	CHECK(product.localeOverlays[0].tag == nullptr);
}

TEST_CASE("the pick labels are plain, from the catalog")
{
	FillTestCatalog();
	using namespace sf4e::ui;
	CHECK(StageLabel(0) == "S01");
	CHECK(StageLabel(2) == "S25");
	CHECK(StageLabel(9) == "9");
	CHECK(CostumeLabel(PickOf(0, 3)) == "Costume 3");
	CHECK(ShortCostumeLabel(PickOf(0, 3)) == "Costume 3");
	CHECK(ShortAppearanceLabel(PickOf(0, 3, 5)) == "Costume 3, color 5");
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
	CHECK(game.state.BuildId() == mod::kMatchBuild);
	CHECK(game.state.Language().empty());
	CHECK_FALSE(game.state.MainMenuInFront()); // no scene manager seen
	CHECK(game.state.ForegroundScreen().empty());
	CHECK(game.battle.Enter() == ember::game::Battle::Entry::Deferred); // no room
	CHECK_FALSE(game.battle.Live());
	CHECK_FALSE(game.battle.ResultHeld());
	CHECK(game.battle.GameResult() == sf4e::room::MatchResult::Cancel);
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

	// Plays both peers to `frames`, stepping the checks as the room client does.
	void RunTo(LoopbackPair& pair, int frames)
	{
		for (int target = pair.a.Frame(); target < frames;) {
			target = std::min(frames, target + 10);
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
	LoopbackPair pair(kBugNone, 0);
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

TEST_CASE("the check starts over when the session's confirmed frame goes back")
{
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
		exchange.Step(first.a, &send, remote);
	}
	REQUIRE(!published.empty());
	CHECK(published.front() == 60);
	published.clear();
	LoopbackPair second(kBugNone, 0);
	REQUIRE(second.RunTo(80));
	second.RunTo(100);
	exchange.Step(second.a, &send, remote);
	REQUIRE(!published.empty());
	CHECK(published.front() == 60);
}
