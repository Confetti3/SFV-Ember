#pragma once

// Ember's hitbox viewer over SFV's Training battle
// (docs/design/beta8/hitboxes.md): both fighters' boxes and those of their
// projectiles, read after every tick of the Training battle the frame meter
// follows (mod::FighterReader, ember/SfvBoxes) and drawn with the library's
// overlay (ui/HitboxOverlay.hxx) as SF4 Ember's Training lab draws SF4's.
// It only reads the game, and reads nothing while it is off.
//
// The switch is the lab's Hitboxes page and Training select's Hitboxes row
// (Capabilities::hitboxes), both the library's one owner of training.json
// (ui::TrainingMeterOptions, SetTrainingMeterOptions): its "meter"'s
// "hitboxes", in SFV Ember's settings folder.

#include <array>

namespace sfve::mod {
class FighterReader;
struct FighterFrame;
}

namespace sfve::adapter {

// Game thread, after each update of the followed Training battle that ran a
// tick (SfvFrameMeter): reads the boxes and the camera while the viewer is
// on, and publishes them for the render thread. fighters: what the meter just
// read of both, for the log's naming of the scripts a kind of box was met in.
void ReadSfvHitboxes(const mod::FighterReader& reader, const std::array<mod::FighterFrame, 2>& fighters);
// Game thread: the followed battle is gone (or another one began); nothing is
// drawn until the next one's first tick.
void ForgetSfvHitboxes();

// Render thread, inside ImGui's frame: the boxes as the last tick left them,
// behind every window; nothing while the viewer is off or before a tick. The
// caller hides them where SfvTraining says the battle is not available and
// under the game's pause menu (SfvHud).
void DrawSfvHitboxes();

// Whether the boxes are read and drawn: the lab's Hitboxes switch with its
// HUD shown, as SfvTrainingHost hands it over each frame. Any thread.
void SetSfvHitboxesShown(bool shown);
bool SfvHitboxesShown();

} // namespace sfve::adapter
