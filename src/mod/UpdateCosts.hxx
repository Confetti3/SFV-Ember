#pragma once

// The costs of a battle's updates and the pace of its frames (mod/Pace.hxx),
// for the log only: SfvAdapter reports each update it runs and each restore's
// callbacks, and every kCostAdvances updates played forward a cost line gives
// each kind of update's time, the layout's saving, loading and hashing
// (StateLayout::TakeCosts) and the pace window the session's counts make.

#include "core/StateRegions.hxx"
#include "mod/Pace.hxx"

#include <stdint.h>

#include <functional>
#include <utility>

namespace sfve::mod {

class UpdateCosts {
public:
	// What the pace line needs of the session (logging only): its frame, its
	// rollbacks and the steps that ran no frame, read on the battle's thread
	// at each update. The netplay mode sets it for each session; null where
	// the battle has none.
	struct PaceCounts {
		int frame = -1;
		uint64_t rollbacks = 0;
		uint64_t waiting = 0;
	};
	// QueryPerformanceCounter's ticks: what every time here is measured in.
	static int64_t Now();

	void SetPaceSource(std::function<PaceCounts()> source) { paceSource_ = std::move(source); }
	// An update is due in the frame now running (SfvAdapter::NoteUpdateDue).
	void Due() { paceClock_.Due(); }
	// The battle's last rendered frame has ended (a new one is seen).
	// A frame that took longer than kHitchMs is logged (audio dropouts: a game
	// thread that stands still that long starves the sound the game starts per tick).
	void FrameEnded();
	// The battle ends: its pace and controller events logged, the next
	// battle's record begun, and every count back to none.
	void EndBattle();
	// Before an update runs: the cost line's first window begins with the
	// first update played forward.
	void BeforeUpdate(bool resimulating);
	// An update ran in `took` ticks (replayed, or played forward: then due,
	// and a cost line every kCostAdvances, with `stateLayout`'s costs).
	void Played(int64_t took, bool resimulating, StateLayout& stateLayout);
	// A restore's callbacks ran in `took` ticks.
	void Callbacks(int64_t took)
	{
		costs_.callbacks++;
		costs_.callbackTicks += took;
	}

private:
	// The cost line: each kind of update's time, and saving, loading, hashing
	// and the restore's callbacks since the last line.
	void LogCosts(StateLayout& stateLayout);

	// The costs since the last cost line, in QueryPerformanceCounter ticks:
	// updates played forward and replayed, and AfterLoad's work.
	struct Counts {
		uint64_t ticks = 0, replayed = 0, callbacks = 0;
		int64_t tickTicks = 0, replayedTicks = 0, callbackTicks = 0;
	};
	Counts costs_;
	// The pace of the updates played forward: the battle's frames' time
	// (split by whether an update was due), whether the window the next
	// costs line covers has begun, and the clock's and the session's counts then.
	std::function<PaceCounts()> paceSource_;
	pace::FrameClock paceClock_;
	bool paceBegun_ = false;
	pace::FrameClock::Totals clockAtStart_;
	PaceCounts paceAtStart_;
};

} // namespace sfve::mod
