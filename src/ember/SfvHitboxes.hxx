#pragma once

// Ember's hitbox viewer over SFV's Training battle
// (docs/design/beta8/hitboxes.md): both fighters' boxes and those of their
// projectiles, read after every tick of the Training battle the frame meter
// follows (mod::FighterReader, ember/SfvBoxes) and drawn with the library's
// overlay (ui/HitboxOverlay.hxx) as SF4 Ember's Training lab draws SF4's.
// It only reads the game, and reads nothing while it is off.
//
// The switch is Training select's Hitboxes row (Capabilities::hitboxes),
// kept as SF4's lab keeps its own: the "hitboxes" of training.json's "meter"
// (training/PracticeSettings.hxx), in SFV Ember's settings folder.

namespace sfve::mod {
class FighterReader;
}

namespace sfve::adapter {

// Game thread, after each update of the followed Training battle that ran a
// tick (SfvFrameMeter): reads the boxes and the camera while the viewer is
// on, and publishes them for the render thread.
void ReadSfvHitboxes(const mod::FighterReader& reader);
// Game thread: the followed battle is gone (or another one began); nothing is
// drawn until the next one's first tick.
void ForgetSfvHitboxes();

// Render thread, inside ImGui's frame: the boxes as the last tick left them,
// behind every window; nothing while the viewer is off or before a tick. The
// caller hides them where SfvTraining says the battle is not available and
// under the game's pause menu (SfvHud).
void DrawSfvHitboxes();

// The player's choice, read from training.json the first time it is asked.
// Any thread.
bool SfvHitboxesShown();
// Training select's row: shows or hides the boxes and saves the choice.
// False, the choice unchanged, when it could not be saved.
bool ShowSfvHitboxes(bool shown);

} // namespace sfve::adapter
