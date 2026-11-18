#pragma once

// The fight's reads of a fighter's Unreal model (docs/design/beta8/desync.md;
// docs/design/beta9/socket-anchors.md and its beta.10 note).
//
// Some spawns are placed at a socket of the model (sfv::kSocketWorldPosition,
// rva 0x3919D0): the read returns where the pose last evaluated by the engine
// puts the socket. The engine evaluates the pose once per drawn frame, so a
// rollback's replayed frames, and the new frame a rollback is followed by,
// read a stale pose, and two copies with different rollback histories can
// read different values for the same state.
//
// So the hook decides each read by one rule, a policy of the call site (the
// rva of its return address; Policy below):
//   - Outside a battle a session plays (BattleLatch), every read is Live: the
//     engine's value, as the game would have it.
//   - In one, a read whose value never reaches fight state is Live too: the
//     call sites that are not the fight's (the camera, the interface, the
//     visual tasks; kNotTheFights) and the spawn bone resolver, whose value
//     only places an effect (Site::DrawOnly).
//   - Every other read is Anchored: it is given a value computed from fight
//     state alone, the same on every copy: the owning fighter's fight position
//     (CharaUnit +0x780, as the update began) plus an offset for the call site
//     and socket, mirrored by the way the fighter faces (AnchorOffsetFor,
//     AnchorPosition). The four call sites known to feed fight state have
//     their own offsets; a call site no one has classified takes the head's
//     and is logged once a battle, so a newly found site is deterministic by
//     construction rather than a desync waiting for a report.
// All 20 references to rva 0x3919D0 in build 13173604 are classified (19
// calls and an unwind entry), so in practice the unknown-site rule is a net.
// When the fighter's unit, position or facing cannot be read, an anchored read
// keeps the engine's value, and is counted and logged.
//
// Beta.8 and beta.9 recorded fresh reads and served them to stale ones, and
// held a tick for a drawn frame when no recorded value fitted (pose holds).
// With every read that reaches fight state anchored, neither has a job, and
// both are gone (docs/design/beta9/pose-hold.md, its beta.10 note).
//
// With -sfve_socket_log=1 (development) every anchored read is logged too.

#include "mod/SfvOffsets.hxx"

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

constexpr int kMostFighterUnits = (int)sfv::unit::kMostCharaUnits;

// One fighter unit as an update begins (SfvAdapter, the game thread): what an
// anchored read of its model is given.
struct UnitPlace {
	const uint8_t* chara = nullptr; // the CharaUnit: its visual part's owner
	int32_t unit = -1;              // its place among the battle's CharaUnits
	int32_t side = -1;
	// The unit's fight position (sfv::fighter::kFightPosition) and facing as
	// the update begins; `anchorable` only when both were read and the
	// position is finite.
	bool anchorable = false;
	float position[4] = {};
	bool facingLeft = false;
};

// The battle update about to run (SfvAdapter::RunUpdate, the game thread),
// read by the socket reads it makes.
struct SocketUpdate {
	int32_t inputFrame = -1;
	bool replaying = false; // a rollback's replayed frame (for the log)
	UnitPlace units[kMostFighterUnits];
	int unitCount = 0;
};
void NoteSocketUpdate(const SocketUpdate& update);
// A new battle: every count, and what was logged once a battle, is forgotten
// (log the counts first, SocketReadsSummary).
void ForgetSocketReads();
// Hooks the socket read; false when it could not be hooked (the reads then
// stay the game's). Once, from SfveStart. `log`: -sfve_socket_log; `anchor`:
// -sfve_socket_anchor (off: every read is Live, for a test).
bool InstallSocketReads(uintptr_t imageBase, bool log, bool anchor);
// The battle's counts so far (socket_reads::CountsText), under the counts'
// own lock: safe from any thread (the log, a desync report).
std::string SocketReadsSummary();

// The same pose on every copy, whatever its window. The engine evaluates a
// skeletal mesh's bones at the LOD the renderer picks from its size on
// screen, so two copies at 960x540 and 1920x1080 read a socket a few ulps
// apart (2026-11-12, F5: ProjectileUnit[20]+0x1D0's y and z one ulp apart).
// Each battle update (SfvAdapter, the game thread, before the update) gives
// every skeletal mesh component of every fighter unit's actor (partners
// included) LOD 0 (ForcedLodModel 1, sfv::mesh), which the engine's LOD
// update (rva 0x1C8B830) takes over the screen size. The anchored reads no
// longer read the pose, but what is drawn from it (effects, the camera) is
// then the same picture on every copy. `visualParts`: the units'
// CharaVisualParts in the battle's order (null where there is none), `count`
// of them. Each unit's components are logged once a battle with what the
// engine had picked (forget with ForgetSocketReads).
void KeepFighterMeshesAtLodZero(uint8_t* const* visualParts, int count);

namespace socket_reads {

// One skeletal mesh component of a fighter's actor as ForceLodZero found it.
struct MeshLod {
	int component = 0; // 0 the actor's mesh, 1 its second mesh, 2 + i layer i
	int32_t forced = 0;    // ForcedLodModel as found
	int32_t minLod = 0;    // MinLodModel
	int32_t predicted = 0; // PredictedLODLevel: the LOD the engine evaluated last
	uint8_t updateFlag = 0; // MeshComponentUpdateFlag as found
	bool written = false;  // ForcedLodModel was set to 1 here
	bool flagWritten = false; // the update flag was set to AlwaysTickPoseAndRefreshBones here
};

// Gives each skeletal mesh component of `actor` (its mesh, its second mesh and
// its layers, sfv::visual) ForcedLodModel 1 where it holds another value, and
// the update flag AlwaysTickPoseAndRefreshBones (the pose is then evaluated
// every frame, rendered or not); neither is ever put back: a battle's fighter
// meshes are the mod's to keep that way; an
// object that is not a usable skeletal mesh component (sfv::mesh's flag and
// class tests, as the game makes them) is left alone. Fills `seen`, if any,
// with each component as found. Returns how many it wrote.
int ForceLodZero(uint8_t* actor, std::vector<MeshLod>* seen);

// A socket's name as a number (FNV-1a of its characters, at most 64).
uint64_t NameKey(const wchar_t* name);

// What the read wrote: x, y, z in the fight's units, w.
struct Position {
	float xyzw[4] = {};
	bool operator==(const Position& o) const; // bitwise
};

// What a call site of the socket read is, by the rva of its return address
// (call site + 5; read in IDA, build 13173604).
enum class Site : uint8_t {
	NotTheFights, // kNotTheFights: bone 28 for the camera, the camera's handlers, the visual tasks, the interface
	DrawOnly,     // the spawn bone resolver (return rva 0x4CFC99): its position only places an effect
	Known,        // one of the four that feed fight state: 0x3BDB02, 0x3BDF3B, 0x3BC294, 0x3A4B79
	Unknown,      // any other: not classified
};
Site SiteOf(uint32_t returnRva);
const char* SiteName(Site site);

// Version of the anchor rules below: part of the room's build
// (LayoutRulesId, with the sites and offsets themselves: AnchorRulesText).
// It moves with the rules' code (Policy, AnchorPosition), which no table
// shows. 2 from beta.10: every call site not known to stay out of fight
// state is anchored (the head's offset for one not classified), and no read
// is recorded, served or snapped to a grid any more, so a copy of version 1
// computes some reads differently and must not meet one of 2.
constexpr int kAnchorVersion = 2;

// Where from the owner's fight position an anchored read starts, in the
// fight's units: `forward` along the way the fighter faces, `up` from the
// feet.
struct AnchorOffset {
	float forward = 0;
	float up = 0;
};
// The head's offset: the dropped prop's, the mask's, and any unknown site's.
constexpr AnchorOffset kHeadOffset{0.00f, 1.60f};
// The offset for a site and the socket it asked. A hand-held item
// (0x3BDB02, 0x3BDF3B): 0.20, 1.00; at 0x3A4B79 a hand (b_R_Hand, b_L_Hand):
// 0.30, 1.20, the head (b_C_Head): 0.05, 1.60, and any other name there the
// head's too, setting `*otherName`, if given. Every other rva (the prop's at
// 0x3BC294, an unknown site's): kHeadOffset.
AnchorOffset AnchorOffsetFor(uint32_t returnRva, const wchar_t* socket, bool* otherName = nullptr);

// The anchor rules as text: kAnchorVersion; every call site's class
// (kNotTheFights, the spawn bone resolver, the four known sites); the offset
// AnchorOffsetFor gives each known site for each socket name it tells apart
// and for any other; and the head's (kHeadOffset). Part of the rollback
// rules' id (SfvLayoutRules) while the anchors are on: a site moved between
// classes, or an offset changed, changes what copies compute.
std::string AnchorRulesText();

// What the read writes: x = base x + (-1 facing left, else 1) * forward,
// y = base y + up, z = 0 (the fight plane), w = 1. `base`: the owner's fight
// position, four floats.
Position AnchorPosition(const float base[4], bool facingLeft, const AnchorOffset& offset);

// The one rule for a read (the top of this file).
struct ReadRule {
	bool anchor = false; // false: Live, the engine's value
	AnchorOffset offset; // where an anchored read starts
	Site site = Site::Unknown;
	bool otherName = false; // AnchorOffsetFor's: the claw's site asked a socket it has no offset for
};
// `sessionBattle`: the read is made in a battle a session plays, with the
// anchors on (BattleLatch, -sfve_socket_anchor). Outside one every read is
// Live; in one, NotTheFights and DrawOnly are Live, Known and Unknown
// anchored.
ReadRule Policy(uint32_t returnRva, const wchar_t* socket, bool sessionBattle);

// One battle's counts.
struct Counts {
	uint64_t anchored = 0;         // reads given the fight's position
	uint64_t unknownSites = 0;     // ...of them at a call site not classified (Site::Unknown)
	uint64_t anchorUnresolved = 0; // reads to anchor whose unit, position or facing was not known: the engine's value kept
};
// "A given from the fight's position (N at a site not classified), U anchor
// unresolved": every count.
std::string CountsText(const Counts& counts);

} // namespace socket_reads

} // namespace sfve::mod
