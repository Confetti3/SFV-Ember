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
//
// The scenes after a decided match (the last KO's finish, the winner's pose)
// take the same press, and need the same care for another reason: once a
// side has the wins the match needs, the game asks the pads themselves for
// the skip bit (sfv::kPadsAny), so each copy heard only its own player and a
// press moved one copy's flow alone: the copies' flow, mode and rule state
// then differed at the next checkpoint with both fighters equal. While a
// session's inputs are injected that question is answered from the battle's
// input (GameHooks), and the frames outside the fight of a decided match
// are skip frames like the intro's, with the request sent the same way.

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

// A press skips what flow state `flowState` (the one the flow last
// announced) shows: the intro, or with the match decided
// (sfv::battledata::kDecided) anything but the fight. Both are part of the
// rollback state, so every copy answers alike at a frame's start.
constexpr bool Skippable(int flowState, bool decided)
{
	return flowState == sfv::flow::kIntroDemo || (decided && flowState != sfv::flow::kFight);
}

// Session frame `frame`, starting from that state, reads the skip bit.
constexpr bool SkipFrame(int flowState, int frame, bool decided = false)
{
	return Skippable(flowState, decided) && frame >= 0 && frame % kEvery == 0;
}

// The bits that frame reads confirmed (GameAdapter::ConfirmedBits).
constexpr PackedInput ConfirmedBits(int flowState, int frame, bool decided = false)
{
	return SkipFrame(flowState, frame, decided) ? kSkipBit : 0;
}

// This copy's skip request: what it sends for each frame it steps.
class Request {
public:
	// `local` (the player's input for the next frame, the game's value of
	// the pad or a test's script) with the skip bit set from the first new
	// press in the intro (or in a decided match's scenes) to its end, and
	// clear otherwise. A button already down as it starts (the one that
	// landed the last hit, say) is not a press until it is released.
	PackedInput Next(int flowState, PackedInput local, bool decided = false)
	{
		const PackedInput down = local & kPressBits;
		requested_ = Skippable(flowState, decided) && (requested_ || (down & ~down_) != 0);
		down_ = down;
		return (local & ~kSkipBit) | (requested_ ? kSkipBit : 0);
	}
	bool Requested() const { return requested_; }

private:
	bool requested_ = false;
	PackedInput down_ = kPressBits; // as if all down: nothing held before the first call is a press
};

} // namespace sfve::mod::intro_skip
