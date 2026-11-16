#pragma once

// Ember's Training lab over SFV's Training battle (docs/design/beta10/
// training.md): dummy recording and playback and the dummy's reply, the
// library's model (training/TrainingSession.hxx, DummyReply.hxx, LabCore.hxx)
// on SFV's battle. The lab's controls and HUD are the library's too, drawn by
// SfvTrainingHost.
//
// Where the game is touched, all of it only in the Training battle SfvTraining
// follows (TrainingView: its scene, never a room's or a replay's battle,
// which SFV Ember plays through a session):
//   - each tick, the two slots' input as the battle records it
//     (mod::SetTrainingPadsHook, inside InputRecord): on standby and while
//     recording P1's pad plays the dummy and P1 stands; a playback or a reply
//     puts the slot's frames in the dummy's slot, turned for its facing.
//     Otherwise nothing is written and the game's own dummy plays;
//   - while the lab supplies the dummy's input, the dummy's control in the
//     match data is Training's own "Player 2's controller" (sfv::dummy:
//     kind 0, its own slot), from the first tick the lab supplies to the one
//     after the last it supplied, then put back as it was
//     (training::DummyControl). A dummy action chosen in SFV's pause menu
//     meanwhile stands, and the lab stops.
// It reads the fighters through the frame meter's follower (SfvFrameMeter:
// one battle follower for the meter, the hitboxes and the lab).

#include "mod/FighterReader.hxx"

#include "training/FrameMeter.hxx"
#include "training/TrainingSession.hxx"

#include <stdint.h>

#include <array>

namespace sfve::mod {
class SfvAdapter;
}

namespace sfve::adapter {

// Hooks the battle's input record, and the pipe's "traininglab" status.
// Once, from the netplay mode's Install, with rooms on.
void InstallSfvTrainingLab();

// The lab as the controls and the HUD read it, and what they send it. Any thread.
sf4e::training::View ReadTrainingLab();
bool SubmitTrainingLab(const sf4e::training::Command& command);

// For the tests (the pipe's "traininglab setting"): one of Training's own
// settings other than the dummy's action (sfv::dummy::kTrainingSettings,
// 1..13), set at the next update as SFV's pause menu sets it, within the
// range its table gives. Any thread.
void RequestTrainingSetting(int index, int value);
// For the tests (the pipe's "traininglab leave"): the battle leaves for the
// main menu at the next update, as SFV's pause menu's Go to Main Menu.
void RequestTrainingLeave();

// Render thread, each frame: Ember's lab controls are open, which hold the
// pad (standby and recording wait; a playback goes on).
void SetTrainingLabControlsOpen(bool open);

// Game thread, from the battle follower (SfvFrameMeter) after each update of
// the followed Training battle: each slot's input as the battle recorded it
// in this update's last tick (the lab's words, SfvLabInput), for the meter's
// input lanes.
std::array<unsigned, 2> TrainingLabInputs();
// Then, after the meter took the update: the fighters as the meter's samples
// and as read, the ticks the update ran and the meter's view, which the lab
// publishes with its own.
void TrainingLabAfterUpdate(mod::SfvAdapter& adapter, const std::array<sf4e::training::FighterSample, 2>& samples,
	const std::array<mod::FighterFrame, 2>& fighters, uint32_t ticks, const sf4e::training::MeterView& meter);

} // namespace sfve::adapter
