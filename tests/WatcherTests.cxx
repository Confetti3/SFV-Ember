// A watcher of a room's battle (EMBER_LIBRARY.md section 25): two fighters'
// RollbackSessions and a third, watching, over loopback UDP, each running a
// MockFighter. The watcher plays P1's stream of the fighters' confirmed
// frames: its hashes are theirs, over the fight, a held WIN and a rematch in
// place; and a watcher that goes away, or never comes, never holds or ends
// the fight.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "TestInputs.hxx"
#include "core/RollbackSession.hxx"
#include "ember/RematchDecision.hxx"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

namespace {

using namespace sfve;
using namespace sfve::testbed;
namespace rip = sfve::adapter::rematch_in_place;
using Answer = rip::Answer;

constexpr std::uint32_t kAsked = sf4e::rematch::AskingBit;
constexpr std::uint32_t kChose = sf4e::rematch::ChoiceBit;

// P1 and P2 over loopback, P1 streaming to a watcher in match slot 2. The
// watcher starts with the fighters unless `watcherPort` is given apart from
// the sessions (a watcher P1 expects but that never comes).
struct LoopbackTrio {
	MockFighter gameA, gameB, gameW;
	RollbackSession a{gameA}, b{gameB}, w{gameW};
	uint16_t base = 0;

	explicit LoopbackTrio(int delay, bool watcherComes = true, uint64_t watcherDeadlineMs = 5000)
	{
		base = LoopbackPortBase();
		LoopbackPortBase(); // two more ports: the watcher's, and one spare
		RollbackSession::Config ca, cb, cw;
		ca.localPlayer = 0;
		ca.localPort = base;
		ca.remotePort = (uint16_t)(base + 1);
		ca.watchers.push_back({2, (uint16_t)(base + 2)});
		ca.watcherSyncDeadlineMs = watcherDeadlineMs;
		cb.localPlayer = 1;
		cb.localPort = (uint16_t)(base + 1);
		cb.remotePort = base;
		// Only P1 streams: a watcher list given to P2 is ignored.
		cb.watchers.push_back({2, (uint16_t)(base + 3)});
		ca.inputDelay = cb.inputDelay = delay;
		ca.verifyLoads = cb.verifyLoads = true; // every save hashed
		// Every hash kept, not only the latest 256, so the session's first
		// frames still compare.
		ca.keepAllHashes = cb.keepAllHashes = cw.keepAllHashes = true;
		ca.disconnectTimeoutMs = cb.disconnectTimeoutMs = 1000;
		cw.watching = true;
		cw.localPort = (uint16_t)(base + 2);
		cw.remotePort = base;
		cw.hashEveryFrames = 1;
		std::string error;
		REQUIRE_MESSAGE(a.Start(ca, &error), error);
		REQUIRE_MESSAGE(b.Start(cb, &error), error);
		if (watcherComes) REQUIRE_MESSAGE(w.Start(cw, &error), error);
	}

	// Steps the fighters until both ran `frames` frames, and the watcher
	// along with them, catching up as it can; false at the deadline or on a
	// failure.
	bool RunTo(int frames, bool watcherToo = true, DWORD deadlineMs = 30000)
	{
		const ULONGLONG start = GetTickCount64();
		while (a.Frame() < frames || b.Frame() < frames || (watcherToo && w.Live() && w.Frame() < frames)) {
			if (GetTickCount64() - start > deadlineMs) return false;
			if (a.Frame() < frames) a.Step(ScriptedInput(0, a.Frame(), kLoopbackSeed));
			else a.Poll();
			if (b.Frame() < frames) b.Step(ScriptedInput(1, b.Frame(), kLoopbackSeed));
			else b.Poll();
			if (watcherToo && w.Live()) {
				for (int extra = 0; extra < 4 && w.Frame() < frames; extra++) {
					if (w.Step(0) != RollbackSession::StepResult::Advanced) break;
				}
			}
			if (a.Failed() || b.Failed() || (watcherToo && w.Failed())) return false;
			Sleep(0);
		}
		return true;
	}

	// Frames hashed on all three, compared; how many.
	int CompareHashes(int from, int to)
	{
		int compared = 0;
		for (int f = from; f <= to; f++) {
			uint64_t ha = 0, hb = 0, hw = 0;
			if (!a.HashAt(f, &ha) || !b.HashAt(f, &hb) || !w.HashAt(f, &hw)) continue;
			INFO("frame " << f);
			CHECK(ha == hb);
			CHECK(hw == ha);
			compared++;
		}
		return compared;
	}
};

TEST_CASE("a watcher plays the fighters' confirmed frames and hashes the same states")
{
	for (const int delay : {1, 3}) {
		INFO("input delay " << delay);
		LoopbackTrio trio(delay);
		REQUIRE(trio.RunTo(400));
		trio.RunTo(410, false);
		CHECK(trio.w.Watching());
		CHECK_FALSE(trio.a.Watching());
		CHECK(trio.a.WatcherStreams() == 1u);
		CHECK(trio.b.WatcherStreams() == 0u); // P2 streams to nobody
		// Every frame a watcher has played is final.
		CHECK(trio.w.LastConfirmedFrame() == trio.w.Frame());
		const int confirmed = std::min({trio.a.LastConfirmedFrame(), trio.b.LastConfirmedFrame(), trio.w.Frame()});
		// Frame 0, the battle's first state, included.
		uint64_t h0 = 0, hw0 = 0;
		REQUIRE(trio.a.HashAt(0, &h0));
		REQUIRE(trio.w.HashAt(0, &hw0));
		CHECK(h0 == hw0);
		CHECK(trio.CompareHashes(0, confirmed) > 300);
		// The same per-region checkpoints too.
		std::vector<RegionHash> ra, rw;
		REQUIRE(trio.a.RegionHashesAt(360, &ra));
		REQUIRE(trio.w.RegionHashesAt(360, &rw));
		REQUIRE(ra.size() == rw.size());
		for (size_t i = 0; i < ra.size(); i++) CHECK(ra[i].hash == rw[i].hash);
		// The watcher never rolls back or sends anything of its own.
		CHECK(trio.w.GetStats().rollbacks == 0u);
		CHECK(trio.gameW.Frame() == trio.w.Frame());
		CHECK(trio.a.TakeDroppedWatchers().empty());
	}
}

// Each copy over the held WIN: the fighters send `from` answers as in
// RematchDecisionTests; the watcher sends nothing and decides from theirs.
struct HeldTrio {
	std::optional<int> decidedAt[3];
	Answer answer[3] = {Answer::Wait, Answer::Wait, Answer::Wait};
	bool ok = false;
};

HeldTrio RunHeld(LoopbackTrio& trio, int aFrom, int bFrom)
{
	HeldTrio run;
	RollbackSession* sessions[3] = {&trio.a, &trio.b, &trio.w};
	MockFighter* games[3] = {&trio.gameA, &trio.gameB, &trio.gameW};
	const int from[2] = {aFrom, bFrom};
	const ULONGLONG start = GetTickCount64();
	while (!run.decidedAt[0] || !run.decidedAt[1] || !run.decidedAt[2]) {
		if (GetTickCount64() - start > 30000) return run;
		for (int i = 0; i < 3; i++) {
			RollbackSession& s = *sessions[i];
			if (run.decidedAt[i]) {
				s.Poll();
				continue;
			}
			const int held = s.HeldFrames();
			const std::uint32_t bits = i == 2 ? 0 : from[i] < 0 ? rip::kDeclineBit : held >= from[i] ? kChose : kAsked;
			Answer answer = Answer::Wait;
			const RollbackSession::StepResult r = s.StepHeld(bits, [&](const FrameInputs& in) {
				answer = rip::Decide(in.p[0], in.p[1], s.HeldFrames());
				// The rematch's change to the game is the decided frame's.
				if (answer == Answer::Rematch) games[i]->Advance(FrameInputs{}, false);
			});
			if (r == RollbackSession::StepResult::Failed) return run;
			if (r == RollbackSession::StepResult::Advanced && answer != Answer::Wait) {
				run.decidedAt[i] = s.Frame();
				run.answer[i] = answer;
			}
		}
		Sleep(0);
	}
	run.ok = true;
	return run;
}

TEST_CASE("a watcher holds the WIN with the fighters and follows their answers")
{
	SUBCASE("both rematch: the watcher goes on in place on the same frame and stays in step")
	{
		LoopbackTrio trio(2);
		REQUIRE(trio.RunTo(300));
		trio.RunTo(310);
		// All three at the frame the fighters hold from, as the room's battle
		// is when each copy takes the match's end.
		REQUIRE(trio.a.Frame() == trio.b.Frame());
		const int heldFrom = trio.a.Frame();
		const ULONGLONG start = GetTickCount64();
		while (trio.w.Frame() < heldFrom && GetTickCount64() - start < 10000) {
			trio.a.Poll();
			trio.b.Poll();
			trio.w.Step(0);
		}
		REQUIRE(trio.w.Frame() == heldFrom);
		const HeldTrio run = RunHeld(trio, 15, 40);
		REQUIRE(run.ok);
		for (int i = 0; i < 3; i++) {
			INFO("copy " << i);
			CHECK(run.answer[i] == Answer::Rematch);
			CHECK(*run.decidedAt[i] == *run.decidedAt[0]);
		}
		CHECK(trio.gameW.Frame() == trio.gameA.Frame());
		const int resumed = trio.a.Frame();
		REQUIRE(trio.RunTo(resumed + 300));
		trio.RunTo(resumed + 310, false);
		const int confirmed = std::min({trio.a.LastConfirmedFrame(), trio.b.LastConfirmedFrame(), trio.w.Frame()});
		// The frozen WIN's frames and the next game's, all alike.
		CHECK(trio.CompareHashes(heldFrom, confirmed) > 300);
		CHECK(trio.w.GetStats().heldFrames > 40u);
		CHECK(trio.w.HeldFrames() == 0);
	}
	SUBCASE("one decline: the watcher goes back on the same frame, nothing restarted")
	{
		LoopbackTrio trio(2);
		REQUIRE(trio.RunTo(200));
		trio.RunTo(210);
		REQUIRE(trio.a.Frame() == trio.b.Frame());
		const int heldFrom = trio.a.Frame();
		const ULONGLONG start = GetTickCount64();
		while (trio.w.Frame() < heldFrom && GetTickCount64() - start < 10000) {
			trio.a.Poll();
			trio.b.Poll();
			trio.w.Step(0);
		}
		REQUIRE(trio.w.Frame() == heldFrom);
		const int gameFrame = trio.gameW.Frame();
		const HeldTrio run = RunHeld(trio, 10, -1);
		REQUIRE(run.ok);
		for (int i = 0; i < 3; i++) {
			INFO("copy " << i);
			CHECK(run.answer[i] == Answer::Leave);
			CHECK(*run.decidedAt[i] == *run.decidedAt[0]);
		}
		CHECK(trio.gameW.Frame() == gameFrame);
	}
}

TEST_CASE("a watcher that goes away never disturbs the fighters")
{
	LoopbackTrio trio(2);
	REQUIRE(trio.RunTo(200));
	trio.w.Close();
	// Well past P1's disconnect timeout for the watcher's silent link (1 s).
	const ULONGLONG start = GetTickCount64();
	bool warned = false;
	while (GetTickCount64() - start < 2500) {
		trio.a.Step(ScriptedInput(0, trio.a.Frame(), kLoopbackSeed));
		trio.b.Step(ScriptedInput(1, trio.b.Frame(), kLoopbackSeed));
		warned = warned || trio.a.ConnectionWarning() || trio.b.ConnectionWarning();
		REQUIRE_FALSE(trio.a.Failed());
		REQUIRE_FALSE(trio.b.Failed());
		Sleep(1);
	}
	CHECK_FALSE(warned); // the watcher's silence is not the fight's
	CHECK(trio.a.WatcherStreams() == 0u);
	const std::vector<size_t> dropped = trio.a.TakeDroppedWatchers();
	REQUIRE(dropped.size() == 1u);
	CHECK(dropped[0] == 2u); // its match slot, for the room to end its link
	CHECK(trio.a.TakeDroppedWatchers().empty()); // handed over once
	const int before = trio.a.Frame();
	REQUIRE(trio.RunTo(before + 120, false));
	CHECK(trio.a.Running());
}

TEST_CASE("a watcher that never synchronizes is dropped and the fight starts")
{
	// P1 expects a watcher on a port nobody answers: GGPO withholds RUNNING
	// until P1's policy drops it, at its deadline after the fighters synchronized.
	LoopbackTrio trio(2, false, 400);
	REQUIRE(trio.RunTo(120, false));
	CHECK(trio.a.WatcherStreams() == 0u);
	const std::vector<size_t> dropped = trio.a.TakeDroppedWatchers();
	REQUIRE(dropped.size() == 1u);
	CHECK(dropped[0] == 2u);
	CHECK_FALSE(trio.a.Failed());
}

TEST_CASE("a fighter's session kept for its watchers ignores the other fighter leaving")
{
	LoopbackTrio trio(2);
	REQUIRE(trio.RunTo(150));
	trio.a.KeepForWatchers();
	CHECK(trio.a.KeptForWatchers());
	trio.b.Close();
	const ULONGLONG start = GetTickCount64();
	while (GetTickCount64() - start < 2000) {
		trio.a.Poll();
		trio.w.Step(0);
		Sleep(1);
	}
	CHECK(trio.a.Live());
	CHECK_FALSE(trio.a.Failed());
	CHECK_FALSE(trio.a.ConnectionWarning());
	// The watcher played everything P1 confirmed before the end.
	CHECK(trio.w.Frame() >= trio.a.LastConfirmedFrame());
	CHECK_FALSE(trio.w.Failed());
}

} // namespace
