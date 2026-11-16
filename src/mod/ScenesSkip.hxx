#pragma once

// Ending the decided match's scenes (and those between two rounds) on a skip
// frame: what the flow's current state would do on the skip bit, done for it
// from the state at the frame's start (IntroSkip.hxx, docs/design/beta9/
// skip.md). SfvAdapter::Advance applies it before the frame's update; the
// netplay mode logs what it did with the frame (Take).

#include "mod/FighterUnits.hxx"

#include <stdint.h>

#include <utility>

namespace sfve::mod {

class ScenesSkip {
public:
	// The game's functions, from the image base (at each bind).
	void Bind(uintptr_t base);
	// A new battle: nothing to take, every idle state logged again.
	void Forget();
	// The frame carries the skip bit with the match decided: the flow state
	// current in `flow` (the flow unit) moved on (intro_skip::ScenesStepFor),
	// with the battle frame's state word and the fighters' units as the
	// game's own step would use them.
	void Apply(uint8_t* flow, uint8_t* battleFrame, const FighterUnit* units, int count);
	// What the last Apply did (a line for the log), taken once; null when it
	// did nothing.
	const char* Take() { return std::exchange(scenesSkip_, nullptr); }

private:
	uintptr_t imageBase_ = 0;
	int64_t (*flowRequest_)(uint8_t* flowUnit, int32_t state, uint8_t flags, int32_t arg) = nullptr; // sfv::kFlowRequest
	int64_t (*setFlowPhase_)(uint8_t* flowState, int32_t phase) = nullptr;                          // sfv::kSetFlowPhase
	void (*markResultSkipped_)(uint8_t* unitContext) = nullptr;                                      // sfv::kMarkResultSkipped
	const char* scenesSkip_ = nullptr; // Take: points at scenesSkipText_
	char scenesSkipText_[192] = {};
	// What Apply last left alone (state id, phase), logged once each.
	int scenesIdleState_ = -1;
	int scenesIdlePhase_ = -1;
};

} // namespace sfve::mod
