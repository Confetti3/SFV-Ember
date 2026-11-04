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

TEST_CASE("checks restore no state before a lockstep frame, and check the guesses before one")
{
	FrameCounter game;
	SyncTester tester(game, 7);
	for (int frame = 0; frame < 60; frame++) {
		const bool fighting = frame < 23 || frame >= 40;
		REQUIRE(tester.Step(ScriptedFrame(frame, 42), fighting ? FramePlay::MayRollBack : FramePlay::Lockstep));
	}
	// Checks at 7, 14, 21 restore 0, 7, 14. Before lockstep frame 23 the
	// guessed frames 16..22 are checked (restore 16). Frames 23..39 are
	// lockstep, so later checks restore no earlier than state 40: at 42 from
	// 40, at 49 from 42, at 56 from 49.
	CHECK(tester.Report().rollbacks == 7);
	const std::vector<int> expected = {0, 7, 14, 16, 40, 42, 49};
	CHECK(game.loads == expected);
}