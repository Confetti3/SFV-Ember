#pragma once

// Ember's frame meter over SFV's Training battle (EMBER_LIBRARY.md section
// 39): the library's training::FrameMeter, fed after every battle tick from
// SFV's own fighter state (mod::FighterReader, ember/SfvMeterSample), and
// drawn with the library's meter window (ui/FrameMeterWindow.hxx) as SF4
// Ember's Training HUD draws it: startup, active and recovery cells,
// hitstun and blockstun, each hit's advantage, and the numbers beside the
// bars. It follows the battle SfvTraining follows (ReadTrainingView), and
// only reads the game: no field is written, and no game function is called
// after the fighters are found (mod::FighterReader).

namespace sfve::adapter {

// Starts following battle ticks (mod::WatchBattleFrames). Once, from the
// netplay mode's Install, with rooms on.
void InstallSfvFrameMeter();

// Render thread, inside ImGui's frame: the meter as the followed Training
// battle's last tick left it, above SFV's gauges; nothing before one. The
// caller hides it where SfvTraining says the battle is not available, under
// the shell and under the game's pause menu (SfvHud).
void DrawSfvFrameMeter();

} // namespace sfve::adapter
