#pragma once

// What a fighter is doing, as SFV's battle holds it: the offsets Ember's
// frame meter reads (EMBER_LIBRARY.md section 39). Part of SfvOffsets.hxx,
// which includes this file and verifies every site below with the rest of the
// build table (kSites), so the meter reads nothing on another build. The meter
// only reads these fields; nothing here is written.
//
// Same build as SfvOffsets.hxx (Steam 13173604, PE timestamp 0x648AD3A4),
// derived from our IDA database and checked against the live game in Training
// (RYU against RYU, section 39's table).

#include "mod/BuildCheck.hxx"

#include <stdint.h>

namespace sfve::mod::sfv {

// void UpdateUntilActionable(CharaUnit*): each tick, the fighter's frames until
// it can act, as 16.16 at +0x91C: the hitstop left ((+0x108 - +0x104) >> 16),
// plus the frames its script still plays up to its interrupt frame (rva
// 0x2F21F0 over the action part's script player: frame +0x4C, interrupt
// +0x60, end +0x54, speed +0x68), or the landing time while airborne, or 1000
// when the game cannot time it (lying down, posture 3 and 4).
constexpr CodeSite kUpdateUntilActionable{"UpdateUntilActionable", 0x386010,
	{0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x8B, 0x81, 0x08}};
// int FrameAdvantage(CharaUnit*, bool cached): Training's own advantage (its
// Attack Data's "%d (%+d)", rva 0x449510): the opponent's frames until
// actionable (the CharaUnit its +0x764 names) minus the fighter's own, while the
// fighter's are 1..999 and the opponent's under 1000; else 0. With `cached`
// the value rva 0x386710 keeps at +0xE30.
constexpr CodeSite kFrameAdvantage{"FrameAdvantage", 0x372E60,
	{0x48, 0x83, 0xEC, 0x48, 0x85, 0xD2, 0x74, 0x0B, 0x8B, 0x81, 0x30, 0x0E, 0x00, 0x00, 0x48, 0x83}};
// void ChangeState(StateMachine*, int kind, int param): the fighter's state
// machine (CharaUnit +0xC58) takes the state singleton kStateTable[kind] as its
// current state (+0x10), keeps the last one (+0x18) and the param (+0x20).
constexpr CodeSite kChangeState{"ChangeState", 0x35D000,
	{0x40, 0x55, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xD1, 0x48}};
// The static initialiser that fills the state table: 29 singletons, each a
// vtable whose +0x20 returns its kind, 1 to 29, in table order.
constexpr CodeSite kStateTableInit{"StateTableInit", 0x13E4F0,
	{0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0x26, 0x7F, 0x92, 0x03, 0xA8, 0x01, 0x75, 0x23, 0x83, 0xC8}};
// void ApplyHitStop(Unit*, CharaUnit* struck, HitData*): a hit's hitstop on the
// fighter, 16.16: elapsed (+0x104) to 0, total (+0x108) from the hit's +32.
constexpr CodeSite kApplyHitStop{"ApplyHitStop", 0x2FEA00,
	{0x40, 0x53, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x74, 0x24, 0x60, 0x48, 0x8B, 0xDA}};
// int SetScript(ScriptPlayer*, int script, bool flag): plays a BAC script from
// its start: its header at +0x40 and number at +0x48, the frame at +0x4C to 0,
// and the header's boundaries as 16.16 (+0x58 first attack frame, +0x5C last,
// +0x60 interrupt, +0x54 end; a header value of -1, or one past the end,
// becomes 0 or the end).
constexpr CodeSite kSetScript{"SetScript", 0x307840,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x33}};
// int SetScriptFrame(ScriptPlayer*, int script, int frame16): a frame of the
// script: +0x4C, and the phase bits at +0x6C from the boundaries (0x20 before
// the first attack frame, 0x40 up to the last, 0x80 after, 0x100 from the
// interrupt frame on).
constexpr CodeSite kSetScriptFrame{"SetScriptFrame", 0x3099E0,
	{0x44, 0x89, 0x44, 0x24, 0x18, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xD9}};
// int ScriptPosture(CharaActionPart*): the posture of the script the part plays
// (header +0x2E).
constexpr CodeSite kScriptPosture{"ScriptPosture", 0x32B120,
	{0x40, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x81, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0}};
// bool KoOrThrown(CharaUnit*): one of the state changes that names the action
// it starts first: +0x90C, then ChangeState (here kind 21, or kind 29 with
// action 185/186).
constexpr CodeSite kStartAction{"StartAction", 0x372540,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xDE, 0x48}};
// void ProjectileDie(ProjectileUnit*): a projectile's end, which clears its
// live flag (+0x2B0, read alone by rva 0x450410).
constexpr CodeSite kProjectileDie{"ProjectileDie", 0x4576B0,
	{0x40, 0x55, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB1, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00, 0x48}};
// void Damage(CharaStatusPart*, ...): a hit's damage to the vital (+0xC0, of
// +0xC4), 16.16 (EMBER_LIBRARY.md section 27).
constexpr CodeSite kDamage{"Damage", 0x361670,
	{0x44, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48}};

// The fighter's picture: Ember's Tiny fighters (EMBER_LIBRARY.md section 59)
// draws a side's model small, on screen only. Each site's bytes are read from
// the exe; BuildCheck verifies them, so another build hooks nothing.
//
// void CharaVisualPartUpdate(CharaVisualPart*) (__fastcall): once per
// presented frame per fighter on the game thread; pushes the fighter's
// transform into its Unreal actor. Hooked: the original runs first, then a
// tiny side's mesh is scaled.
constexpr CodeSite kCharaVisualUpdate{"CharaVisualUpdate", 0x3A06C0,
	{0x40, 0x55, 0x53, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x98, 0xFD, 0xFF, 0xFF, 0x48}};
// void USceneComponent::SetWorldScale3D(USceneComponent*, const float* xyz)
// (UE4: divides by the parent's scale and sets the relative scale). The game
// calls it on the fighter's mesh component itself (rva 0x3A16C4). Called, not
// hooked.
constexpr CodeSite kSetWorldScale3D{"SetWorldScale3D", 0x1BE8B40,
	{0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x2F, 0x97, 0xDD}};
// void ActorSetMeshTransform(AActor*, const __m128 transform[3]) (__fastcall):
// the actor's whole-mesh transform, [0] the rotation quaternion, [1] the
// translation, [2] the scale (xyz). Cinematics (Critical Arts and the like)
// may set the mesh's transform through it after the per-frame update above.
// Hooked: a tiny side's actor gets the scale times kTinyScale.
constexpr CodeSite kActorSetMeshTransform{"ActorSetMeshTransform", 0xE5BD90,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x89}};
// void SocketWorldPosition(CharaVisualPart*, const wchar_t* socket, float out[4],
// bool) (__fastcall): a socket of the fighter's model where the Unreal pose
// last left it: the actor (visual::kActor), its mesh (visual::kActorMesh),
// the mesh's GetSocketTransform in world space (its virtual +856, from rva
// 0xE58F10), the translation turned into the fight's units (x/100, z/100,
// -y/100, w 1). The fight reads it: a fighter's bone 28 every tick (rva
// 0x37D250), the camera's handlers, and spawns placed at a bone (rva 0x4CFB70
// through 0x32BAA0 for any script's spawn with a bone; Z33's part at the end
// of its V-Trigger, rva 0x3BD900). The socket names come from the table rva
// 0x351250 reads. Hooked (SocketReads): a read on a stale pose takes the
// straight play's value for the same state.
constexpr CodeSite kSocketWorldPosition{"SocketWorldPosition", 0x3919D0,
	{0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x9F, 0x08, 0x63}};

namespace visual {

// CharaVisualPart: its type id, what the game's own lookups pass FindPart
// (rva 0x3BB4A0, 0x3BD900).
constexpr uint32_t kPartType = 0x3A64AD0;
constexpr uint32_t kWantedScale = 0xD0;     // float3 (+0xD0, +0xD4, +0xD8): the scale the game wants; its setter (rva 0x3A3C20) writes 16 bytes here
constexpr uint32_t kOwnerObject = 0x98;     // the owning fighter's weak pointer: the CharaUnit* ...
constexpr uint32_t kOwnerControl = 0xA0;    // ... and its control block (the game's lock, rva 0x315870, reads both)
constexpr uint32_t kActor = 0xA90;          // AActor* the part updates
// The control block's use count (int): the object is alive while it is above 0.
constexpr uint32_t kControlUseCount = 8;
// AActor: the mesh component the game scales itself.
constexpr uint32_t kActorMesh = 0x3A8;
// The fighter actor's other skeletal mesh components: a second one (rva
// 0xE5D300 and 0xE5D880 work on it) and the layers the visual task sets a
// motion and time on, an array of pointers and its count (rva 0xE5F7D0).
constexpr uint32_t kActorSecondMesh = 0x400;
constexpr uint32_t kActorLayers = 0x420;
constexpr uint32_t kActorLayerCount = 0x428;
constexpr int kMostLayers = 32; // a sanity bound on the count

} // namespace visual

// UObject and USkinnedMeshComponent, as the engine in this build lays them out.
namespace mesh {

constexpr uint32_t kObjectFlags = 0x8;  // int32; the game skips an object with kUnusable set (rva 0xE5F7D0)
constexpr uint32_t kUnusable = 0x4000;
constexpr uint32_t kObjectClass = 0x10; // UClass*
constexpr uint32_t kClassCastFlags = 0xA8; // uint64 in the UClass
// The cast flag the game tests before asking a component for a socket (rva
// 0xE58F10): a skeletal mesh component.
constexpr uint64_t kSkeletalMeshComponentFlag = 0x40000000000ull;
// USkinnedMeshComponent's reflected properties (their offsets are the ones
// the class's registration passes, rva 0x20F7A60), read by its LOD update
// (rva 0x1C8B830): ForcedLodModel > 0 forces LOD ForcedLodModel - 1;
// otherwise the LOD is the renderer's from the mesh's screen size (the mesh
// object +24) plus the global LOD bias. MinLodModel then raises it.
// RefreshBoneTransforms (rva 0x1C0B890) rebuilds the bones it evaluates from
// PredictedLODLevel (rva 0x1C0B3E0).
constexpr uint32_t kForcedLodModel = 0x560;    // int, 1376
constexpr uint32_t kMinLodModel = 0x564;       // int, 1380
constexpr uint32_t kPredictedLodLevel = 0x568; // int, 1384
constexpr uint32_t kUpdateFlag = 0x5AC;        // uint8 MeshComponentUpdateFlag, 1452 (logged only)
constexpr int32_t kForceLodZero = 1;

} // namespace mesh

namespace fighter {

// CharaUnit (unit::kCharaUnitType).
constexpr uint32_t kHitStopElapsed = 0x104; // 16.16
constexpr uint32_t kHitStopTotal = 0x108;   // 16.16
constexpr uint32_t kSide = 0x758;           // int: 0 P1, 1 P2 (practice::kCharaSide)
constexpr uint32_t kStartedAction = 0x90C;  // int: the action the last state change started
constexpr uint32_t kUntilActionable = 0x91E; // int16: whole frames of +0x91C
constexpr int kUntimed = 1000;              // what +0x91E holds when the game cannot time it
constexpr uint32_t kStateMachine = 0xC58;   // StateMachine*
constexpr uint32_t kCachedAdvantage = 0xE30; // int: kFrameAdvantage's cached value (logged, never fed)

// The state machine and its table of states (kChangeState, kStateTableInit).
constexpr uint32_t kCurrentState = 0x10;    // state singleton*
constexpr uintptr_t kStateTable = 0x3A66160; // data rva: [kind] -> singleton, kind 1..kStateKinds
constexpr int kStateKinds = 29;

// The state kinds, each as it was seen in game (EMBER_LIBRARY.md section 39).
// 10, 11, 13, 20, 21 and 28 were not seen (rva 0x372540 enters 21 on a KO);
// None is a state not in the table.
enum class StateKind : int {
	None = 0, Stand = 1, Crouch = 2, PreJump = 3, Jump = 4, Landing = 5, WalkForward = 6, WalkBack = 7, DashForward = 8,
	DashBack = 9, Guard = 12, StandToCrouch = 14, CrouchToStand = 15, TurnStand = 16, TurnCrouch = 17, TurnWalk = 18,
	Attack = 19, Hit = 22, GuardHit = 23, LaunchHit = 24, Down = 25, LyingDown = 26, Rise = 27, Held = 29
};

// CharaActionPart (type below) +0xD8: the script player of what the fighter plays.
constexpr uint32_t kActionPartType = 0x3A64A98;
constexpr uint32_t kScriptPlayer = 0xD8;
// int: 1 while the fighter faces left, and the fight mirrors its x (rva
// 0x2FB820 compares it with 1, rva 0x2F9310 returns it); fight state, in the
// action part's registered run (rva 0x2E8D30: its controller's +15824).
constexpr uint32_t kMirrored = 0x3E90;
constexpr uint32_t kScriptHeader = 0x40;    // const int32[3]: first attack frame, last (exclusive), interrupt; -1 none
constexpr uint32_t kScriptNumber = 0x48;    // int: the script (action) played
constexpr uint32_t kScriptFrame = 0x4C;     // 16.16
constexpr uint32_t kScriptTimeScale = 0x64; // 16.16: 0 while another fighter's freeze holds this one
constexpr uint32_t kHeaderTotalShort = 0x0C; // the game reads the total as int16
constexpr uint32_t kHeaderPosture = 0x2E;   // int16: 0 standing, 1 crouching, 2 airborne, 3 lying, 4 knocked into the air

// CharaStatusPart (type below).
constexpr uint32_t kStatusPartType = 0x3A64B08;
constexpr uint32_t kVital = 0xC0;           // 16.16
constexpr uint32_t kVitalMax = 0xC4;        // 16.16

// ProjectileUnit (type below): its owner's side and whether it is out.
constexpr uint32_t kProjectileUnitType = 0x3A64C20;
constexpr uint32_t kProjectileOwner = 0x100; // int: the side that threw it
constexpr uint32_t kProjectileLive = 0x2B0;  // int: 1 from the tick it comes out until the tick it hits or ends

} // namespace fighter

} // namespace sfve::mod::sfv
