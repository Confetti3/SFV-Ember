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

// The sides drawn small, by side (0 P1, 1 P2), and each one's size against
// normal: kTinyScale for a Tiny pick, the photo booth's factor for a side it
// scaled (ApplyFighterScaleNow).
struct TinySides {
	bool side[2] = {false, false};
	float factor[2] = {kTinyScale, kTinyScale};
	bool Any() const { return side[0] || side[1]; }
};

// Sets which sides are drawn small, and logs them ("tiny: P2") when any is
// and `announce` (a launch announces once; the battle's bind sets them again
// without). Any thread.
void SetTinyFighters(bool p1, bool p2, bool announce = true);
// Nothing is drawn small for a Tiny pick (the booth's scale stays). Any
// thread.
void ClearTinyFighters();
// The sides drawn small: a Tiny pick's (SetTinyFighters) at kTinyScale, and
// a side the booth scaled at its factor, which wins over a Tiny pick's.
TinySides TinyFightersNow();

// The photo booth's scale (development builds, -sfve_mode=booth;
// docs/design/beta8/booth-protocol.md `booth scale`): sets each side's fighter
// mesh to `p1` and `p2` times its own size at once, on the visual part each
// side's fighter last updated, instead of waiting for the next per-frame
// update (a frozen booth frame has none). 1 is normal; the booth uses 0.001 to
// take a fighter out of a plate. Game thread only. True when both sides'
// parts were known and scaled; false (nothing changed for a side whose part is
// unknown) otherwise, and always false for a factor that is not positive and
// finite. It writes only the picture, as every function here does. The parts
// are those the per-frame hook last updated for each side in this battle
// (remembered only while the booth allows it); the factor stays with the side
// (TinySides::factor), so the per-frame hook and a battle update keep it until
// ForgetFighterScale. Also false outside the booth (SetFighterScaleAllowed
// never called with true, which only BoothMode does) and off the game thread.
bool ApplyFighterScaleNow(float p1, float p2);

// The photo booth's plate (`booth hide`): each side's fighter actor hidden
// (true) or shown (false) at once, the whole actor as UE4's
// SetActorHiddenInGame hides it (sfv::visual::kActorSetHiddenInGame): every
// mesh, the cloth and the shadow go, where a scale leaves the cloth behind.
// Same rules as ApplyFighterScaleNow: the booth only, the game thread, both
// sides' parts known; false otherwise or when an actor cannot be read.
bool SetFightersHiddenNow(bool p1, bool p2);

// The booth's install (BoothMode::Install, development builds): the
// per-frame hook remembers each side's part, and ApplyFighterScaleNow works.
// Off (the default, every other mode): neither. Any thread.
void SetFighterScaleAllowed(bool on);
// A new battle or a battle left (BoothMode): no booth scale on either side,
// and no remembered part (the last battle's parts are gone). Game thread.
void ForgetFighterScale();

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
// is 0, negative or not finite, times `factor` (a Tiny pick's kTinyScale, or
// the booth's).
void ScaledScale(const float wanted[3], float out[3], float factor = kTinyScale);
// A size factor the booth may give: positive and finite.
bool UsableFactor(float factor);
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
// is given ScaledScale of the part's wanted scale, with that side's
// `sides.factor`, through `setScale`.
PartResult ApplyToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale);
// As ApplyToPart, with NormalScale: a tiny side's mesh at the game's own
// scale, for the fight's reads of the model.
PartResult RestoreToPart(const uint8_t* part, const TinySides& sides, SetScaleFn setScale);

// The whole-mesh transform {rotation, translation, scale} with its scale's
// xyz times `factor` (ScaledScale of its xyz; its w is kept): `out` is a
// copy, `in` is not written.
void ScaledTransform(const __m128 in[3], __m128 out[3], float factor = kTinyScale);

// The booth's scale on both sides' parts (ApplyFighterScaleNow without its
// checks): each part in `parts` that is a live fighter's with an actor and a
// mesh gets ScaledScale of its wanted scale times that side's factor, and
// `results` say what was done (side -1 where the part is null or unreadable).
// True when both parts were scaled, each on its own side. Nothing is called
// for a factor that is not UsableFactor.
bool ScaleBothSides(const uint8_t* const parts[2], const float factors[2], SetScaleFn setScale, PartResult results[2]);

} // namespace tiny

} // namespace sfve::mod
