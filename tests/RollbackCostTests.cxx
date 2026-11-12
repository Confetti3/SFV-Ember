// Cheaper rollbacks (docs/design/beta8/perf.md): a save no load can reach
// keeps no copy, a frame is hashed once its state is final, and a sample of
// the restores is re-hashed. Two peers over a lossy, laggy loopback link
// (LossyLink) play as a game does, a frame every few milliseconds, so some
// inputs come in time and some do not: no load may ever reach a save that
// kept no copy, and every hash and checkpoint must be what the session made
// without the savings, and what an offline run makes.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "LossyLink.hxx"
#include "TestInputs.hxx"
#include "core/RollbackSession.hxx"
#include "core/SyncTest.hxx"

#include <windows.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::testbed;

namespace {

// A save of frame F (made at the end of step F - 1) keeps no copy once the
// other side's input for frame F + 1 is here, which that side sent in its
// step F + 1 - kDelay: with a delay of 3, an input under a frame on the way
// skips a save, and one more than 3 frames on the way was guessed and is
// rolled back.
constexpr int kDelay = 3;
constexpr int kFrameMs = 4;

// The link most of these play over: from under a frame to 4 frames on the
// way, in order, and some lost.
LossyLink::Shape Lossy(uint32_t seed)
{
	LossyLink::Shape shape;
	shape.lagMs = 2;
	shape.jitterMs = 14;
	shape.lossPercent = 5;
	shape.seed = seed;
	return shape;
}

// A loopback pair over a LossyLink, each peer's skipped saves noted.
struct LossyPair {
	std::unique_ptr<LossyLink> link;
	std::set<int> skippedA, skippedB;
	// Each checkpoint's reports (onCheckpoint), in order, per peer.
	std::map<int, std::vector<uint64_t>> reportsA, reportsB;
	std::unique_ptr<LoopbackPair> pair;

	LossyPair(uint32_t seed, const LoopbackPair::Configure& configure)
	{
		pair = std::make_unique<LoopbackPair>(kBugNone, kDelay, false, LoopbackPair::BeforeStart{},
			[&](RollbackSession::Config& a, RollbackSession::Config& b) {
				link = std::make_unique<LossyLink>(a.localPort, b.localPort, Lossy(seed));
				REQUIRE(link->Ready());
				a.remotePort = link->PortForA();
				b.remotePort = link->PortForB();
				a.onSaveSkipped = [this](int frame) { skippedA.insert(frame); };
				b.onSaveSkipped = [this](int frame) { skippedB.insert(frame); };
				a.onCheckpoint = [this](int frame, uint64_t hash) { reportsA[frame].push_back(hash); };
				b.onCheckpoint = [this](int frame, uint64_t hash) { reportsB[frame].push_back(hash); };
				if (configure) configure(a, b);
			});
		// Losses once both sessions run (LossyLink::Lose: GGPO's handshake
		// retries a lost datagram only after 0.5 to 2 s, past the pair's
		// one-second disconnect timeout).
		pair->pump = [this] {
			link->Lose(pair->a.Running() && pair->b.Running());
			link->Pump();
		};
	}
};

// Steps both peers as a game does, one attempt every kFrameMs each, until
// both have run `frames` frames, then keeps polling for `settleMs` so the
// last frames are confirmed. False at a failure or the deadline.
bool RunPaced(LoopbackPair& pair, int frames, double settleMs = 300, double deadlineMs = 30000)
{
	const double start = LossyLink::NowMs();
	double due[2] = {start, start};
	RollbackSession* sessions[2] = {&pair.a, &pair.b};
	double settledFrom = -1;
	for (;;) {
		const double now = LossyLink::NowMs();
		if (now - start > deadlineMs) {
			MESSAGE("deadline: A at frame " << pair.a.Frame() << ", B at frame " << pair.b.Frame());
			return false;
		}
		if (pair.pump) pair.pump();
		bool done = true;
		for (int p = 0; p < 2; p++) {
			RollbackSession& s = *sessions[p];
			if (s.Frame() < frames) {
				done = false;
				if (now >= due[p]) {
					s.Step(ScriptedInput(p, s.Frame(), kLoopbackSeed));
					// A peer that fell far behind catches up a frame per turn,
					// not in one burst.
					due[p] = std::max(due[p] + kFrameMs, now - 4.0 * kFrameMs);
				}
			} else {
				s.Poll();
			}
			if (s.Failed()) {
				MESSAGE((p == 0 ? "A" : "B") << " failed at frame " << s.Frame() << ": " << s.FailureReason()
											 << " (A at frame " << pair.a.Frame() << ", B at frame " << pair.b.Frame()
											 << ")");
				return false;
			}
		}
		if (done) {
			if (settledFrom < 0) settledFrom = now;
			if (now - settledFrom >= settleMs) return true;
		}
		Sleep(0);
	}
}

// The peers' hash of an offline run at each frame, with both players' inputs
// kDelay frames late as the sessions play them (none before frame kDelay).
std::vector<uint64_t> DelayedReference(int frames)
{
	MockFighter reference;
	std::vector<uint64_t> hashes{reference.Layout().HashLive(HashScope::Peers)};
	for (int f = 0; f < frames; f++) {
		FrameInputs sent;
		for (int p = 0; p < kPlayers; p++) sent.p[p] = f >= kDelay ? ScriptedInput(p, f - kDelay, kLoopbackSeed) : 0;
		reference.Advance(GivenInputs(reference, sent, f), false);
		hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	}
	return hashes;
}

int Confirmed(const LoopbackPair& pair)
{
	return std::min({pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame(), pair.a.Frame(), pair.b.Frame()});
}

} // namespace

TEST_CASE("a save no load can reach keeps no copy, and no load ever reaches one")
{
	LossyPair lossy(11, {});
	LoopbackPair& pair = *lossy.pair;
	REQUIRE(RunPaced(pair, 900));
	const RollbackSession::Stats& sa = pair.a.GetStats();
	const RollbackSession::Stats& sb = pair.b.GetStats();
	MESSAGE("link: " << lossy.link->Passed() << " datagrams passed, " << lossy.link->Lost() << " lost");
	MESSAGE("A: " << sa.rollbacks << " rollbacks, " << sa.savesSkipped << " saves kept no copy, " << sa.hashesOnConfirm
				  << " hashed once final, " << sa.hashesDropped << " replaced before final");
	MESSAGE("B: " << sb.rollbacks << " rollbacks, " << sb.savesSkipped << " saves kept no copy, " << sb.hashesOnConfirm
				  << " hashed once final, " << sb.hashesDropped << " replaced before final");
	// Both kinds of frame came: inputs in time (saves skipped) and late
	// (rollbacks).
	CHECK(sa.savesSkipped > 0);
	CHECK(sb.savesSkipped > 0);
	CHECK(sa.rollbacks + sb.rollbacks > 0);
	CHECK(sa.savesSkipped == lossy.skippedA.size());
	CHECK(sb.savesSkipped == lossy.skippedB.size());
	// No load reached a save that kept no copy: the session would have failed
	// (Load), and no rollback started on one.
	CHECK(sa.skippedLoads == 0);
	CHECK(sb.skippedLoads == 0);
	for (int target : pair.gameA.RollbackTargets()) {
		INFO("A rolled back to frame " << target);
		CHECK(lossy.skippedA.count(target) == 0);
	}
	for (int target : pair.gameB.RollbackTargets()) {
		INFO("B rolled back to frame " << target);
		CHECK(lossy.skippedB.count(target) == 0);
	}
	CHECK(sa.loadMismatches == 0);
	CHECK(sb.loadMismatches == 0);
}

TEST_CASE("hashes and checkpoints are the same with and without the cheaper rollbacks")
{
	// The same scripted match twice over the same lossy link: once with the
	// savings (the default) and once without. Every frame is hashed
	// (LoopbackPair's verifyLoads), so every frame both sessions confirmed
	// compares, with each other and with an offline run.
	constexpr int kFrames = 900;
	LossyPair on(21, {});
	REQUIRE(RunPaced(*on.pair, kFrames));
	LossyPair off(21, [](RollbackSession::Config& a, RollbackSession::Config& b) {
		a.skipUnloadableSaves = b.skipUnloadableSaves = false;
		a.hashOnConfirm = b.hashOnConfirm = false;
	});
	REQUIRE(RunPaced(*off.pair, kFrames));
	CHECK(off.pair->a.GetStats().savesSkipped + off.pair->b.GetStats().savesSkipped == 0);
	CHECK(off.pair->a.GetStats().hashesOnConfirm + off.pair->b.GetStats().hashesOnConfirm == 0);
	// The savings were made: frames hashed once final, and saves a rollback
	// replaced before they were final, whose hash was never made.
	const RollbackSession::Stats& sa = on.pair->a.GetStats();
	const RollbackSession::Stats& sb = on.pair->b.GetStats();
	CHECK(sa.savesSkipped + sb.savesSkipped > 0);
	CHECK(sa.hashesOnConfirm + sb.hashesOnConfirm > 0);
	CHECK(sa.rollbacks + sb.rollbacks > 0);

	const int confirmed = std::min(Confirmed(*on.pair), Confirmed(*off.pair));
	REQUIRE(confirmed >= kFrames - 60);
	const std::vector<uint64_t> reference = DelayedReference(confirmed);
	int compared = 0;
	for (int f = 0; f <= confirmed; f++) {
		uint64_t h[4] = {};
		const bool all = on.pair->a.HashAt(f, &h[0]) && on.pair->b.HashAt(f, &h[1]) && off.pair->a.HashAt(f, &h[2]) &&
			off.pair->b.HashAt(f, &h[3]);
		if (!all) continue; // fell out of a session's last 256
		INFO("frame " << f);
		CHECK(h[0] == reference[f]);
		CHECK(h[1] == reference[f]);
		CHECK(h[2] == reference[f]);
		CHECK(h[3] == reference[f]);
		compared++;
	}
	CHECK(compared >= 200);

	// Every checkpoint: the same regions on all four sessions, and with the
	// savings each reported once, final, with the value the session without
	// them reported last.
	int checkpoints = 0;
	for (int f = RollbackSession::kCheckpointInterval; f <= confirmed; f += RollbackSession::kCheckpointInterval) {
		INFO("checkpoint " << f);
		std::vector<RegionHash> r[4];
		REQUIRE(on.pair->a.RegionHashesAt(f, &r[0]));
		REQUIRE(on.pair->b.RegionHashesAt(f, &r[1]));
		REQUIRE(off.pair->a.RegionHashesAt(f, &r[2]));
		REQUIRE(off.pair->b.RegionHashesAt(f, &r[3]));
		CHECK(r[0] == r[2]);
		CHECK(r[1] == r[3]);
		CHECK(r[0] == r[1]);
		REQUIRE(on.reportsA.count(f));
		REQUIRE(on.reportsB.count(f));
		REQUIRE(off.reportsA.count(f));
		CHECK(on.reportsA[f].size() == 1);
		CHECK(on.reportsB[f].size() == 1);
		CHECK(on.reportsA[f].back() == off.reportsA[f].back());
		CHECK(on.reportsA[f].back() == reference[f]);
		checkpoints++;
	}
	CHECK(checkpoints >= 10);
}

TEST_CASE("with a match's settings a sample of the restores is re-hashed, and checkpoints agree")
{
	// As a match plays: only checkpoints hashed, one restore in four
	// re-hashed against its save.
	LossyPair lossy(31, [](RollbackSession::Config& a, RollbackSession::Config& b) {
		a.verifyLoads = b.verifyLoads = false;
		a.verifyLoadsEvery = b.verifyLoadsEvery = 4;
	});
	LoopbackPair& pair = *lossy.pair;
	REQUIRE(RunPaced(pair, 900));
	const RollbackSession::Stats& sa = pair.a.GetStats();
	const RollbackSession::Stats& sb = pair.b.GetStats();
	REQUIRE(sa.rollbacks + sb.rollbacks >= 4);
	CHECK(sa.loadsVerified == sa.rollbacks / 4);
	CHECK(sb.loadsVerified == sb.rollbacks / 4);
	CHECK(sa.loadMismatches == 0);
	CHECK(sb.loadMismatches == 0);
	// Most saves keep no copy now: only checkpoints are hashed.
	CHECK(sa.savesSkipped > 0);
	CHECK(sb.savesSkipped > 0);
	const int confirmed = Confirmed(pair);
	const std::vector<uint64_t> reference = DelayedReference(confirmed);
	int checkpoints = 0;
	for (int f = RollbackSession::kCheckpointInterval; f <= confirmed; f += RollbackSession::kCheckpointInterval) {
		INFO("checkpoint " << f);
		// Each reported once, final, as the offline run has it.
		REQUIRE(lossy.reportsA.count(f));
		REQUIRE(lossy.reportsB.count(f));
		CHECK(lossy.reportsA[f].size() == 1);
		CHECK(lossy.reportsB[f].size() == 1);
		CHECK(lossy.reportsA[f].back() == reference[f]);
		CHECK(lossy.reportsB[f].back() == reference[f]);
		std::vector<RegionHash> ra, rb;
		REQUIRE(pair.a.RegionHashesAt(f, &ra));
		REQUIRE(pair.b.RegionHashesAt(f, &rb));
		CHECK(ra == rb);
		// HashAt keeps the latest 256 frames.
		uint64_t ha = 0;
		if (f > confirmed - 200) {
			REQUIRE(pair.a.HashAt(f, &ha));
			CHECK(ha == reference[f]);
		}
		checkpoints++;
	}
	CHECK(checkpoints >= 10);
	// The latest checkpoint's bytes are kept, whether or not its save kept a
	// copy, and they are the state that hashes as the checkpoint.
	const int latest = pair.a.LatestCheckpoint(confirmed);
	const std::vector<uint8_t>* state = pair.a.CheckpointState(latest);
	REQUIRE(state != nullptr);
	CHECK(pair.gameA.Layout().HashSaved(state->data(), HashScope::Peers) == reference[latest]);
}

TEST_CASE("a fighter who leaves mid-match: the reload GGPO makes reaches a save that kept its copy")
{
	// When a fighter is dropped, GGPO restores the other's state at that
	// fighter's last frame received and plays on with neutral inputs for it.
	// Kept for the watchers, the session plays on; otherwise it ends, for the
	// drop and not for a load of a save that kept no copy.
	for (const bool keep : {true, false}) {
		INFO("kept for watchers " << keep);
		std::set<int> skipped;
		LoopbackPair pair(kBugNone, kDelay, false, LoopbackPair::BeforeStart{},
			[&](RollbackSession::Config& a, RollbackSession::Config&) {
				a.onSaveSkipped = [&skipped](int frame) { skipped.insert(frame); };
			});
		REQUIRE(pair.RunTo(200));
		REQUIRE(pair.a.GetStats().savesSkipped > 0);
		if (keep) pair.a.KeepForWatchers();
		const int left = pair.a.Frame();
		pair.b.Close();
		// The drop comes after the pair's disconnect timeout (a second).
		const ULONGLONG start = GetTickCount64();
		while (!pair.a.Failed() && pair.a.Frame() < left + 200 && GetTickCount64() - start < 8000) {
			pair.a.Step(ScriptedInput(0, pair.a.Frame(), kLoopbackSeed));
			Sleep(0);
		}
		CHECK(pair.a.GetStats().skippedLoads == 0);
		for (int target : pair.gameA.RollbackTargets()) {
			INFO("A rolled back to frame " << target);
			CHECK(skipped.count(target) == 0);
		}
		if (keep) {
			// Played on past the drop: the reload came, and the rest of the
			// saves keep no copy (only A's own inputs are left to confirm).
			REQUIRE_FALSE(pair.a.Failed());
			CHECK(pair.a.Frame() >= left + 200);
			CHECK(!pair.gameA.RollbackTargets().empty());
			CHECK(pair.gameA.RollbackTargets().back() > left - 10);
		} else {
			REQUIRE(pair.a.Failed());
			CHECK(pair.a.FailureReason() == "the other player disconnected");
		}
	}
}

TEST_CASE("a save kept for a dump keeps its copy")
{
	LoopbackPair pair(kBugNone, kDelay);
	REQUIRE(pair.RunTo(100));
	const int kept = pair.a.Frame() + 20;
	pair.a.KeepSave(kept);
	REQUIRE(pair.RunTo(kept + 1));
	// The inputs that make it final arrive; no frame is saved meanwhile, so
	// its save is still GGPO's.
	for (int i = 0; i < 50; i++) {
		pair.a.Poll();
		pair.b.Poll();
		Sleep(1);
	}
	REQUIRE(pair.a.GetStats().savesSkipped > 0);
	const uint8_t* saved = pair.a.SavedState(kept);
	REQUIRE(saved != nullptr);
	uint64_t hash = 0;
	REQUIRE(pair.a.HashAt(kept, &hash)); // LoopbackPair hashes every frame
	CHECK(pair.gameA.Layout().HashSaved(saved, HashScope::Peers) == hash);
}

TEST_CASE("the synctest restores every save it makes")
{
	// The synctest keeps its own saves and restores them on purpose; the
	// session's savings never reach it.
	MockFighter game;
	game.Layout().TakeCosts();
	SyncTestOptions options;
	options.frames = 400;
	options.scheduleSeed = 5;
	options.inputs = [](int frame) { return ScriptedFrame(frame, kLoopbackSeed); };
	const SyncTestReport report = RunSyncTest(game, options);
	CHECK(report.ok);
	CHECK(report.rollbacks > 0);
	const StateLayout::Costs costs = game.Layout().TakeCosts();
	CHECK(costs.loads > 0);
	CHECK(costs.skippedSaves == 0);
	CHECK(costs.confirmedHashes == 0);
}
