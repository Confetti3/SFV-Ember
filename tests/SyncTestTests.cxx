#include <doctest/doctest.h>

#include "TestInputs.hxx"
#include "core/SyncTest.hxx"

#include <set>

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

// Counts frames in its layout, reads no inputs on frames [ignoreFrom,
// ignoreTo), and remembers every frame a load went back to and every input
// an ignored frame was given.
class FrameCounter : public GameAdapter {
public:
	FrameCounter(int ignoreFrom, int ignoreTo) : ignoreFrom_(ignoreFrom), ignoreTo_(ignoreTo)
	{
		layout_.Add("frame", &frame_, sizeof(frame_));
	}
	std::string BuildId() const override { return "counter"; }
	StateLayout& Layout() override { return layout_; }
	bool InputsMatter() const override { return frame_ < ignoreFrom_ || frame_ >= ignoreTo_; }
	void Advance(const FrameInputs& inputs, bool) override
	{
		if (!InputsMatter() && (inputs.p[0] || inputs.p[1])) inputsWhileIgnored++;
		frame_++;
	}
	void AfterLoad() override { loads.push_back(frame_); }
	std::vector<int> loads;
	int inputsWhileIgnored = 0;

private:
	int frame_ = 0;
	int ignoreFrom_;
	int ignoreTo_;
	StateLayout layout_;
};

} // namespace

TEST_CASE("checks start only on frames that read inputs, and replay across those that do not")
{
	FrameCounter game(23, 40);
	SyncTester tester(game, 7);
	for (int frame = 0; frame < 60; frame++) REQUIRE(tester.Step(ScriptedFrame(frame, 42)));
	// A check every 7 frames starts from the oldest frame within 7 that reads
	// inputs: at 7, 14, 21 from 0, 7, 14; at 28 from 21 (replaying across
	// 23..27, which read none); at 35 none (28..34 read none); at 42, 49 and
	// 56 from 40, 42 and 49.
	CHECK(tester.Report().rollbacks == 7);
	const std::vector<int> expected = {0, 7, 14, 21, 40, 42, 49};
	CHECK(game.loads == expected);
	CHECK(game.inputsWhileIgnored == 0); // played and replayed with neutral inputs
}

TEST_CASE("a mock fight whose inputs are ignored for a while survives perturbed rollbacks")
{
	MockFighter game;
	game.IgnoreInputsBetween(300, 360);
	SyncTester tester(game, 7, false, true);
	for (int frame = 0; frame < 600; frame++) REQUIRE(tester.Step(ScriptedFrame(frame, 42)));
	CHECK(tester.Report().ok);
	// 85 checks, less the 8 (at 308..357) whose window reads no inputs.
	CHECK(tester.Report().rollbacks == 77);
	bool crossed = false;
	for (const int target : game.RollbackTargets()) {
		CHECK((target < 300 || target >= 360));
		crossed |= target < 300 && target + 7 > 300;
	}
	CHECK(crossed); // some check replayed into the ignored stretch
}

TEST_CASE("a random schedule rolls back every depth, the same way for the same seed, and still finds leaks")
{
	auto run = [](uint32_t seed, std::set<int>* depths, std::set<int>* gaps) {
		FrameCounter game(0, 0); // every frame reads inputs
		SyncTester tester(game, 7, false, false, seed);
		int lastCheck = 0;
		for (int frame = 0; frame < 600; frame++) {
			const int before = tester.Report().rollbacks;
			REQUIRE(tester.Step(ScriptedFrame(frame, 42)));
			if (tester.Report().rollbacks == before) continue;
			if (depths) depths->insert(frame + 1 - game.loads.back());
			if (gaps) gaps->insert(frame + 1 - lastCheck);
			lastCheck = frame + 1;
		}
		return game.loads;
	};
	std::set<int> depths, gaps;
	const std::vector<int> loads = run(12345, &depths, &gaps);
	CHECK(depths == std::set<int>{1, 2, 3, 4, 5, 6, 7});
	CHECK(gaps == std::set<int>{1, 2, 3, 4, 5, 6, 7});
	CHECK(loads.size() > 600 / 7); // about every 4 frames on average
	CHECK(run(12345, nullptr, nullptr) == loads);
	CHECK(run(54321, nullptr, nullptr) != loads);

	MockFighter correct;
	SyncTester ok(correct, 8, false, true, 99);
	for (int frame = 0; frame < 900; frame++) REQUIRE(ok.Step(ScriptedFrame(frame, 42)));
	CHECK(ok.Report().ok);

	MockFighter leaky(kBugStateOutsideLayout);
	SyncTester found(leaky, 8, false, true, 99);
	bool going = true;
	for (int frame = 0; frame < 900 && going; frame++) going = found.Step(ScriptedFrame(frame, 42));
	CHECK_FALSE(found.Report().ok);
}
