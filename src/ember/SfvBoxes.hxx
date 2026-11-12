#pragma once

// SFV's boxes in the hitbox viewer's terms (docs/design/beta8/hitboxes.md):
// what mod::FighterReader read after a tick (mod::BoxFrame: each action
// part's attack and body blocks, and the camera) as the library's
// training::BoxView, which ui/HitboxOverlay draws as SF4 Ember's Training lab
// draws SF4's. Pure: no game memory, so the tests run it on records and
// cameras recorded in game.
//
// The view's vocabulary is the library's (training/HitBoxes.hxx): a box is
// four corners on the fight's plane and a kind, and the camera is a view and
// a projection matrix, rows first, a point multiplied from the left. SFV gives
// each directly:
//   - a box is a listed record's world rectangle (left, bottom, width,
//     height), which the game places every tick from the fighter's position
//     and facing (sfv::kPlaceAttackBoxes, kPlaceBodyBoxes);
//   - the kind is the block and the record's type (AttackBoxKind);
//   - the camera is the engine's eye, the point it looks at and its
//     horizontal field of view at the game's 16:9 (sfv::kAimCamera), with the
//     fight's frame in the world and the engine's units folded into the view.

#include "mod/FighterReader.hxx"

#include "training/HitBoxes.hxx"

#include <stdint.h>

#include <optional>
#include <vector>

namespace sfve::adapter {

// What an attack record of a type is drawn as: strikes, projectiles and type
// 2 as hits, the throw as a throw, the proximity box as one; none for the
// types that are no attack's reach (3, never found tested, and 5, a catch).
std::optional<sf4e::training::BoxKind> AttackBoxKind(int type);

// What the conversion met, for the log: the boxes made, listed records of a
// type not drawn (and those types, one bit each), list entries that point at
// no record of their block, records whose shape is not the rectangle, and
// rectangles with no area or a number that is not one.
struct BoxTally {
	int drawn = 0, undrawn = 0, stray = 0, otherShapes = 0, empty = 0;
	unsigned undrawnTypes = 0;
};

// The boxes a part's blocks list this tick, appended to `out` in the game's
// order (the attack lists by type, then the body lists by group), each record
// once and owned by the part's side. rectShape is the rectangle's vtable in
// the process the blocks were read from.
void AppendBoxes(const mod::BoxBlocks& part, uintptr_t rectShape, std::vector<sf4e::training::Box>& out, BoxTally& tally);

// The camera as the view's two matrices: view takes a point of the fight's
// plane through the fight's frame into the engine's units and then into the
// camera's (right, up, ahead); projection is the field of view at the
// game's 16:9. False, the matrices untouched, for a camera that cannot be
// one (not read, no distance to its target, looking straight up or down, a
// field of view outside 0..180 degrees, or a number that is not one).
bool CameraMatrices(const mod::CameraFrame& camera, float view[16], float projection[16]);

// The whole frame as the library's view: valid with a camera, every part's
// boxes either way.
sf4e::training::BoxView SfvBoxView(const mod::BoxFrame& frame, BoxTally& tally);

} // namespace sfve::adapter
