#include "mod/UpdateCosts.hxx"

#include "mod/Log.hxx"

#include <windows.h>

namespace sfve::mod {

namespace {

// How often the costs of the session's work are logged: every this many
// updates played forward (rollbacks' replays not counted).
constexpr uint64_t kCostAdvances = 600;

// A rendered frame longer than this many milliseconds is logged (FrameEnded).
constexpr int64_t kHitchMs = 120;

} // namespace

int64_t UpdateCosts::Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

void UpdateCosts::FrameEnded()
{
	const bool due = paceClock_.IsDue();
	const int64_t length = paceClock_.Frame(Now());
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	if (!frequency.QuadPart || length * 1000 < kHitchMs * frequency.QuadPart) return;
	const PaceCounts counts = paceSource_ ? paceSource_() : PaceCounts{};
	Log("pace: a frame took %.0f ms (%s; session frame %d, rollbacks so far %llu, waiting steps %llu)",
		1000.0 * (double)length / (double)frequency.QuadPart, due ? "an update was due" : "the battle stood still", counts.frame,
		(unsigned long long)counts.rollbacks, (unsigned long long)counts.waiting);
}

void UpdateCosts::EndBattle()
{
	{
		// The pace of the battle that ends and the controller events in it
		// (mod/Pace.hxx); the next battle's record begins.
		pace::Summary summary;
		std::vector<pace::PadEvent> events;
		size_t totalEvents = 0;
		pace::Current().End(&summary, &events, &totalEvents);
		if (summary.windows) Log("%s", pace::SummaryText(summary).c_str());
		if (summary.windows || totalEvents) Log("pace: %s", pace::PadEventsText(events, totalEvents).c_str());
		pace::Current().Begin();
	}
	paceClock_ = {};
	paceBegun_ = false;
	clockAtStart_ = {};
	paceAtStart_ = {};
	costs_ = {};
}

void UpdateCosts::BeforeUpdate(bool resimulating)
{
	if (!resimulating && !paceBegun_) {
		paceBegun_ = true;
		clockAtStart_ = paceClock_.Read();
		paceAtStart_ = paceSource_ ? paceSource_() : PaceCounts{};
	}
}

void UpdateCosts::Played(int64_t took, bool resimulating, StateLayout& stateLayout)
{
	if (resimulating) {
		costs_.replayed++;
		costs_.replayedTicks += took;
		return;
	}
	costs_.ticks++;
	costs_.tickTicks += took;
	paceClock_.Due();
	if (paceSource_) pace::Current().SetFrame(paceSource_().frame);
	if (costs_.ticks >= kCostAdvances) LogCosts(stateLayout);
}

void UpdateCosts::LogCosts(StateLayout& stateLayout)
{
	const StateLayout::Costs layout = stateLayout.TakeCosts();
	LARGE_INTEGER frequency{};
	QueryPerformanceFrequency(&frequency);
	// The time of this window's frames an update was due in, and what the
	// session did in it: a slow own game has few rollbacks and no waiting
	// steps, waiting on the peer has waiting steps. The frames the battle
	// stood still in (the WIN's answers, a decided match's end) are told apart.
	const pace::FrameClock::Totals clock = paceClock_.Read();
	const PaceCounts counts = paceSource_ ? paceSource_() : PaceCounts{};
	const auto seconds = [&](int64_t ticks) { return frequency.QuadPart ? (double)ticks / (double)frequency.QuadPart : 0.0; };
	pace::Window window;
	window.updates = costs_.ticks;
	window.seconds = seconds(clock.due - clockAtStart_.due);
	window.heldSeconds = seconds(clock.held - clockAtStart_.held);
	window.rollbacks = counts.rollbacks - paceAtStart_.rollbacks;
	window.waiting = counts.waiting - paceAtStart_.waiting;
	const bool slow = pace::Current().AddWindow(window);
	const std::string paceText = pace::WindowText(window);
	// Average microseconds per call, and the calls.
	const auto us = [&](int64_t ticks, uint64_t calls) {
		return calls && frequency.QuadPart ? 1e6 * (double)ticks / (double)frequency.QuadPart / (double)calls : 0.0;
	};
	// The session's counts (docs/design/beta8/perf.md): saves that kept no
	// copy, and of the hashes, those made once a frame was final; saves a
	// rollback replaced before they were final, whose hash was never needed.
	Log("costs: the last %llu updates: tick %.0f us; replayed tick %.0f us (%llu); save %.0f us (%llu, %llu skipped), "
		"load %.0f us (%llu), hash %.0f us (%llu, %llu once final, %llu not needed), the restore's callbacks %.0f us (%llu); %s",
		(unsigned long long)costs_.ticks, us(costs_.tickTicks, costs_.ticks), us(costs_.replayedTicks, costs_.replayed),
		(unsigned long long)costs_.replayed, us(layout.saveTicks, layout.saves), (unsigned long long)layout.saves,
		(unsigned long long)layout.skippedSaves, us(layout.loadTicks, layout.loads), (unsigned long long)layout.loads,
		us(layout.hashTicks, layout.hashes), (unsigned long long)layout.hashes, (unsigned long long)layout.confirmedHashes,
		(unsigned long long)layout.droppedHashes, us(costs_.callbackTicks, costs_.callbacks),
		(unsigned long long)costs_.callbacks, paceText.c_str());
	if (slow) Log("%s", pace::SlowText(window).c_str());
	costs_ = {};
	clockAtStart_ = clock;
	paceAtStart_ = counts;
}

} // namespace sfve::mod
