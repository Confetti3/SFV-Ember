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
