// SFV's desync check (SfvVerification's CheckpointExchange) over two real
// rollback sessions on loopback: what each player publishes, when the two
// agree, and how a real desync, a watcher's hash, a late start and a new
// battle are taken.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "ember/SfvVerification.hxx"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::testbed;
using sf4e::SessionProtocol::BattleHashV2;

namespace {

// The session's checkpoint spacing: these tests hold for any of its values (60 in beta.8, 20 from beta.9).
constexpr int kInterval = RollbackSession::kCheckpointInterval;

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

BattleHashV2 PlayerHash(int frame, std::uint64_t overall)
{
	BattleHashV2 hash;
	hash.frameIdx = frame;
	hash.overall = overall;
	hash.fromPlayer = true;
	return hash;
}

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
	CHECK(exchanges.mismatchA->frame == (kDesyncFrom / kInterval + 1) * kInterval);
	CHECK(exchanges.mismatchB->frame == (kDesyncFrom / kInterval + 1) * kInterval);
	// The checkpoint before it, at which the players still agreed, is what a report names as the last good one.
	CHECK(exchanges.mismatchA->previousMatch == kDesyncFrom / kInterval * kInterval);
	CHECK(exchanges.mismatchB->previousMatch == kDesyncFrom / kInterval * kInterval);
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
	watched.frameIdx = kInterval;
	watched.overall = 0x1234;
	watched.fromPlayer = false;
	remote[kInterval] = watched;
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
	// last 256 of them, 165 to 420 here: the checkpoints before 165 are gone.
	REQUIRE(published.size() >= 4);
	const int firstKept = (165 + kInterval - 1) / kInterval * kInterval;
	for (size_t i = 0; i < published.size(); i++) CHECK(published[i] == firstKept + (int)i * kInterval);
}

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
		remote[kInterval] = PlayerHash(kInterval, kWrong);
		CHECK(exchange.Step(first.a, &send, remote));
		// One mismatch a battle: the next is not reported again.
		remote[2 * kInterval] = PlayerHash(2 * kInterval, kWrong);
		CHECK_FALSE(exchange.Step(first.a, &send, remote));
	}
	REQUIRE(!published.empty());
	CHECK(published.front() == kInterval);
	published.clear();
	LoopbackPair second(kBugNone, 0);
	REQUIRE(second.RunTo(80));
	second.RunTo(100);
	// A hash of the battle before, still waiting, is dropped, not compared
	// with this battle's checkpoint of the same frame.
	remote[kInterval] = PlayerHash(kInterval, kWrong);
	CHECK_FALSE(exchange.Step(second.a, &send, remote));
	CHECK(remote.empty());
	REQUIRE(!published.empty());
	CHECK(published.front() == kInterval);
	// The new battle's own mismatch is reported.
	remote[kInterval] = PlayerHash(kInterval, kWrong);
	const auto mismatch = exchange.Step(second.a, &send, remote);
	REQUIRE(mismatch);
	CHECK(mismatch->frame == kInterval);
	CHECK(mismatch->remote == kWrong);
}
