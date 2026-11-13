#pragma once

// The pace of a session's battle, for the log and the desync report (logging
// only: nothing here is read by the simulation, hashed or saved).
//
// A game that runs at 20-40 updates a second for minutes with no rollback
// feels like inputs that come late or not at all, and nothing in the log named
// it. The adapter measures the wall-clock time (a monotonic clock) of each
// window of updates played forward (its `costs:` line's window, 600 updates),
// and this file holds the arithmetic, the texts and the battle's record:
//
//  - Window / Summary: a window's updates and seconds, with the session's
//    rollbacks and its waiting steps (the calls that ran no frame because
//    the other player's inputs or the gate were not there yet) in it. A
//    slow own game has few of both; waiting on the peer has waiting steps.
//  - Battle: the battle's summary and the controller removed / added events
//    seen in it with their session frames, behind a mutex (the pad layer's
//    thread writes, the game thread and the report thread read).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace sfve::mod::pace {

// Windows below this many updates a second are slow (the game runs 60).
constexpr double kSlowRate = 55.0;
// How many controller events a line lists.
constexpr std::size_t kMostListedEvents = 8;
// How many are kept (a pad that flaps for a whole battle).
constexpr std::size_t kMostKeptEvents = 64;

struct Window {
	std::uint64_t updates = 0;
	double seconds = 0;
	std::uint64_t rollbacks = 0; // the session's, during the window
	std::uint64_t waiting = 0;   // steps that ran no frame, during the window
};

inline double Rate(std::uint64_t updates, double seconds)
{
	return seconds > 0 ? static_cast<double>(updates) / seconds : 0.0;
}

inline double Rate(const Window& window) { return Rate(window.updates, window.seconds); }

inline bool IsSlow(const Window& window) { return window.updates > 0 && window.seconds > 0 && Rate(window) < kSlowRate; }

struct Summary {
	std::uint64_t windows = 0;
	std::uint64_t slow = 0; // windows under kSlowRate
	double slowest = 0;     // the lowest rate of any window (0 with none)
	double last = 0;        // the last window's rate
};

// Adds a window; true when it is slow.
inline bool Add(Summary& summary, const Window& window)
{
	if (window.updates == 0 || window.seconds <= 0) return false;
	const double rate = Rate(window);
	summary.slowest = summary.windows == 0 ? rate : std::min(summary.slowest, rate);
	summary.last = rate;
	summary.windows++;
	const bool slow = IsSlow(window);
	if (slow) summary.slow++;
	return slow;
}

inline std::string Format(double value)
{
	char text[32];
	snprintf(text, sizeof(text), "%.1f", value);
	return text;
}

// "pace 600 updates in 16.6 s (36.1 a second)": the end of the costs line.
inline std::string WindowText(const Window& window)
{
	return "pace " + std::to_string(window.updates) + " updates in " + Format(window.seconds) + " s (" + Format(Rate(window)) +
		" a second)";
}

// "pace: slow: 36.1 updates a second over the last 600 (rollbacks N, frames
// held waiting for the other player M)"
inline std::string SlowText(const Window& window)
{
	return "pace: slow: " + Format(Rate(window)) + " updates a second over the last " + std::to_string(window.updates) +
		" (rollbacks " + std::to_string(window.rollbacks) + ", frames held waiting for the other player " +
		std::to_string(window.waiting) + ")";
}

// "pace: battle: N windows, slowest X a second, K under 55 a second"
inline std::string SummaryText(const Summary& summary)
{
	return "pace: battle: " + std::to_string(summary.windows) + " windows, slowest " + Format(summary.slowest) + " a second, " +
		std::to_string(summary.slow) + " under " + Format(kSlowRate).substr(0, 2) + " a second";
}

struct PadEvent {
	bool added = false;
	int frame = -1; // the session's frame when the layer saw it; -1 not known
};

// "controller events: 2 (removed at frame 1200, added at frame 1560)" or "none".
inline std::string PadEventsText(const std::vector<PadEvent>& events, std::size_t total)
{
	if (total == 0) return "controller events: none";
	std::string text = "controller events: " + std::to_string(total) + " (";
	for (std::size_t i = 0; i < events.size() && i < kMostListedEvents; ++i) {
		if (i) text += ", ";
		text += std::string(events[i].added ? "added" : "removed") + " at frame " +
			(events[i].frame >= 0 ? std::to_string(events[i].frame) : std::string("?"));
	}
	if (total > std::min(events.size(), kMostListedEvents)) text += ", ...";
	return text + ")";
}

// The report's line: the battle so far and the last window before the mismatch.
inline std::string ReportText(const Summary& summary, const std::vector<PadEvent>& events, std::size_t totalEvents)
{
	if (summary.windows == 0) return "not measured yet (under 600 updates played); " + PadEventsText(events, totalEvents);
	return std::to_string(summary.windows) + " windows of 600 updates, slowest " + Format(summary.slowest) + " a second, " +
		std::to_string(summary.slow) + " under " + Format(kSlowRate).substr(0, 2) + " a second, the last window " +
		Format(summary.last) + " a second; " + PadEventsText(events, totalEvents);
}

// The live battle's record.
class Battle {
public:
	void Begin()
	{
		std::lock_guard lock(mutex_);
		summary_ = {};
		events_.clear();
		totalEvents_ = 0;
		live_ = true;
		frame_ = -1;
	}
	// The battle's summary as it ends (empty text when nothing was measured);
	// the record is forgotten.
	void End(Summary* summary, std::vector<PadEvent>* events, std::size_t* total)
	{
		std::lock_guard lock(mutex_);
		*summary = summary_;
		*events = events_;
		*total = totalEvents_;
		summary_ = {};
		events_.clear();
		totalEvents_ = 0;
		live_ = false;
		frame_ = -1;
	}
	bool AddWindow(const Window& window)
	{
		std::lock_guard lock(mutex_);
		return Add(summary_, window);
	}
	// The session's frame, from the game thread, once an update.
	void SetFrame(int frame) { frame_.store(frame, std::memory_order_relaxed); }
	// A controller removed or added (the pad layer's thread); counted only
	// while a battle is live.
	void NotePad(bool added)
	{
		std::lock_guard lock(mutex_);
		if (!live_) return;
		totalEvents_++;
		if (events_.size() < kMostKeptEvents) events_.push_back({added, frame_.load(std::memory_order_relaxed)});
	}
	std::string Report() const
	{
		std::lock_guard lock(mutex_);
		return ReportText(summary_, events_, totalEvents_);
	}

private:
	mutable std::mutex mutex_;
	Summary summary_;
	std::vector<PadEvent> events_;
	std::size_t totalEvents_ = 0;
	bool live_ = false;
	std::atomic<int> frame_{-1};
};

inline Battle& Current()
{
	static Battle battle;
	return battle;
}

} // namespace sfve::mod::pace
