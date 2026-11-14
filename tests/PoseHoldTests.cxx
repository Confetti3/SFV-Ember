// Pose holds (core/PoseHold.hxx, docs/design/beta9/pose-hold.md): an update
// whose read only a drawn frame of its start state gives right, and which did
// not have one, ends the tick held at that frame's start; the next tick plays
// it again, fresh. The book's decisions alone; then two sessions over GGPO
// (the GGPO port's pose-hold.patch) and a lagged replay, each over the mock
// fighter's pose reads (MockFighter::PoseReadsEvery), whose final states must
// be what a straight run that draws every frame makes.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "LossyLink.hxx"
#include "TestInputs.hxx"
#include "core/PoseHold.hxx"
#include "core/RollbackSession.hxx"
#include "core/SyncTest.hxx"

#include <windows.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::testbed;
using pose_hold::Book;
using pose_hold::Refusal;

TEST_CASE("the book: the earliest frame in a tick decides, and a refusal stands for the frames after it")
{
	Book book;
	Refusal why = Refusal::None;
	// Off: nothing is ever considered.
	book.Start(false);
	CHECK(book.Wanted(40, true, &why) == -1);

	book.Start(true);
	CHECK(book.Wanted(40, false, &why) == -1); // not wanted
	CHECK(book.Wanted(40, true, &why) == 40);
	CHECK(why == Refusal::None);
	book.Pend(40);
	CHECK(book.Pending() == 40);
	// A later frame of the same tick: the hold at 40 replays it.
	CHECK(book.Wanted(43, true, &why) == -1);
	// An earlier one (after a rollback loaded 38's state, which voided the
	// hold at 40, or not): it decides.
	CHECK(book.Loaded(38) == 40);
	CHECK(book.Pending() == -1);
	CHECK(book.Wanted(39, true, &why) == 39);
	book.Pend(39);
	// A load after the pending frame leaves it.
	CHECK(book.Loaded(41) == -1);
	CHECK(book.Pending() == 39);
	book.EndTick();
	CHECK(book.Pending() == -1);

	// A refusal: the frames after it keep live values this tick, an earlier
	// frame is still considered, and a load at or before it voids it.
	CHECK(book.Wanted(50, true, &why) == 50);
	CHECK_FALSE(book.Refuse(50));
	CHECK(book.Wanted(52, true, &why) == -1);
	CHECK(book.Wanted(49, true, &why) == 49);
	CHECK(book.Loaded(50) == -1);
	CHECK(book.Wanted(52, true, &why) == 52);
	// A pending hold after a refused frame is dropped with it.
	book.EndTick();
	book.Pend(60);
	CHECK(book.Refuse(58));
	CHECK(book.Pending() == -1);
	book.EndTick();

	// Frame 0 has no update before it.
	CHECK(book.Wanted(0, true, &why) == 0);
	CHECK(why == Refusal::FirstFrame);
}

TEST_CASE("the book: a hold's frames wait to be replayed, and its first update may not miss again")
{
	Book book;
	book.Start(true);
	Refusal why = Refusal::None;
	CHECK(book.Wanted(70, true, &why) == 70);
	book.Pend(70);
	book.Held(70, 76);
	book.EndTick();
	CHECK(book.Resuming());
	CHECK(book.HeldAt() == 70);
	CHECK(book.ResumeTo() == 76);
	CHECK(book.Pending() == -1);
	// The first update after the hold is the held frame's: a miss there means
	// it was not fresh, and no second hold is made for it.
	CHECK(book.Wanted(70, true, &why) == 70);
	CHECK(why == Refusal::NotFreshAfterHold);
	book.Refuse(70);
	// The next miss, a later frame, is a new hold's.
	book.EndTick();
	CHECK(book.Wanted(73, true, &why) == 73);
	CHECK(why == Refusal::None);
	book.Resumed();
	CHECK_FALSE(book.Resuming());

	// Only the first update after a hold is checked: one that ran fresh at
	// the held frame clears it, and a later miss at that frame (another
	// rollback, another timeline) may hold again.
	book.Start(true);
	book.Pend(80);
	book.Held(80, 84);
	book.EndTick();
	CHECK(book.Wanted(80, false, &why) == -1);
	book.Resumed();
	CHECK(book.Wanted(80, true, &why) == 80);
	CHECK(why == Refusal::None);
}

TEST_CASE("every refusal has a name")
{
	for (int i = 0; i < pose_hold::kRefusals; i++) {
		INFO(i);
		CHECK(std::string(pose_hold::RefusalName(static_cast<Refusal>(i))) != "?");
	}
}

namespace {

constexpr int kDelay = 3;
constexpr int kFrameMs = 4;

// The mock's pose reads for a test.
struct Poses {
	int every = 5;
	int run = 1;
	bool needsPlacement = false;
};

// Two sessions over a lossy, laggy loopback link (as RollbackCostTests'), the
// mocks making pose reads, each checkpoint's reports kept.
struct HoldPair {
	std::unique_ptr<LossyLink> link;
	std::map<int, std::vector<uint64_t>> reportsA, reportsB;
	std::unique_ptr<LoopbackPair> pair;

	HoldPair(uint32_t seed, bool holds, const Poses& poses)
	{
		// Every frame's hash kept, so every final frame compares.
		pair = std::make_unique<LoopbackPair>(kBugNone, kDelay, true,
			[&](MockFighter& a, MockFighter& b, uint16_t, uint16_t) {
				a.PoseReadsEvery(poses.every, poses.run, poses.needsPlacement);
				b.PoseReadsEvery(poses.every, poses.run, poses.needsPlacement);
				// The engine has drawn the battle's first state.
				a.Draw();
				b.Draw();
			},
			[&](RollbackSession::Config& a, RollbackSession::Config& b) {
				LossyLink::Shape shape;
				shape.lagMs = 2;
				shape.jitterMs = 30;
				shape.lossPercent = 5;
				shape.seed = seed;
				link = std::make_unique<LossyLink>(a.localPort, b.localPort, shape);
				REQUIRE(link->Ready());
				a.remotePort = link->PortForA();
				b.remotePort = link->PortForB();
				a.poseHolds = b.poseHolds = holds;
				a.onCheckpoint = [this](int frame, uint64_t hash) { reportsA[frame].push_back(hash); };
				b.onCheckpoint = [this](int frame, uint64_t hash) { reportsB[frame].push_back(hash); };
			});
		pair->pump = [this] {
			link->Lose(pair->a.Running() && pair->b.Running());
			link->Pump();
		};
	}
};

// Steps both peers as a game does, one attempt every kFrameMs each, and draws
// each one's frame after its call (the engine's frame), until both have run
// `frames` frames; then keeps polling (and drawing) for `settleMs`.
bool RunDrawn(LoopbackPair& pair, int frames, double settleMs = 400, double deadlineMs = 40000)
{
	const double start = LossyLink::NowMs();
	double due[2] = {start, start};
	RollbackSession* sessions[2] = {&pair.a, &pair.b};
	MockFighter* games[2] = {&pair.gameA, &pair.gameB};
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
					games[p]->Draw();
					due[p] = std::max(due[p] + kFrameMs, now - 4.0 * kFrameMs);
				}
			} else {
				s.Poll();
				games[p]->Draw();
			}
			if (s.Failed()) {
				MESSAGE((p == 0 ? "A" : "B") << " failed at frame " << s.Frame() << ": " << s.FailureReason());
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

// The peers' hash of a straight run that draws every frame, with both
// players' inputs kDelay frames late as the sessions play them.
std::vector<uint64_t> DrawnReference(int frames, const Poses& poses)
{
	MockFighter reference;
	reference.PoseReadsEvery(poses.every, poses.run, poses.needsPlacement);
	std::vector<uint64_t> hashes{reference.Layout().HashLive(HashScope::Peers)};
	for (int f = 0; f < frames; f++) {
		reference.Draw();
		FrameInputs sent;
		for (int p = 0; p < kPlayers; p++) sent.p[p] = f >= kDelay ? ScriptedInput(p, f - kDelay, kLoopbackSeed) : 0;
		reference.Advance(GivenInputs(reference, sent, f), false);
		hashes.push_back(reference.Layout().HashLive(HashScope::Peers));
	}
	REQUIRE(reference.PoseMisses() == 0);
	return hashes;
}

// How many of the frames both peers hashed (every frame: LoopbackPair's
// verifyLoads) differ from the reference; `compared` says how many compared.
int Differing(const LoopbackPair& pair, const std::vector<uint64_t>& reference, int* compared)
{
	const int confirmed = std::min({pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame(), pair.a.Frame(), pair.b.Frame()});
	int differing = 0;
	*compared = 0;
	for (int f = 1; f <= confirmed && f < (int)reference.size(); f++) {
		for (const RollbackSession* s : {&pair.a, &pair.b}) {
			uint64_t hash = 0;
			if (!s->HashAt(f, &hash)) continue;
			(*compared)++;
			if (hash != reference[(size_t)f]) differing++;
		}
	}
	return differing;
}

std::string RefusalsText(const MockFighter& game)
{
	std::string text;
	for (int i = 1; i < pose_hold::kRefusals; i++) {
		const Refusal why = static_cast<Refusal>(i);
		if (game.PoseHoldsRefused(why)) text += std::string(pose_hold::RefusalName(why)) + " " + std::to_string(game.PoseHoldsRefused(why)) + "; ";
	}
	return text;
}

void Describe(const char* name, const RollbackSession& s, const MockFighter& game)
{
	const RollbackSession::Stats& st = s.GetStats();
	MESSAGE(name << ": " << st.rollbacks << " rollbacks, " << st.poseHolds << " pose holds, " << st.poseHoldsRefused
				 << " refused; the mock: " << game.PoseMisses() << " misses, " << game.PoseHoldsPending() << " pending, "
				 << game.PoseHoldsCancelled() << " dropped, " << game.PoseHoldsMade() << " held, " << game.FreshAfterHold()
				 << " fresh after, " << game.NotFreshAfterHold() << " not fresh after, " << game.PoseHoldsRefused()
				 << " refused: " << RefusalsText(game));
}

} // namespace

TEST_CASE("over GGPO, a read a rollback misses is held for a drawn frame and both peers end where a straight run does")
{
	constexpr int kFrames = 900;
	const Poses poses{5, 1, false};
	const std::vector<uint64_t> reference = DrawnReference(kFrames + 40, poses);
	HoldPair holding(31, true, poses);
	LoopbackPair& pair = *holding.pair;
	REQUIRE(RunDrawn(pair, kFrames));
	Describe("A", pair.a, pair.gameA);
	Describe("B", pair.b, pair.gameB);
	const RollbackSession::Stats& sa = pair.a.GetStats();
	const RollbackSession::Stats& sb = pair.b.GetStats();
	// Holds were made, and each one's frame was played again fresh (the last
	// may still wait for its replay as the run ends).
	CHECK(sa.poseHolds + sb.poseHolds > 0);
	CHECK(pair.gameA.NotFreshAfterHold() + pair.gameB.NotFreshAfterHold() == 0);
	CHECK(pair.gameA.FreshAfterHold() >= pair.gameA.PoseHoldsMade() - 1);
	CHECK(pair.gameB.FreshAfterHold() >= pair.gameB.PoseHoldsMade() - 1);
	CHECK(sa.poseHoldsRefused + sb.poseHoldsRefused == 0);
	CHECK(sa.loadMismatches + sb.loadMismatches == 0);
	int compared = 0;
	CHECK(Differing(pair, reference, &compared) == 0);
	CHECK(compared > 1000);
	// Every checkpoint reported was final and the straight run's: no state
	// the tick played on a live value was ever hashed.
	for (const auto* reports : {&holding.reportsA, &holding.reportsB}) {
		for (const auto& [frame, hashes] : *reports) {
			INFO("checkpoint " << frame);
			REQUIRE(frame < (int)reference.size());
			for (uint64_t hash : hashes) CHECK(hash == reference[(size_t)frame]);
		}
	}
}

TEST_CASE("without holds the same match leaves the straight run: the misses are real")
{
	constexpr int kFrames = 900;
	const Poses poses{5, 1, false};
	const std::vector<uint64_t> reference = DrawnReference(kFrames + 40, poses);
	HoldPair live(31, false, poses);
	REQUIRE(RunDrawn(*live.pair, kFrames));
	Describe("A", live.pair->a, live.pair->gameA);
	Describe("B", live.pair->b, live.pair->gameB);
	CHECK(live.pair->a.GetStats().poseHolds + live.pair->b.GetStats().poseHolds == 0);
	CHECK(live.pair->gameA.PoseMisses() + live.pair->gameB.PoseMisses() > 0);
	int compared = 0;
	CHECK(Differing(*live.pair, reference, &compared) > 0);
}

TEST_CASE("a hold whose own replay of the frame before misses too goes back a frame, over GGPO")
{
	// Two reading frames in every three, and a stale read takes the record
	// only once an update ran since the last restore: a rollback's first
	// replayed frame, and a hold's own replay of the frame before the held
	// one, miss whenever they read.
	constexpr int kFrames = 900;
	const Poses poses{3, 2, true};
	const std::vector<uint64_t> reference = DrawnReference(kFrames + 40, poses);
	HoldPair holding(41, true, poses);
	LoopbackPair& pair = *holding.pair;
	REQUIRE(RunDrawn(pair, kFrames));
	Describe("A", pair.a, pair.gameA);
	Describe("B", pair.b, pair.gameB);
	const RollbackSession::Stats& sa = pair.a.GetStats();
	const RollbackSession::Stats& sb = pair.b.GetStats();
	CHECK(sa.poseHolds + sb.poseHolds > 0);
	CHECK(pair.gameA.NotFreshAfterHold() + pair.gameB.NotFreshAfterHold() == 0);
	// Some chains reach past the saves still kept and are refused (the hold
	// one frame later stands): the mock's record needs the models placed for
	// every stale read, harsher than SFV's, whose exact readings do not.
	// Whatever was refused was on frames a later rollback or hold replays.
	// Some holds were chained: more pending holds than holds made and dropped.
	CHECK(pair.gameA.PoseHoldsPending() + pair.gameB.PoseHoldsPending() >
		pair.gameA.PoseHoldsMade() + pair.gameB.PoseHoldsMade() + pair.gameA.PoseHoldsCancelled() +
			pair.gameB.PoseHoldsCancelled());
	int compared = 0;
	CHECK(Differing(pair, reference, &compared) == 0);
	CHECK(compared > 1000);
}

namespace {

ReplayFrames Recording(int count)
{
	ReplayFrames frames((size_t)count);
	for (int frame = 0; frame < count; frame++) {
		frames[(size_t)frame].inputs = ScriptedFrame(frame, 42);
		frames[(size_t)frame].flags = RecordedFrame::kFight;
	}
	return frames;
}

// A lagged replay of `frames` with pose reads, a drawn frame after each Step
// and Finish; how many of its final states differ from `straight`.
int LaggedWithPoses(const ReplayFrames& frames, const std::vector<uint64_t>& straight, const Poses& poses, bool holds,
	int lag, int remote, MockFighter& game, int* compared, int* holdsMade)
{
	game.PoseReadsEvery(poses.every, poses.run, poses.needsPlacement);
	game.Draw();
	LaggedReplay replay(game, frames, remote, lag, 0, holds);
	int differing = 0;
	int next = 0;
	const auto compare = [&] {
		for (; next <= replay.ConfirmedThrough(); next++) {
			const uint8_t* bytes = replay.SavedAt(next);
			if (!bytes) continue;
			(*compared)++;
			if (game.Layout().HashSaved(bytes, HashScope::Peers) != straight[(size_t)next]) differing++;
		}
	};
	for (int frame = 0; frame < (int)frames.size(); frame++) {
		replay.Step(frame);
		game.Draw();
		compare();
	}
	for (int turns = 0; !replay.Finish() && turns < 100; turns++) {
		game.Draw();
		compare();
	}
	compare();
	// Every state final, no hold left waiting.
	CHECK(replay.ConfirmedThrough() == (int)frames.size());
	*holdsMade = replay.PoseHolds();
	return differing;
}

} // namespace

TEST_CASE("a lagged replay holds as a session does, and its final states are the straight run's")
{
	constexpr int kFrames = 900;
	const ReplayFrames frames = Recording(kFrames);
	for (const Poses poses : {Poses{5, 1, false}, Poses{3, 2, true}}) {
		std::vector<uint64_t> straight;
		{
			MockFighter game;
			game.PoseReadsEvery(poses.every, poses.run, poses.needsPlacement);
			for (int frame = 0; frame < kFrames; frame++) {
				straight.push_back(game.Layout().HashLive(HashScope::Peers));
				game.Draw();
				game.Advance(GivenInputs(game, frames[(size_t)frame].inputs, frame), false);
			}
			straight.push_back(game.Layout().HashLive(HashScope::Peers));
			REQUIRE(game.PoseMisses() == 0);
		}
		for (int lag : {2, 4, 7}) {
			INFO("every " << poses.every << " run " << poses.run << ", lag " << lag);
			MockFighter held;
			int compared = 0, holdsMade = 0;
			CHECK(LaggedWithPoses(frames, straight, poses, true, lag, 0, held, &compared, &holdsMade) == 0);
			CHECK(compared >= kFrames - 10);
			CHECK(holdsMade > 0);
			MESSAGE("lag " << lag << ", every " << poses.every << " run " << poses.run << ": " << holdsMade << " holds, "
				<< held.FreshAfterHold() << " fresh after, " << held.NotFreshAfterHold() << " not, refused: " << RefusalsText(held));
			// As over GGPO, a chain may reach past the saves kept (lag + 3 here).
			if (poses.run == 1) CHECK(held.PoseHoldsRefused() == 0);
			CHECK(held.NotFreshAfterHold() == 0);
			CHECK(held.FreshAfterHold() >= held.PoseHoldsMade() - 1);
			MockFighter live;
			compared = holdsMade = 0;
			CHECK(LaggedWithPoses(frames, straight, poses, false, lag, 0, live, &compared, &holdsMade) > 0);
			CHECK(holdsMade == 0);
		}
	}
}

TEST_CASE("a hold whose next update finds no drawn frame is refused, never held again for that frame")
{
	// The engine does not draw between two ticks: the held frame's replay is
	// stale, misses again, and keeps the live value: the playback goes on.
	constexpr int kFrames = 300;
	const ReplayFrames frames = Recording(kFrames);
	MockFighter game;
	game.PoseReadsEvery(5, 1, false);
	game.Draw();
	LaggedReplay replay(game, frames, 0, 4, 0, true);
	for (int frame = 0; frame < kFrames; frame++) replay.Step(frame); // never drawn
	for (int turns = 0; !replay.Finish() && turns < 100; turns++) {
	}
	CHECK(replay.PoseHolds() > 0);
	CHECK(game.FreshAfterHold() == 0);
	CHECK(game.PoseHoldsRefused() > 0);
	// Each hold's first update missed and was refused (not held again).
	CHECK(game.PoseHoldsRefused() >= game.PoseHoldsMade() - 1);
	CHECK(replay.ConfirmedThrough() == kFrames);
}
