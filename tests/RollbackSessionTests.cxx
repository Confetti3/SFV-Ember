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
	// The delay GGPO took is the one the HUD shows.
	CHECK(pair.a.AppliedDelay() == 2);
	CHECK(pair.b.AppliedDelay() == 2);
}

TEST_CASE("the HUD's rollback count is the deepest replay of the last second")
{
	LoopbackPair pair(kBugNone, 0);
	CHECK(pair.a.RecentRollbackFrames() == 0u);
	REQUIRE(pair.RunTo(600));
	const auto& sa = pair.a.GetStats();
	const auto& sb = pair.b.GetStats();
	REQUIRE(sa.rollbacks + sb.rollbacks > 0);
	// The run takes well under a second's rollbacks to the end, so the last
	// ones are in the window; none is deeper than the session's deepest.
	const unsigned recent = (std::max)(pair.a.RecentRollbackFrames(), pair.b.RecentRollbackFrames());
	MESSAGE("recent " << recent << ", longest " << sa.longestRollback << " and " << sb.longestRollback);
	CHECK(recent > 0u);
	CHECK(pair.a.RecentRollbackFrames() <= (unsigned)sa.longestRollback);
	CHECK(pair.b.RecentRollbackFrames() <= (unsigned)sb.longestRollback);
	// A second later, with nothing replayed since, the window is empty.
	Sleep(1100);
	CHECK(pair.a.RecentRollbackFrames() == 0u);
	CHECK(pair.b.RecentRollbackFrames() == 0u);
	// No live link to warn of: no warning, no countdown, no stall.
	CHECK_FALSE(pair.a.ConnectionWarning());
	CHECK(pair.a.DisconnectCountdownMs() == -1);
	CHECK_FALSE(pair.a.PeerDisconnected());
}

TEST_CASE("the disconnect countdown counts down from the interruption, as SF4's does")
{
	CHECK(RollbackSession::DisconnectCountdown(false, 1000, 4250, 1500) == -1);
	CHECK(RollbackSession::DisconnectCountdown(true, 1000, 0, 1500) == -1);
	CHECK(RollbackSession::DisconnectCountdown(true, 1000, 4250, 1000) == 4250);
	CHECK(RollbackSession::DisconnectCountdown(true, 1000, 4250, 1500) == 3750);
	CHECK(RollbackSession::DisconnectCountdown(true, 1000, 4250, 5250) == 0);
	CHECK(RollbackSession::DisconnectCountdown(true, 1000, 4250, 9000) == 0);
	// GetTickCount wraps after 49.7 days; the elapsed time still counts.
	CHECK(RollbackSession::DisconnectCountdown(true, 0xFFFFFF00u, 1000, 0x100u) == 1000 - 0x200);
}

TEST_CASE("an interruption warns with a countdown, and the drop says so")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(60));
	pair.b.Close();
	bool warned = false;
	int countdown = -1;
	const ULONGLONG start = GetTickCount64();
	while (!pair.a.Failed() && GetTickCount64() - start < 5000) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
		if (pair.a.ConnectionWarning()) {
			warned = true;
			countdown = pair.a.DisconnectCountdownMs();
			CHECK(countdown >= 0);
			CHECK(countdown <= 1000); // the pair's disconnect timeout
		}
		Sleep(1);
	}
	REQUIRE(pair.a.Failed());
	CHECK(warned);
	CHECK(pair.a.PeerDisconnected());
	// The session is gone: nothing more to warn of.
	CHECK_FALSE(pair.a.ConnectionWarning());
	CHECK(pair.a.DisconnectCountdownMs() == -1);
	CHECK(pair.a.AppliedDelay() == -1);
	CHECK(pair.a.RecentRollbackFrames() == 0u);
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

TEST_CASE("a restore the game cannot complete fails the session, which says why")
{
	// SFV's flow record failing to write a restored flow back: the rollback
	// would go on from bytes without the game they describe. The session
	// fails inside GGPO's rollback and still lets GGPO count the rollback's
	// frames, which it checks by exiting the process.
	LoopbackPair pair(kBugNone, 0);
	pair.gameA.FailNextRestore();
	pair.gameB.FailNextRestore();
	CHECK_FALSE(pair.RunTo(600)); // the first rollback ends it
	REQUIRE((pair.a.Failed() || pair.b.Failed()));
	RollbackSession& failed = pair.a.Failed() ? pair.a : pair.b;
	CHECK(failed.FailureReason() == MockFighter::kStateFailure);
	CHECK_FALSE(failed.Live());
	CHECK(failed.Step(0) == RollbackSession::StepResult::Failed);
}

TEST_CASE("a state failure the game latches while it plays fails the session before another state is kept")
{
	LoopbackPair pair(kBugNone, 0);
	REQUIRE(pair.RunTo(60));
	pair.gameA.FailState();
	const int frame = pair.a.Frame();
	const ULONGLONG start = GetTickCount64();
	while (!pair.a.Failed() && GetTickCount64() - start < 5000) {
		pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
		pair.b.Step(ScriptedInput(1, pair.b.Frame(), kLoopbackSeed));
		Sleep(1);
	}
	REQUIRE(pair.a.Failed());
	CHECK(pair.a.FailureReason() == MockFighter::kStateFailure);
	CHECK(pair.a.Frame() == frame); // no save or restore went through after the failure
	CHECK_FALSE(pair.a.Live());
}

TEST_CASE("a skip read confirmed ends the ignored frames on the same frame on both peers")
{
	// Frames 100-399 read no inputs, but every 15th of them reads kSkip once
	// its inputs are confirmed (SFV's intro and its skip request,
	// mod/IntroSkip.hxx). Peer b asks from the step at its frame 130 on; with
	// an input delay of 2 the ask reaches frame 132 first, so skip frame 135
	// reads it on both peers, which read inputs again from frame 136, while
	// a never asked. Both agree with an offline run given what the rule gives.
	constexpr int kFrom = 100, kTo = 400, kEvery = 15, kAskFrom = 130, kDelay = 2, kFrames = 300;
	constexpr int kSkippedAt = 136;
	const auto local = [&](int player, int frame) -> PackedInput {
		const PackedInput ask = player == 1 && frame >= kAskFrom ? PackedInput{kSkip} : PackedInput{0};
		return ScriptedInput(player, frame, kLoopbackSeed) | ask;
	};
	LoopbackPair pair(kBugNone, kDelay);
	for (MockFighter* game : {&pair.gameA, &pair.gameB}) {
		game->IgnoreInputsBetween(kFrom, kTo);
		game->SkipEvery(kEvery);
	}
	const ULONGLONG start = GetTickCount64();
	while ((pair.a.Frame() < kFrames || pair.b.Frame() < kFrames) && GetTickCount64() - start < 30000) {
		if (pair.a.Frame() < kFrames) pair.a.Step(local(0, pair.a.Frame()));
		else pair.a.Poll();
		if (pair.b.Frame() < kFrames) pair.b.Step(local(1, pair.b.Frame()));
		else pair.b.Poll();
		REQUIRE_FALSE(pair.a.Failed());
		REQUIRE_FALSE(pair.b.Failed());
		Sleep(0);
	}
	REQUIRE(pair.a.Frame() >= kFrames);
	REQUIRE(pair.b.Frame() >= kFrames);
	CHECK(pair.gameA.SkippedAt() == kSkippedAt);
	CHECK(pair.gameB.SkippedAt() == kSkippedAt);
	// No rollback starts on an ignored frame, nor before the skip frame both
	// stepped confirmed.
	for (const MockFighter* game : {&pair.gameA, &pair.gameB}) {
		for (int target : game->RollbackTargets()) {
			INFO("rollback to frame " << target);
			CHECK((target < kFrom || target >= kSkippedAt));
		}
	}

	MockFighter reference;
	reference.IgnoreInputsBetween(kFrom, kTo);
	reference.SkipEvery(kEvery);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed >= kFrames - 20);
	std::vector<uint64_t> hashes{reference.Layout().HashLive(HashScope::Peers)};
	for (int f = 0; f < confirmed; f++) {
		// A frame's input is what each peer added kDelay steps before it.
		FrameInputs sent;
		for (int p = 0; p < kPlayers; p++) sent.p[p] = f >= kDelay ? local(p, f - kDelay) : 0;
		reference.Advance(GivenInputs(reference, sent, f), false);
		hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	}
	CHECK(reference.SkippedAt() == kSkippedAt);
	// The newest confirmed frame may not be hashed yet: compare the frames
	// since the skip that both peers hashed, as the loopback test above does.
	int compared = 0;
	for (int f = kSkippedAt; f <= confirmed; f++) {
		uint64_t ha = 0, hb = 0;
		if (!pair.a.HashAt(f, &ha) || !pair.b.HashAt(f, &hb)) continue;
		INFO("frame " << f);
		CHECK(ha == hb);
		CHECK(ha == hashes[f]);
		compared++;
	}
	CHECK(compared > 100);
}
