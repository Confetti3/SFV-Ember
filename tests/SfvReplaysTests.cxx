// SFV Ember's replays under the library's Replays screen (EMBER_LIBRARY.md
// section 26): battles saved to the replay folder are listed newest first,
// with what the screen shows of them; a file that does not decode is not
// listed; the Keep mark goes on and off; and Watch now refuses what it
// cannot play, saying so, without launching anything.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"
#include "TestInputs.hxx"
#include "core/ReplayFile.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvReplays.hxx"

#include "common/Localization.hxx"
#include "common/ReplayRequest.hxx"
#include "platform/ReplaySets.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sfve;
using ArchivedReplay = sf4e::platform::replays::ArchivedReplay;

replay::Replay Battle(uint64_t startedAt, int slot, std::vector<replay::Game> games)
{
	replay::Replay r;
	r.header.product = "SFV Ember";
	r.header.build = "sfv-ember/another-build";
	r.header.startedAt = startedAt;
	r.header.slot = slot;
	r.header.players[0] = {"player1", 0, 0, 1, "RYU", 0, 1};
	r.header.players[1] = {"player2", 1, 0, 2, "CNL", 0, 2, 1, 0}; // V-Skill II, V-Trigger I
	r.header.stage = 0;
	r.header.stageCode = "S25";
	r.header.rounds = 3;
	r.header.roundSeconds = 99;
	r.header.timeFrames = 5940;
	r.header.seed = 42;
	r.header.inputDelay = 2;
	r.header.games = std::move(games);
	r.header.complete = true;
	for (int f = 0; f < 400; f++) {
		RecordedFrame frame;
		frame.flags = f < 100 || (f >= 200 && f < 250) ? 0 : RecordedFrame::kFight;
		if (frame.Fight()) frame.inputs = testbed::ScriptedFrame(f, 9);
		r.frames.push_back(frame);
	}
	for (int f = 0; f <= 360; f += 60) r.checkpoints.push_back({f, 0xABC0u + (uint64_t)f});
	return r;
}

// The lister's next listing that `done` accepts, within a few seconds.
template <class Done>
std::shared_ptr<const std::vector<ArchivedReplay>> ListingWhen(Done done)
{
	ember::game::Replays& replays = adapter::SfvReplays();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline) {
		replays.WantListing();
		const auto listing = replays.LatestListing();
		if (listing && done(*listing)) return listing;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return nullptr;
}

TEST_CASE("the Replays screen lists the saved battles, keeps the player's mark and refuses what cannot play")
{
	wchar_t temp[MAX_PATH];
	REQUIRE(GetTempPathW(MAX_PATH, temp) > 0);
	const fs::path folder = fs::path(temp) / (L"sfve-replays-test-" + std::to_wstring(GetCurrentProcessId()));
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	REQUIRE(SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", folder.c_str()));
	CHECK(adapter::ReplayFolder() == folder);

	// One game, P2 2-1; and, later, a watched battle of two games in place.
	const replay::Replay one = Battle(1'762'500'000, 0, {{true, 1, {1, 2}, 3, 3, 380}});
	const replay::Replay two = Battle(1'762'500'600, 2, {{true, 0, {2, 0}, 2, 3, 190}, {true, 1, {0, 2}, 2, 3, 390}});
	const fs::path fileOne = adapter::SaveReplay(one);
	const fs::path fileTwo = adapter::SaveReplay(two);
	REQUIRE(fs::exists(fileOne));
	REQUIRE(fs::exists(fileTwo));
	CHECK(fileOne.parent_path() == folder);
	CHECK(fileOne.extension() == replay::kExtension);
	CHECK(fileOne.filename().wstring().rfind(L"20251107-", 0) == 0); // its start, UTC
	{
		std::ofstream damaged(folder / L"damaged.sfvreplay", std::ios::binary);
		damaged << "SFVEREPL not a replay";
	}

	const auto listing = ListingWhen([](const std::vector<ArchivedReplay>& l) { return l.size() >= 2; });
	REQUIRE(listing);
	REQUIRE(listing->size() == 2u); // the damaged file is not listed
	const ArchivedReplay& newest = (*listing)[0];
	const ArchivedReplay& older = (*listing)[1];
	CHECK(newest.path == fileTwo);
	CHECK(older.path == fileOne);
	CHECK(older.names[0] == "player1");
	CHECK(older.names[1] == "player2");
	CHECK(older.fighters[0] == 0);
	CHECK(older.fighters[1] == 1);
	CHECK(older.time == 1'762'500'000u);
	CHECK_FALSE(older.label.empty());
	CHECK(older.provenance == sf4e::replayfiles::Provenance::Played);
	CHECK(newest.provenance == sf4e::replayfiles::Provenance::Spectated);
	CHECK_FALSE(older.kept);
	CHECK_FALSE(older.watched);
	REQUIRE(older.summary);
	// One game: its rounds' score, as SF4's replay of a game shows it.
	CHECK(older.summary->scored);
	CHECK(older.summary->score[0] == 1);
	CHECK(older.summary->score[1] == 2);
	CHECK(older.summary->rounds == 3u);
	CHECK(older.summary->frames == 250u); // the frames that read inputs
	CHECK(older.summary->players[1].color == 2);
	// Each side's Ultra is its loadout's number (section 40).
	CHECK(older.summary->players[0].ultra == picks::LoadoutIndex(*picks::LoadoutOfNumbers(0, 0)));
	CHECK(older.summary->players[1].ultra == picks::LoadoutIndex(*picks::LoadoutOfNumbers(1, 0)));
	// Two games in place: the games' score.
	REQUIRE(newest.summary);
	CHECK(newest.summary->scored);
	CHECK(newest.summary->score[0] == 1);
	CHECK(newest.summary->score[1] == 1);
	CHECK(newest.summary->rounds == 4u);
	// Replays › Sets (EMBER_LIBRARY.md section 28): two battles of the same
	// two players within half an hour are one room set, oldest first, as SF4's
	// games of a room set are (the library's ReplaySets.hxx); no bridge holds
	// it, so it has no pack.
	const auto sets = sf4e::platform::replays::Sets(*listing);
	REQUIRE(sets.size() == 1u);
	CHECK_FALSE(sets[0].Tagged());
	CHECK(sets[0].bridgeId.empty());
	REQUIRE(sets[0].games.size() == 2u);
	CHECK(sets[0].games[0]->path == fileOne);
	CHECK(sets[0].games[1]->path == fileTwo);

	ember::game::Replays& replays = adapter::SfvReplays();
	replays.MarkKept(fileOne, true);
	CHECK(ListingWhen([&](const std::vector<ArchivedReplay>& l) { return l.size() == 2 && l[1].kept; }));
	replays.MarkKept(fileOne, false);
	CHECK(ListingWhen([&](const std::vector<ArchivedReplay>& l) { return l.size() == 2 && !l[1].kept; }));
	// A file outside the replay folder is never marked.
	replays.MarkKept(fs::path(temp) / L"elsewhere.sfvreplay", true);
	CHECK_FALSE(fs::exists(fs::path(temp) / L"kept.txt"));

	// Watch now: only at the main menu with no room, and only a replay this
	// build can play; otherwise it says so and launches nothing.
	sf4e::replay::Request watch;
	watch.mode = sf4e::replay::Mode::Watch;
	watch.path = sf4e::platform::WideToUtf8(fileOne.wstring());
	replays.Start(watch, false, true);
	CHECK(replays.Status().notice == sf4e::loc::T("replays.not_ready"));
	CHECK(replays.Status().noticeError);
	CHECK(replays.Status().step == sf4e::replaystore::Step::Idle);
	replays.Start(watch, true, false);
	CHECK(replays.Status().notice == sf4e::loc::T("replays.not_ready"));
	replays.Start(watch, true, true); // another build made it
	CHECK(replays.Status().notice == sf4e::loc::T("inputs.unreadable_detail"));
	CHECK(replays.Status().step == sf4e::replaystore::Step::Idle);
	watch.path = sf4e::platform::WideToUtf8((folder / L"damaged.sfvreplay").wstring());
	replays.Start(watch, true, true);
	CHECK(replays.Status().notice == sf4e::loc::T("inputs.unreadable_detail"));
	CHECK(replays.Status().logOpens == 0u); // Ember's menu never stepped aside
	CHECK_FALSE(replays.Playback().playback);
	// What SFV does not offer is refused, never done.
	sf4e::replay::Request add;
	add.mode = sf4e::replay::Mode::Add;
	add.path = sf4e::platform::WideToUtf8(fileOne.wstring());
	replays.Start(add, true, true);
	CHECK(replays.Status().noticeError);

	// A file in a folder that is not there is refused as one that cannot be
	// read, never thrown out of the request: a parent that was never made...
	const fs::path nowhere = folder / L"gone" / L"gone.sfvreplay";
	watch.path = sf4e::platform::WideToUtf8(nowhere.wstring());
	CHECK_NOTHROW(replays.Start(watch, true, true));
	CHECK(replays.Status().notice == sf4e::loc::T("inputs.unreadable_detail"));
	CHECK_NOTHROW(replays.MarkKept(nowhere, true));
	CHECK_FALSE(fs::exists(folder / L"gone"));

	// ...or the replay folder itself, taken away after it was listed.
	replays.StopListing();
	fs::remove_all(folder, ignored);
	REQUIRE_FALSE(fs::exists(folder));
	CHECK_NOTHROW(replays.MarkKept(fileOne, true));
	watch.path = sf4e::platform::WideToUtf8(fileOne.wstring());
	CHECK_NOTHROW(replays.Start(watch, true, true));
	CHECK(replays.Status().step == sf4e::replaystore::Step::Idle);
	CHECK_FALSE(fs::exists(folder));

	SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", nullptr);
}

TEST_CASE("Watch now refuses a replay of this build whose stage this copy has not installed")
{
	// A room's battle records the room's stage, which this copy may lack
	// (EMBER_LIBRARY.md section 38). Everything else about this replay is one
	// this copy plays: its build, fighters, rounds and frames.
	catalog_fixture::FillTestCatalog();
	wchar_t temp[MAX_PATH];
	REQUIRE(GetTempPathW(MAX_PATH, temp) > 0);
	const fs::path folder = fs::path(temp) / (L"sfve-replays-stage-test-" + std::to_wstring(GetCurrentProcessId()));
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	REQUIRE(SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", folder.c_str()));
	replay::Replay battle = Battle(1'762'501'200, 0, {{true, 1, {1, 2}, 3, 3, 380}});
	battle.header.build = adapter::MatchBuildId();
	battle.header.stage = catalog_fixture::kUninstalledStage;
	battle.header.stageCode = "S26";
	const fs::path file = adapter::SaveReplay(battle);
	REQUIRE(fs::exists(file));
	ember::game::Replays& replays = adapter::SfvReplays();
	sf4e::replay::Request watch;
	watch.mode = sf4e::replay::Mode::Watch;
	watch.path = sf4e::platform::WideToUtf8(file.wstring());
	replays.Start(watch, true, true);
	CHECK(replays.Status().notice == sf4e::loc::T("inputs.unreadable_detail"));
	CHECK(replays.Status().noticeError);
	CHECK(replays.Status().step == sf4e::replaystore::Step::Idle);
	CHECK_FALSE(replays.Playback().playback);
	replays.StopListing();
	fs::remove_all(folder, ignored);
	SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", nullptr);
}

TEST_CASE("Watch now refuses a replay of this build whose V-Skill or V-Trigger is no choice")
{
	// A file this build wrote always holds 0 or 1 (section 40); anything else
	// is not a battle this copy can launch.
	catalog_fixture::FillTestCatalog();
	wchar_t temp[MAX_PATH];
	REQUIRE(GetTempPathW(MAX_PATH, temp) > 0);
	const fs::path folder = fs::path(temp) / (L"sfve-replays-loadout-test-" + std::to_wstring(GetCurrentProcessId()));
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	REQUIRE(SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", folder.c_str()));
	replay::Replay battle = Battle(1'762'501'800, 0, {{true, 1, {1, 2}, 3, 3, 380}});
	battle.header.build = adapter::MatchBuildId();
	battle.header.players[0].vTrigger = 2;
	const fs::path file = adapter::SaveReplay(battle);
	REQUIRE(fs::exists(file));
	ember::game::Replays& replays = adapter::SfvReplays();
	sf4e::replay::Request watch;
	watch.mode = sf4e::replay::Mode::Watch;
	watch.path = sf4e::platform::WideToUtf8(file.wstring());
	replays.Start(watch, true, true);
	CHECK(replays.Status().notice == sf4e::loc::T("inputs.unreadable_detail"));
	CHECK(replays.Status().step == sf4e::replaystore::Step::Idle);
	CHECK_FALSE(replays.Playback().playback);
	replays.StopListing();
	fs::remove_all(folder, ignored);
	SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", nullptr);
}

} // namespace
