#pragma once

// What SfvAdapter logs of its battle's updates, for diagnosis only (nothing
// here changes the game): fighter units' command queues left between updates,
// the ticks per update with the match decided, and the battle frame's pause
// bits in a session's battle.

#include "mod/FighterUnits.hxx"

#include <stdint.h>

namespace sfve::mod {

class UpdateNotes {
public:
	// The battle ends: the command queues' count logged, and every note
	// starts afresh for the next battle.
	void Forget();
	// After an update: a fighter unit's command queue left not empty
	// (sfv::fighter::kCommandQueueBegin), outside the snapshot, is counted and
	// the first few logged with the update's input frame.
	void CommandQueues(const FighterUnit* units, int count, int inputFrame, bool replaying);
	// After a session's update: the input unit's ticks per update while the
	// match is decided, logged each time the count changes.
	void DecidedTicks(bool decided, uint32_t ticksInLastUpdate, int flowState);
	// After a session's update: the battle frame's pause bits (sfv::pause),
	// logged each time they change.
	void Pause(uint8_t* battleFrame, int flowState);

private:
	uint64_t queuesCarried_ = 0;
	int queueLines_ = 0;
	static constexpr int kMostQueueLines = 8;
	int64_t decidedTicks_ = -1; // the tick count last logged with the match decided; -1 none
	uint32_t pausedBits_ = 0;   // the battle frame's pause bits last logged
};

} // namespace sfve::mod
