// SFV Ember's replays under the library's Replays screen (EMBER_LIBRARY.md
// section 26): battles saved to the replay folder are listed newest first,
// with what the screen shows of them; a file that does not decode is not
// listed; the Keep mark goes on and off; and Watch now refuses what it
// cannot play, saying so, without launching anything.

#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/ReplayFile.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvReplays.hxx"

#include "common/Localization.hxx"
#include "common/ReplayRequest.hxx"
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
	r.header.players[1] = {"player2", 1, 0, 2, "CNL", 0, 2};
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
	// Two games in place: the games' score.
	REQUIRE(newest.summary);
	CHECK(newest.summary->scored);
	CHECK(newest.summary->score[0] == 1);
	CHECK(newest.summary->score[1] == 1);
	CHECK(newest.summary->rounds == 4u);

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

	replays.StopListing();
	SetEnvironmentVariableW(L"SFVE_REPLAY_DIR", nullptr);
	fs::remove_all(folder, ignored);
}

} // namespace
