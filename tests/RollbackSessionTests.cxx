#include <doctest/doctest.h>

#include "LoopbackPair.hxx"

#include <windows.h>

#include <algorithm>

using namespace sfve;
using namespace sfve::testbed;

namespace {

// Peers' hash of an offline reference run at each frame (another game,
// with its own clock: only the peers' hash can match it).
std::vector<uint64_t> ReferenceHashes(int frames)
{
	MockFighter reference;
	std::vector<uint64_t> hashes;
	hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	for (int f = 0; f < frames; f++) {
		reference.Advance(ScriptedFrame(f, kLoopbackSeed), false);
		hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	}
	return hashes;
}

} // namespace

TEST_CASE("two peers over loopback UDP roll back and agree with an offline run")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(600));
	// Let confirmation catch up with the last frames.
	pair.RunTo(620);

	const auto& sa = pair.a.GetStats();
	const auto& sb = pair.b.GetStats();
	MESSAGE("A rollbacks " << sa.rollbacks << " (longest " << sa.longestRollback << "), B rollbacks " << sb.rollbacks);
	CHECK(sa.rollbacks + sb.rollbacks > 0);
	CHECK(sa.loadMismatches == 0);
	CHECK(sb.loadMismatches == 0);

	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed >= 400);
	const std::vector<uint64_t> reference = ReferenceHashes(confirmed);
	int compared = 0;
	for (int f = confirmed - 200; f <= confirmed; f++) {
		uint64_t ha = 0, hb = 0;
		if (!pair.a.HashAt(f, &ha) || !pair.b.HashAt(f, &hb)) continue;
		INFO("frame " << f);
		CHECK(ha == hb);
		CHECK(ha == reference[f]);
		compared++;
	}
	CHECK(compared > 100);
}

TEST_CASE("kept hashes reach back to the session's first frames")
{
	for (const bool keep : {false, true}) {
		LoopbackPair pair(kBugNone, 0, keep);
		REQUIRE(pair.RunTo(600));
		const std::vector<uint64_t> reference = ReferenceHashes(20);
		uint64_t ha = 0, hb = 0;
		const bool kept = pair.a.HashAt(10, &ha) && pair.b.HashAt(10, &hb);
		INFO("keepAllHashes " << keep);
		CHECK(kept == keep); // otherwise only the latest 256 frames
		if (kept) {
			CHECK(ha == hb);
			CHECK(ha == reference[10]);
		}
	}
}

TEST_CASE("frames whose inputs the game ignores are never a rollback's start")
{
	// Frames 100-399 read no inputs (SFV outside the fight), while both peers
	// keep sending changing ones: no rollback may start there, and the
	// peers still agree with an offline run that plays neutral inputs there.
	constexpr int kFrom = 100, kTo = 400;
	LoopbackPair pair(kBugNone, 0);
	pair.gameA.IgnoreInputsBetween(kFrom, kTo);
	pair.gameB.IgnoreInputsBetween(kFrom, kTo);
	REQUIRE(pair.RunTo(600));
	pair.RunTo(620);
	CHECK(pair.gameA.RollbackTargets().size() + pair.gameB.RollbackTargets().size() > 0);
	for (const MockFighter* game : {&pair.gameA, &pair.gameB}) {
		for (int target : game->RollbackTargets()) {
			INFO("rollback to frame " << target);
			CHECK((target < kFrom || target >= kTo));
		}
	}
	MockFighter reference;
	reference.IgnoreInputsBetween(kFrom, kTo);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed >= 500);
	std::vector<uint64_t> hashes{reference.Layout().HashLive(HashScope::Peers)};
	for (int f = 0; f < confirmed; f++) {
		reference.Advance(reference.InputsMatter() ? ScriptedFrame(f, kLoopbackSeed) : FrameInputs{}, false);
		hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	}
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha == hb);
	CHECK(ha == hashes[confirmed]);
}

TEST_CASE("input delay keeps the peers in step")
{
	LoopbackPair pair(kBugNone, 2);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed > 300);
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha == hb);
}

TEST_CASE("an unmasked heap pointer makes identical play look like a desync")
{
	LoopbackPair pair(kBugUnmaskedPointer, 0);
	REQUIRE(pair.RunTo(120));
	pair.RunTo(140);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha != hb);
	CHECK(pair.gameA.Health(0) == pair.gameB.Health(0));
}

TEST_CASE("a per-process value not masked for peers makes identical play look like a desync")
{
	LoopbackPair pair(kBugUnmaskedClock, 0);
	REQUIRE(pair.RunTo(120));
	pair.RunTo(140);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	uint64_t ha = 0, hb = 0;
	REQUIRE(pair.a.HashAt(confirmed, &ha));
	REQUIRE(pair.b.HashAt(confirmed, &hb));
	CHECK(ha != hb);
	CHECK(pair.gameA.Health(0) == pair.gameB.Health(0));
	CHECK(pair.a.GetStats().loadMismatches == 0); // one process still replays it exactly
}

TEST_CASE("a real desync on one peer shows in the first checkpoint after it, not before")
{
	constexpr int kDesyncFrom = 250;
	LoopbackPair pair(kBugNone, 0);
	pair.gameB.DesyncFrom(kDesyncFrom);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	const int last = pair.a.LatestCheckpoint(confirmed);
	REQUIRE(last >= 300);
	std::vector<RegionHash> ha, hb;
	for (int checkpoint = last; pair.a.RegionHashesAt(checkpoint, &ha); checkpoint -= RollbackSession::kCheckpointInterval) {
		REQUIRE(pair.b.RegionHashesAt(checkpoint, &hb));
		INFO("checkpoint " << checkpoint);
		CHECK((ha == hb) == (checkpoint <= kDesyncFrom));
	}
	CHECK(pair.gameA.Health(0) != pair.gameB.Health(0));
}

TEST_CASE("a peer that goes away ends the other's session cleanly")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(60));
	pair.b.Close();
	const ULONGLONG start = GetTickCount64();
	while (!pair.a.Failed() && GetTickCount64() - start < 5000) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
		Sleep(1);
	}
	CHECK(pair.a.Failed());
	CHECK_FALSE(pair.a.Live());
	CHECK(pair.a.Step(0) == RollbackSession::StepResult::Failed);
}

TEST_CASE("peers keep matching per-region checkpoints for confirmed frames")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	const int checkpoint = pair.a.LatestCheckpoint(confirmed);
	REQUIRE(checkpoint >= 300);
	CHECK(checkpoint % RollbackSession::kCheckpointInterval == 0);
	std::vector<RegionHash> ha, hb;
	REQUIRE(pair.a.RegionHashesAt(checkpoint, &ha));
	REQUIRE(pair.b.RegionHashesAt(checkpoint, &hb));
	const auto& regions = pair.gameA.Layout().Regions();
	CHECK(ha.size() == (size_t)std::count_if(regions.begin(), regions.end(), [](const StateRegion& r) { return r.checked; }));
	CHECK(ha == hb);
}

TEST_CASE("a peer that only polls while it holds frames keeps receiving inputs")
{
	LoopbackPair pair(kBugNone, 1);
	REQUIRE(pair.RunTo(60));
	// b holds for a while, polling only; a cannot get far ahead but b learns
	// a's inputs, so b's confirmed frame catches up with its own frame.
	for (int i = 0; i < 200; i++) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
		pair.b.Poll();
		Sleep(1);
	}
	CHECK(pair.b.LastConfirmedFrame() >= pair.b.Frame() - 1);
	CHECK(pair.b.Live());
}

TEST_CASE("a round flow that is part of the rollback state latches the KO where an offline run does")
{
	MockFighter reference;
	for (int f = 0; f < 1500 && reference.FlowKoFrame() < 0; f++) reference.Advance(ScriptedFrame(f, kLoopbackSeed), false);
	REQUIRE(reference.FlowKoFrame() > 0);

	LoopbackPair pair(kBugNone, 0);
	const ULONGLONG start = GetTickCount64();
	while ((pair.gameA.FlowKoFrame() < 0 || pair.gameB.FlowKoFrame() < 0) && GetTickCount64() - start < 30000) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
		pair.b.Step(ScriptedInput(1, pair.b.Frame(), kLoopbackSeed));
		Sleep(0);
	}
	MESSAGE("KO at " << reference.FlowKoFrame() << "; rollbacks " << pair.a.GetStats().rollbacks << " / "
					 << pair.b.GetStats().rollbacks);
	REQUIRE(pair.a.GetStats().rollbacks + pair.b.GetStats().rollbacks > 0); // the guesses were corrected
	CHECK(pair.gameA.FlowKoFrame() == reference.FlowKoFrame());
	CHECK(pair.gameB.FlowKoFrame() == reference.FlowKoFrame());
}
