#pragma once

// Ember's frame meter over SFV's Training battle (EMBER_LIBRARY.md section
// 39): the library's training::FrameMeter, fed after every battle tick from
// SFV's own fighter state (mod::FighterReader, ember/SfvMeterSample), and
// drawn with the library's meter window (ui/FrameMeterWindow.hxx) as SF4
// Ember's Training HUD draws it: startup, active and recovery cells,
// hitstun and blockstun, each hit's advantage, and the numbers beside the
// bars. It reads the game and never writes it.

namespace sfve::adapter {

// Starts following battle ticks (mod::WatchBattleFrames). Once, from the
// netplay mode's Install, with rooms on.
void InstallSfvFrameMeter();

// Render thread, inside ImGui's frame: the meter over a Training battle that
// is running (not paused), above SFV's gauges. Nothing outside Training.
void DrawSfvFrameMeter();

} // namespace sfve::adapter
