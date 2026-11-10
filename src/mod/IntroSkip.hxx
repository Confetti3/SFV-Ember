#pragma once

// Skipping the fighters' intros in a linked battle, on both copies on the
// same frame (EMBER_LIBRARY.md section 50).
//
// Offline, the intro (flow state IntroDemo) ends early when the battle's
// input for a tick carries the game's skip bit (sfv::intro, SfvBattleLayouts).
// Linked, a frame outside the fight gets neutral inputs (the game ignores
// them, so no rollback starts there), so no press ever reached the intro.
//
// Now the skip bit rides in the exchanged input: each copy sends it from its
// player's first new press in the intro (the skip button, or any attack, as
// SF4 Ember's intros take any button) to the intro's end. The game reads it
// only on skip frames, one session frame in kEvery while the flow says
// IntroDemo, and those frames are stepped only once their inputs are
// confirmed (GameAdapter::ConfirmedBits). Both fighters, the watchers and a
// replay's playback then give the game the same bits on the same frame, so
// the intro ends on the same tick everywhere, whichever player pressed. The
// cost: at most one frame in kEvery waits for the other copy's input, and a
// skip comes within kEvery frames of the press reaching both copies.

#include "core/GameAdapter.hxx"
#include "mod/GameInput.hxx"
#include "mod/SfvBattleLayouts.hxx"

namespace sfve::mod::intro_skip {

// The game's own skip bit: what the intro reads, and what is sent.
constexpr PackedInput kSkipBit = sfv::intro::kSkipBit;
static_assert((kSkipBit & input::kInputBits) == 0, "the skip bit is not one a fight frame reads");

// A press asks for the skip: the game's skip button or any attack.
constexpr PackedInput kPressBits =
	kSkipBit | input::kLP | input::kMP | input::kLK | input::kMK | input::kHP | input::kHK;

// One session frame in this many reads the skip bit while in the intro.
constexpr int kEvery = 15;

// Session frame `frame`, starting from flow state `flowState` (the one the
// flow last announced), reads the skip bit.
constexpr bool SkipFrame(int flowState, int frame)
{
	return flowState == sfv::flow::kIntroDemo && frame >= 0 && frame % kEvery == 0;
}

// The bits that frame reads confirmed (GameAdapter::ConfirmedBits).
constexpr PackedInput ConfirmedBits(int flowState, int frame)
{
	return SkipFrame(flowState, frame) ? kSkipBit : 0;
}

// This copy's skip request: what it sends for each frame it steps.
class Request {
public:
	// `local` (the player's input for the next frame, the game's value of
	// the pad or a test's script) with the skip bit set from the first new
	// press in the intro to the intro's end, and clear otherwise. A button
	// already down as the intro starts is not a press until it is released.
	PackedInput Next(int flowState, PackedInput local)
	{
		const PackedInput down = local & kPressBits;
		requested_ = flowState == sfv::flow::kIntroDemo && (requested_ || (down & ~down_) != 0);
		down_ = down;
		return (local & ~kSkipBit) | (requested_ ? kSkipBit : 0);
	}
	bool Requested() const { return requested_; }

private:
	bool requested_ = false;
	PackedInput down_ = kPressBits; // as if all down: nothing held before the first call is a press
};

} // namespace sfve::mod::intro_skip
