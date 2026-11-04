#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/SyncTest.hxx"

using namespace sfve;
using namespace sfve::testbed;

namespace {

SyncTestOptions Options(int frames = 900, int distance = 7)
{
	SyncTestOptions o;
	o.frames = frames;
	o.distance = distance;
	o.inputs = [](int frame) { return ScriptedFrame(frame, 42); };
	return o;
}

} // namespace

TEST_CASE("a correct game survives rollbacks at every distance GGPO uses")
{
	for (int distance = 1; distance <= 8; distance++) {
		MockFighter game;
		const SyncTestReport report = RunSyncTest(game, Options(600, distance));
		INFO("distance " << distance << ": " << report.message);
		CHECK(report.ok);
		CHECK(report.rollbacks == 600 / distance);
		CHECK(game.ResimulatedFrames() == report.rollbacks * distance);
	}
}

TEST_CASE("the scripted match actually fights")
{
	MockFighter game;
	for (int frame = 0; frame < 900; frame++) game.Advance(ScriptedFrame(frame, 42), false);
	MESSAGE("health after 900 frames: " << game.Health(0) << " / " << game.Health(1));
	CHECK(game.Health(0) < 1000);
	CHECK(game.Health(1) < 1000);
}

TEST_CASE("state kept outside the layout is found and located")
{
	MockFighter game(kBugStateOutsideLayout);
	const SyncTestReport report = RunSyncTest(game, Options());
	CHECK_FALSE(report.ok);
	CHECK_FALSE(report.loadMismatch);
	REQUIRE(report.diffs.size() == 1);
	CHECK(report.diffs[0].region == "world");
}

TEST_CASE("a simulation that reads something other than its inputs is found")
{
	MockFighter game(kBugReadsNonInput);
	const SyncTestReport report = RunSyncTest(game, Options());
	CHECK_FALSE(report.ok);
	CHECK(report.failedFrame > 0);
}

TEST_CASE("an unmasked pointer does not break one process, only two")
{
	// Within one process the pointer is the same before and after a rollback,
	// so the synctest passes; RollbackSessionTests shows it across two
	// instances, which is what two PCs look like.
	MockFighter game(kBugUnmaskedPointer);
	CHECK(RunSyncTest(game, Options(300)).ok);
}

TEST_CASE("learning mode tallies the regions that differ and keeps playing")
{
	MockFighter game(kBugStateOutsideLayout);
	SyncTester tester(game, 7, true);
	for (int frame = 0; frame < 900; frame++) REQUIRE(tester.Step(ScriptedFrame(frame, 42)));
	const SyncTestReport& r = tester.Report();
	CHECK(r.ok);
	CHECK(r.framesRun == 900);
	CHECK(r.failures > 0);
	REQUIRE(r.regionFailures.count("world") == 1);
	CHECK(r.regionFailures.at("world") == r.failures);
	CHECK(r.regionFailures.count("presentation") == 0);
}

TEST_CASE("perturbed replays pass for a correct game and catch state the wrong timeline leaves behind")
{
	{
		MockFighter game;
		SyncTester tester(game, 7, false, true);
		for (int frame = 0; frame < 900; frame++) REQUIRE(tester.Step(ScriptedFrame(frame, 42)));
	}
	MockFighter leaky(kBugStateOutsideLayout);
	SyncTester tester(leaky, 7, false, true);
	bool going = true;
	for (int frame = 0; frame < 900 && going; frame++) going = tester.Step(ScriptedFrame(frame, 42));
	CHECK_FALSE(tester.Report().ok);
}


namespace {

// A game whose state lives outside the layout, kept in the layout as a record
// the adapter captures before every save and hash and writes back after a
// load (how SfvAdapter keeps the round flow).
class RecordedGame : public GameAdapter {
public:
	explicit RecordedGame(bool captures) : captures_(captures) { layout_.Add("record", &record_, sizeof(record_)); }
	std::string BuildId() const override { return "recorded"; }
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs& inputs, bool) override { hidden_ = hidden_ * 31 + inputs.p[0] + 1; }
	void Capture() override
	{
		if (captures_) record_ = hidden_;
	}
	void AfterLoad() override { hidden_ = record_; }
	int64_t Hidden() const { return hidden_; }

private:
	bool captures_;
	int64_t hidden_ = 0;
	int64_t record_ = 0;
	StateLayout layout_;
};

// Counts frames in its layout and remembers every frame a load went back to.
class FrameCounter : public GameAdapter {
public:
	FrameCounter() { layout_.Add("frame", &frame_, sizeof(frame_)); }
	std::string BuildId() const override { return "counter"; }
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs&, bool) override { frame_++; }
	void AfterLoad() override { loads.push_back(frame_); }
	std::vector<int> loads;

private:
	int frame_ = 0;
	StateLayout layout_;
};

} // namespace

TEST_CASE("a record the adapter captures before saves and hashes is rolled back like memory")
{
	RecordedGame offline(true);
	for (int frame = 0; frame < 300; frame++) offline.Advance(ScriptedFrame(frame, 42), false);
	RecordedGame game(true);
	CHECK(RunSyncTest(game, Options(300)).ok);
	CHECK(game.Hidden() == offline.Hidden());
	// Without the capture every save holds a stale record, so each rollback
	// throws the state away.
	RecordedGame stale(false);
	RunSyncTest(stale, Options(300));
	CHECK(stale.Hidden() != offline.Hidden());
}

TEST_CASE("frames played in lockstep are never rolled back, and entering lockstep checks the guesses before it")
{
	FrameCounter game;
	SyncTester tester(game, 7);
	for (int frame = 0; frame < 60; frame++) {
		const bool fighting = frame < 23 || frame >= 40;
		REQUIRE(tester.Step(ScriptedFrame(frame, 42), fighting));
	}
	// Checks at 7, 14, 21; the lockstep entry after frame 23 (state 24);
	// none until 7 frames past the last lockstep frame (state 40): 49, 56.
	CHECK(tester.Report().rollbacks == 6);
	const std::vector<int> expected = {0, 7, 14, 17, 42, 49};
	CHECK(game.loads == expected);
}