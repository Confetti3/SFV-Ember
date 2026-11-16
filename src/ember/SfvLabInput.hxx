#pragma once

// SFV's Training battle in the Training lab's terms (the library's
// training/TrainingSession.hxx and DummyReply.hxx), for SfvTrainingLab. Pure:
// no game memory, so the tests run it on frames recorded in game.
//
// - Input. What SFV's battle records for a slot each tick (mod/GameInput:
//   directions in bits 0-3, LP 0x10, MP 0x20, LK 0x40, MK 0x80, HP 0x200,
//   HK 0x800) is the battle's own input, after the player's button settings:
//   the value rooms send between two copies, whose players may have set their
//   buttons differently, and which plays the same moves on both. The lab
//   keeps a recording in its own fight-button word (training::FightButtons:
//   HP at 0x400, the rest where SFV has them), so a recording is the moves
//   pressed, not the pad's buttons, and survives any change of the button
//   settings. Left and Right are the screen's, as the lab's are: a slot is
//   held for a fighter facing right and turned for one facing left.
// - The dummy's free frame. SFV counts it itself (CharaUnit +0x91E, the
//   frame meter's untilActionable): after a tick, the ticks until the
//   fighter's state is its own again, hitstop included, and kUntimed lying
//   down. Training's own reversal recording times its replies on the same
//   count (DummyControlUnit, rva 0x3DE3F0). The stun watch takes it as the
//   frames to the free frame.

#include "mod/FighterReader.hxx"

#include "training/FrameMeter.hxx"

#include <stdint.h>

namespace sfve::adapter::lab {

// SFV's recorded input and the lab's word: HP's bit is SFV's 0x200 and the
// lab's 0x400, the two swapped and every other bit kept, so no bit is lost
// either way.
constexpr uint32_t kSfvHp = 0x200, kLabHp = 0x400;
constexpr uint32_t ToLab(uint32_t sfv)
{
	return (sfv & ~(kSfvHp | kLabHp)) | ((sfv & kSfvHp) ? kLabHp : 0) | ((sfv & kLabHp) ? kSfvHp : 0);
}
constexpr uint32_t ToSfv(uint32_t lab)
{
	return ToLab(lab); // the same swap
}

// The frames from this tick to the dummy's free frame by the game's own
// count, -1 where it does not tell (lying down, or not held: the stun watch
// reads it only while the dummy is held).
int ScriptedFrames(const mod::FighterFrame& dummy);

// A side's control in the match data (sfv::dummy): its kind and its pad.
struct SideControl {
	int32_t kind = -1, pad = -1;
	bool operator==(const SideControl& other) const { return kind == other.kind && pad == other.pad; }
	bool operator!=(const SideControl& other) const { return !(*this == other); }
};
// What Training's "Player 2's controller" gives the dummy (rva 0x3CAB90's 4):
// its own slot, which the lab fills.
constexpr SideControl kLabControl{0, 1};

} // namespace sfve::adapter::lab
