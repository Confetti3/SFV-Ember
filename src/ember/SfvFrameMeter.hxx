#pragma once

// Ember's frame meter over SFV's Training battle (EMBER_LIBRARY.md section
// 39): the library's training::FrameMeter, fed after every battle tick from
// SFV's own fighter state (mod::FighterReader, ember/SfvMeterSample), and
// drawn by the library's Training HUD (SfvTrainingHost) with the player's
// options: startup, active and recovery cells, hitstun and blockstun, each
// hit's advantage, and the numbers beside the bars. It follows the battle
// SfvTraining follows (ReadTrainingView) and is that battle's one follower:
// after each update it reads the fighters once and hands them to the meter,
// the hitbox viewer and the Training lab (SfvTrainingLab). It only reads the
// game: no game function is called after the fighters are found
// (mod::FighterReader).

#include "training/FrameMeter.hxx"

namespace sfve::adapter {

// Starts following battle ticks (mod::WatchBattleFrames). Once, from the
// netplay mode's Install, with rooms on.
void InstallSfvFrameMeter();

// The options of the meter's own measuring, as the lab's Meter settings page
// chose them: how many frames it keeps, whether an exchange after a pause goes
// on after the last, and whether the guard pose before a hit is a cell of its
// own (MeterOptions::history, continuous, proximityGuard). The HUD draws the
// meter as the lab publishes it (SfvTrainingLab, SfvTrainingHost). Any thread.
void SetSfvMeterOptions(const sf4e::training::MeterOptions& options);

} // namespace sfve::adapter
