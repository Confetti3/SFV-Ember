#pragma once

// Time sync by frame stretching, as SF4 Ember paces (sf4e PacingController,
// continuous rift mode).
//
// Every rendered frame of a session the pacer samples GGPO's rift (how far
// this side runs ahead of the other) and, while this side is ahead, makes the
// frame a little longer by waiting on the game thread before the battle
// update: at most PacingController::maxStepMs a frame, so play slows by a few
// percent instead of freezing for a whole frame. UE4's limiter waits until one
// period after the previous frame; the pacer waits past that, so the frame is
// really longer, and the limiter does not catch a long frame up. The
// side that is behind cannot shorten its frames (the engine's limiter owns
// that wait); the side ahead closes the whole gap instead.
//
// Simulation is never skipped or doubled, and nothing the game simulates
// reads this wall-clock bookkeeping.

#include "core/RollbackSession.hxx"

#include "sf4e__PacingController.hxx"

#include <string>

namespace sfve::mod {

class FramePacer {
public:
	FramePacer();
	~FramePacer();
	FramePacer(const FramePacer&) = delete;
	FramePacer& operator=(const FramePacer&) = delete;

	void Reset();
	// Once per rendered frame of a running session, before the battle update.
	// `stalled`: the last step hit GGPO's prediction barrier.
	void Frame(const RollbackSession& session, bool stalled);

	std::string Describe() const; // JSON for the status report

private:
	void WaitUntil(double deadlineMs);

	sf4e::pacing::PacingController controller_{};
	void* timer_ = nullptr; // high-resolution waitable timer
	int pingMs_ = -1;
	double lastFrameMs_ = 0.0; // when the previous paced frame reached the pacer
};

} // namespace sfve::mod
