#pragma once

// Tiny fighters (EMBER_LIBRARY.md section 59): in a room whose host allowed
// them, a player may pick Tiny for their fighter, and that fighter's 3D model
// is drawn small, like the old SFV tiny Ryu mods. ON SCREEN ONLY: hitboxes,
// positions and every piece of fight state stay as they were. Nothing here
// writes to a battle's state or to anything the rollback registers: it only
// scales the picture the game hands to its renderer.
//
// The sides drawn small are set when a room's or a replay's battle launches
// (VersusLaunch, from the picks' size) and cleared when the battle closes
// (RoomBattle::Close), when a battle that is neither binds, and when a launch
// fails, so Training, Home and the main menu are always normal.
//
// Two hooks, both after the game's own code:
//   - CharaVisualPart's per-frame update (sfv::kCharaVisualUpdate): the
//     fighter's transform has just been pushed to its Unreal actor; a tiny
//     side's mesh component gets the game's wanted scale times kTinyScale
//     (USceneComponent::SetWorldScale3D, sfv::kSetWorldScale3D).
//   - The actor's whole-mesh transform setter (sfv::kActorSetMeshTransform):
//     a cinematic (a Critical Art) may set the mesh's transform after the
//     per-frame update, so a tiny side's actor has its scale multiplied here
//     too.
//
// The fight reads the model too: a socket's world position
// (sfv::kSocketWorldPosition) places some projectiles and items at a bone,
// and a scaled mesh moves every socket toward the mesh's origin. So the fight
// never sees the small model: each battle update starts with the tiny sides'
// meshes back at the game's own scale (BeginFightUpdate), neither hook scales
// while it runs, and the update ends with them small again (EndFightUpdate),
// before anything is drawn (docs/design/beta8/desync.md).

#include <stdint.h>

#include <xmmintrin.h>

namespace sfve::mod {

// The size of a tiny fighter against a normal one (the reference clip shows
// about 40 % of the normal height).
constexpr float kTinyScale = 0.4f;

// The sides drawn small, by side (0 P1, 1 P2).
struct TinySides {
	bool side[2] = {false, false};
	bool Any() const { return side[0] || side[1]; }
};

// Sets which sides are drawn small, and logs them ("tiny: P2") when any is
// and `announce` (a launch announces once; the battle's bind sets them again
// without). Any thread.
void SetTinyFighters(bool p1, bool p2, bool announce = true);
// Nothing is drawn small. Any thread.
void ClearTinyFighters();
TinySides TinyFightersNow();

// Hooks the picture's functions (a set: all or none); false when they could
// not be hooked. Once, from SfveStart.
bool InstallTinyFighters(uintptr_t imageBase);

// Around one battle update, on the game thread (SfvAdapter): `parts` are both
// fighters' CharaVisualParts (null where there is none). Begin gives a tiny
// side's mesh the game's own scale and keeps both hooks from scaling until
// End, which makes it small again. Nothing happens when no side is tiny.
void BeginFightUpdate(uint8_t* const parts[2]);
void EndFightUpdate(uint8_t* const parts[2]);

// The parts of it that need no game: the tests reach them with memory laid
// out as the game's.
namespace tiny {

using SetScaleFn = void (*)(void* sceneComponent, const float* xyz);

// The scale a tiny fighter's mesh is given: each component of the game's
// wanted scale (a part's float3 at sfv::visual::kWantedScale), or 1 where it
// is 0, negative or not finite, times kTinyScale.
void ScaledScale(const float wanted[3], float out[3]);
// The scale the fight sees the same mesh at: the wanted scale, with 1 where
// it is 0, negative or not finite (ScaledScale without kTinyScale).
void NormalScale(const float wanted[3], float out[3]);

// What a CharaVisualPart update did for one part.
struct PartResult {
	int side = -1;         // 0 or 1; -1 where the part is no fighter's, or something was not readable
	void* actor = nullptr; // the part's actor, where the side is tiny and its mesh was scaled
	bool scaled = false;
};

// After the game's update of `part`: if it belongs to a fighter that is alive
// (a weak pointer with an object, a control block and a use count above 0),
// its actor and mesh component exist and its side is tiny in `sides`, the mesh
// is given ScaledScale of the part's wanted scale through `setScale`.
PartResult ApplyToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale);
// As ApplyToPart, with NormalScale: a tiny side's mesh at the game's own
// scale, for the fight's reads of the model.
PartResult RestoreToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale);

// The whole-mesh transform {rotation, translation, scale} with its scale's
// xyz times kTinyScale (ScaledScale of its xyz; its w is kept): `out` is a
// copy, `in` is not written.
void ScaledTransform(const __m128 in[3], __m128 out[3]);

} // namespace tiny

} // namespace sfve::mod
