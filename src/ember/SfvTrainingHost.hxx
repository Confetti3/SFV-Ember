#pragma once

// Ember's Training lab over SFV's Training battle, on the overlay's side: the
// library's lab controls (F6, or the pad's View held with Start, as SF4
// Ember's), its HUD (the frame meter as the player set it, the recording
// strip, the input displays, the strip of keys; F5 hides it), F7 Record and
// F8 Play, and the hitboxes' switch, all over SfvTrainingLab
// (docs/design/beta10/training.md). The pages are the library's, offered by
// SFV's Capabilities::trainingFeatures: recording, input history, frame data,
// HUD and hitboxes, the dummy's reply. SFV's own pause menu keeps the game's
// dummy settings, its own recording and its position reset.

namespace sfve::adapter {

// The lab's settings folder and what SFV's lab offers, and the pipe's
// "traininglab" command. Once, from the shell's install.
void InstallSfvTrainingHost();

// Render thread, each frame inside ImGui's: the lab's keys, controls and HUD
// over the Training battle (available), under neither Ember's menu nor the
// game's pause menu; outside the battle the controls close.
void DrawSfvTrainingLab(bool available, bool shellShown, bool paused, bool focused, bool called);

// Whether the lab's controls are open, which hold the pad and the pointer as
// Ember's menu does. Any thread.
bool TrainingControlsOpen();

// Game thread, each read of the gameplay pad in Training: the chord that
// opens the controls (View held, then Start), or on standby starts the
// recording. True when the game must not see this read (the chord's Start,
// which would open its pause menu).
bool TrainingTakesPad(unsigned short xinputButtons, bool focused);

// The window procedure's WM_ACTIVATEAPP: losing the focus closes the controls.
void TrainingFocus(bool focused);

// Training select's Hitboxes row: the lab's Hitboxes switch (the library's
// training.json), read and saved. Render thread.
bool TrainingHitboxesChosen();
bool ChooseTrainingHitboxes(bool shown);

} // namespace sfve::adapter
