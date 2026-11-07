#pragma once

// A battle's frames as a RollbackSession played them, for a replay
// (EMBER_LIBRARY.md section 26): what each frame gave the game, and whether
// the frame was held (the WIN's answers) or read the players' inputs.
//
// A session reports every frame it plays (Config::onFrame), a rollback's
// replay of a frame again, and a recorder keeps the last report of each
// frame: once the frame is confirmed that is the report of the confirmed
// inputs, the same on both fighters' copies and a watcher's. A replay plays
// the frames back through the session's watcher path (Config::replay), so
// the battle takes the same decisions on the same frames as it did live.

#include "core/GameAdapter.hxx"

#include <stdint.h>

#include <vector>

namespace sfve {

struct RecordedFrame {
	// An advanced frame: the inputs the game was given (InputsForThisFrame:
	// only the bits a pad makes, or neutral when the frame ignores them). A
	// held frame: both fighters' confirmed inputs, every bit, which carry
	// the WIN's answers (RematchDecision.hxx).
	FrameInputs inputs;
	uint8_t flags = 0;

	static constexpr uint8_t kHeld = 1u << 0;  // the game stood still (StepHeld)
	static constexpr uint8_t kFight = 1u << 1; // an advanced frame that read the inputs (InputsMatter)
	static constexpr uint8_t kKnown = kHeld | kFight;

	bool Held() const { return (flags & kHeld) != 0; }
	bool Fight() const { return (flags & kFight) != 0; }
	bool operator==(const RecordedFrame&) const = default;
};

using ReplayFrames = std::vector<RecordedFrame>;

} // namespace sfve
