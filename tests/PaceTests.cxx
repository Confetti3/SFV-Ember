// The pace of a session's battle (mod/Pace.hxx): the window arithmetic, the log
// texts and the battle's record of controller events.

#include <doctest/doctest.h>

#include "mod/Pace.hxx"

namespace pace = sfve::mod::pace;

TEST_CASE("a window's rate is its updates over its seconds, and under 55 a second is slow")
{
	pace::Window window;
	window.updates = 600;
	window.seconds = 10.0;
	CHECK(pace::Rate(window) == doctest::Approx(60.0));
	CHECK_FALSE(pace::IsSlow(window));
	window.seconds = 16.6;
	CHECK(pace::Rate(window) == doctest::Approx(36.14).epsilon(0.001));
	CHECK(pace::IsSlow(window));
	window.seconds = 600.0 / 55.0; // exactly 55 a second is not slow
	CHECK_FALSE(pace::IsSlow(window));
	window.seconds = 0; // no time measured: no rate, never slow
	CHECK(pace::Rate(window) == 0.0);
	CHECK_FALSE(pace::IsSlow(window));
}

TEST_CASE("the summary keeps the windows, the slowest, the slow count and the last rate")
{
	pace::Summary summary;
	CHECK_FALSE(pace::Add(summary, {600, 10.0, 0, 0}));
	CHECK(pace::Add(summary, {600, 20.0, 1, 2}));
	CHECK(pace::Add(summary, {600, 15.0, 0, 0}));
	CHECK_FALSE(pace::Add(summary, {600, 9.9, 0, 0}));
	CHECK_FALSE(pace::Add(summary, {0, 10.0, 0, 0})); // no updates: not a window
	CHECK_FALSE(pace::Add(summary, {600, 0.0, 0, 0})); // no time: not a window
	CHECK(summary.windows == 4);
	CHECK(summary.slow == 2);
	CHECK(summary.slowest == doctest::Approx(30.0));
	CHECK(summary.last == doctest::Approx(600 / 9.9));
}

TEST_CASE("the log texts")
{
	CHECK(pace::WindowText({600, 16.6, 0, 0}) == "pace 600 updates in 16.6 s (36.1 a second)");
	CHECK(pace::SlowText({600, 16.6, 3, 12}) ==
		"pace: slow: 36.1 updates a second over the last 600 (rollbacks 3, frames held waiting for the other player 12)");
	pace::Summary summary;
	pace::Add(summary, {600, 10.0, 0, 0});
	pace::Add(summary, {600, 16.6, 0, 0});
	CHECK(pace::SummaryText(summary) == "pace: battle: 2 windows, slowest 36.1 a second, 1 under 55 a second");
}

TEST_CASE("the controller events list their frames, a few at most")
{
	CHECK(pace::PadEventsText({}, 0) == "controller events: none");
	CHECK(pace::PadEventsText({{false, 1200}, {true, 1560}}, 2) == "controller events: 2 (removed at frame 1200, added at frame 1560)");
	CHECK(pace::PadEventsText({{true, -1}}, 1) == "controller events: 1 (added at frame ?)");
	std::vector<pace::PadEvent> many(20, pace::PadEvent{true, 7});
	const std::string text = pace::PadEventsText(many, 20);
	CHECK(text.find("controller events: 20 (") == 0);
	CHECK(text.find(", ...)") != std::string::npos);
}

TEST_CASE("the report's pace text names the last window before the mismatch")
{
	pace::Summary summary;
	CHECK(pace::ReportText(summary, {}, 0) == "not measured yet (under 600 updates played); controller events: none");
	pace::Add(summary, {600, 10.0, 0, 0});
	pace::Add(summary, {600, 16.6, 0, 0});
	CHECK(pace::ReportText(summary, {{false, 90}}, 1) ==
		"2 windows of 600 updates, slowest 36.1 a second, 1 under 55 a second, the last window 36.1 a second; "
		"controller events: 1 (removed at frame 90)");
}

TEST_CASE("the battle records controller events only while it is live, and forgets them at its end")
{
	pace::Battle battle;
	battle.NotePad(false); // before any battle: not counted
	battle.Begin();
	battle.SetFrame(300);
	battle.NotePad(false);
	battle.SetFrame(660);
	battle.NotePad(true);
	CHECK(battle.AddWindow({600, 20.0, 0, 0}));
	CHECK(battle.Report().find("controller events: 2 (removed at frame 300, added at frame 660)") != std::string::npos);
	CHECK(battle.Report().find("1 windows of 600 updates") == 0);
	pace::Summary summary;
	std::vector<pace::PadEvent> events;
	std::size_t total = 0;
	battle.End(&summary, &events, &total);
	CHECK(summary.windows == 1);
	CHECK(total == 2);
	battle.NotePad(true); // after the end: not counted
	CHECK(battle.Report().find("controller events: none") != std::string::npos);
	// A flapping pad keeps a bounded list but counts every event.
	battle.Begin();
	for (int i = 0; i < 100; ++i) battle.NotePad(i % 2 == 0);
	battle.End(&summary, &events, &total);
	CHECK(total == 100);
	CHECK(events.size() == pace::kMostKeptEvents);
}
