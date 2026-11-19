#pragma once

// The way back to the main menu for a launch that gave up on its battle
// (VersusLaunch). SFV's battle jump has no way out of its own once its load
// has begun: its load step (sfv::jump's kJumpStateStep) waits for the battle
// flow's loader to say it is loaded and asks nothing else, so a load that
// never ends keeps the game in the jump, its screen black, until the player
// ends the game. When the launch's own limit has passed, the copy asks the
// scene manager for the main menu, as the jump's own exit does (GameScene's
// ChangeToMainMenu); the manager makes the change on its next tick, which
// ends the jump and its load. A battle that comes instead (the jump's change
// to it was asked for first) is not the launch's any more and is left the
// same way.
//
// This decides, a frame at a time, when to ask and when the way back is
// over; it reads no game memory, so tests drive it with the scenes alone.

namespace sfve::mod {

class MenuReturn {
public:
	enum class Step {
		Ask,    // ask the scene manager for the main menu now
		Wait,   // a scene change is pending: the manager makes it on its next tick
		Back,   // the main menu is the current scene: the way back is over
		GaveUp, // no main menu after kGiveUpFrames: the game stays where it is
	};

	// A change the manager was asked for is made on its next tick, so the
	// main menu normally comes the frame after the first ask; a game that has
	// not changed scene after about 10 s will not.
	static constexpr int kGiveUpFrames = 60 * 10;

	// One frame, before the manager's tick: whether the main menu is the
	// current scene, and whether a change of scene is pending.
	Step Observe(bool atMainMenu, bool changePending)
	{
		if (atMainMenu) return Step::Back;
		if (++frames_ > kGiveUpFrames) return Step::GaveUp;
		return changePending ? Step::Wait : Step::Ask;
	}

	// The frames observed away from the main menu so far.
	int Frames() const { return frames_; }

private:
	int frames_ = 0;
};

} // namespace sfve::mod
