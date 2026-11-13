#pragma once

// The fight's reads of a fighter's Unreal model (docs/design/beta8/desync.md,
// its sections on the socket fix and "Beta.9: the pose record").
//
// Some spawns are placed at a socket of the model (sfv::kSocketWorldPosition):
// any script's spawn whose record names a bone, and Z33's drop as its
// V-Trigger ends. The read returns where the pose last evaluated by the engine
// puts the socket. The engine evaluates the pose once per drawn frame, after
// the battle's visual task has set each layer's motion and time from the
// fight state, so the first battle update after a drawn frame (with no
// restore before it) reads the pose of the state that update starts from,
// on every copy. Every other update reads a stale pose: a rollback's
// replayed frames, and the new frame a rollback is followed by.
//
// So each read on a fresh pose is recorded, and a read on a stale pose takes
// a recorded value instead of the live one. The record is keyed by what
// decides the value: the fighter unit (its place among the battle's
// CharaUnits, partners included), the socket, and the unit's pose key (its
// animation state, facing and Tiny pick, MakePoseKey); within that, each
// reading keeps the unit's root key (where it stands, MakeRootKey), the
// model's transforms the read came from, and what the read wrote.
//   - Exact: a reading of the same pose at the same root gives its value as
//     it was written. That is what the straight play of that state reads.
//   - Rebuilt: a reading of the same pose at another root, whose mesh had the
//     same rotation and scale, gives the value the engine computes at the
//     current root, when that value is certain (CertifiedSum, Rebuild):
//     the engine adds the mesh's translation last, so a socket's world
//     position is fl(r + T) with r the same for both readings; every r the
//     recorded reading allows is tried at the new T, and the value is used
//     only if they all give the same float. Only while the models stand
//     where the update's state puts them (no restore since the last update).
//   - Otherwise the read keeps the live value and is counted as missed: no
//     value both copies would compute alike is known (the copy that guessed
//     right reads the engine's own value for that pose, and nothing short of
//     evaluating the pose gives it).
// A fresh read that finds its pose and root with another value is counted as
// conflicting (the keys would not hold all the value depends on); one whose
// certified rebuild from another root disagrees with it marks that pose
// never-rebuild for the battle (the arithmetic model would be wrong for it;
// its other-root readings are dropped) and is counted too.
// Reads the fight does not make (bone 28 every tick, the camera's, the visual
// tasks') are left as they are.
//
// Refreshing the pose before a replayed tick instead was looked at and left
// out: the pose comes from the engine's component tick (the mesh's virtual
// +0x7D0), which in SFV's layer class advances looping layers by a fixed 1/60
// per call and sets material parameters (rva 0xE62E70), so extra calls during
// a rollback would change the picture after it, and the tick expects the
// engine's tick function.
//
// With -sfve_socket_log=1 (development) every fight read is logged too, a
// missed one with its call site, socket and why.

#include "mod/SfvOffsets.hxx"

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace sfve::mod {

constexpr int kMostFighterUnits = (int)sfv::unit::kMostCharaUnits;

// One fighter unit as an update begins (SfvAdapter, the game thread).
struct UnitKeys {
	const uint8_t* chara = nullptr; // the CharaUnit: its visual part's owner
	int32_t unit = -1;              // its place among the battle's CharaUnits
	int32_t side = -1;
	uint64_t pose = 0; // MakePoseKey; 0 unknown
	uint64_t root = 0; // MakeRootKey
};

// The battle update about to run (SfvAdapter::RunUpdate, the game thread),
// read by the socket reads it makes.
struct SocketUpdate {
	int32_t inputFrame = -1;
	bool replaying = false; // a rollback's replayed frame (for the log)
	bool fresh = false;     // the models show the state this update starts from
	// The models stand where that state puts them: no restore since the last
	// update, whose visual task placed them (a rebuild needs it).
	bool placed = false;
	UnitKeys units[kMostFighterUnits];
	int unitCount = 0;
};
void NoteSocketUpdate(const SocketUpdate& update);
// A new battle: every recorded read and every count is forgotten (log the
// counts first, SocketReadsSummary).
void ForgetSocketReads();
// Hooks the socket read; false when it could not be hooked (the reads then
// stay the game's). Once, from SfveStart. `log`: -sfve_socket_log.
bool InstallSocketReads(uintptr_t imageBase, bool log);
// The battle's counts so far, for the log (SfvAdapter, with the keys' cost).
std::string SocketReadsSummary();
// The latest misses (at most socket_reads::MissRing::kMostMisses) whose frame
// lies in [first, last], oldest first, for a desync report (copied under the
// reads' own lock: safe from any thread).
std::vector<socket_reads::Miss> SocketMissesBetween(int32_t first, int32_t last);

// The same pose on every copy, whatever its window. The engine evaluates a
// skeletal mesh's bones at the LOD the renderer picks from its size on
// screen, so two copies at 960x540 and 1920x1080 read a socket a few ulps
// apart (2026-11-12, F5: ProjectileUnit[20]+0x1D0's y and z one ulp apart).
// Each battle update (SfvAdapter, the game thread, before the update) gives
// every skeletal mesh component of every fighter unit's actor (partners
// included) LOD 0 (ForcedLodModel 1, sfv::mesh), which the engine's LOD
// update (rva 0x1C8B830) takes over the screen size. `visualParts`: the
// units' CharaVisualParts in the battle's order (null where there is none),
// `count` of them. Each unit's components are logged once a battle with
// what the engine had picked (forget with ForgetSocketReads).
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

// A read from a call site that is not the fight's, by the rva of its return
// address: left as it is, not logged.
bool EveryTick(uint32_t returnRva);

// A socket's name as a number (FNV-1a of its characters, at most 64).
uint64_t NameKey(const wchar_t* name);

// A run of bytes a key is made of; a null `bytes` (a part the unit lacks)
// counts as empty, the same on every call.
struct Run {
	const uint8_t* bytes = nullptr;
	size_t size = 0;
};
// The pose key: the unit's animation state (its CharaMotionPart's two
// registered runs, sfv::fighter::kMotionRun and kMotionRun2), its facing
// (sfv::fighter::kMirrored) and whether its side is drawn small. Every
// 8-byte-aligned word of the runs that looks like an address (LooksLikeAddress)
// is hashed as zero, as the state hashes do: a pointer to per-instance heap
// differs between a play and its replay, and the key must not. Never 0.
uint64_t MakePoseKey(const Run motion[2], int32_t facing, bool tiny);
// The root key: where the unit stands (CharaUnit's kUnitRun and kPlaceRun),
// leaving out kPlaceRun's bone 28 bytes [kPlaceBoneFrom, kPlaceBoneTo).
// Never 0.
uint64_t MakeRootKey(const Run& unit, const Run& place);

// What the read wrote: x, y, z in the fight's units, w.
struct Position {
	float xyzw[4] = {};
	bool operator==(const Position& o) const; // bitwise
};

// The mesh component's world transform as the engine keeps it (FTransform):
// rotation quaternion, translation (cm), scale; the fourth lane of the last
// two is not compared.
struct ModelTransform {
	alignas(16) float rotation[4] = {};
	float translation[4] = {};
	float scale[4] = {};
};
// The engine's reading of one socket: its mesh's world transform and the
// socket's world translation (cm), which the read turns into `Position`.
struct ModelReading {
	ModelTransform mesh;
	float socket[3] = {};
};
// What the game's read writes for a world translation (sfv::kSocketWorldPosition):
// x * 0.01f, z * 0.01f, y * -0.01f, 1, each one float multiply.
Position FightPosition(const float socket[3]);
// The same rotation and scale, bit for bit.
bool SameTurnAndScale(const ModelTransform& a, const ModelTransform& b);
// One axis of the engine's last add, fl(r + root), for every float r with
// fl(r + recordedRoot) == recordedSum. True with `out` set when they all give
// the same float (rounding is monotonic, so the smallest and largest such r
// decide it); false when they do not, when no r gives recordedSum (the
// engine did not add as assumed), or for a value that is not finite. Single
// precision adds, round to nearest even, as the engine's SSE add.
bool CertifiedSum(float recordedSum, float recordedRoot, float root, float* out);
// The value the read gives at `now` for the pose of `recorded`: the same
// rotation and scale, and CertifiedSum on each axis, then FightPosition.
// False when either does not hold.
bool Rebuild(const ModelReading& recorded, const ModelTransform& now, Position* out);

struct Key {
	uint64_t pose = 0;   // MakePoseKey
	uint64_t socket = 0; // NameKey
	int32_t unit = -1;   // UnitKeys::unit
	bool operator==(const Key& o) const { return pose == o.pose && socket == o.socket && unit == o.unit; }
};

struct KeyHash {
	size_t operator()(const Key& k) const;
};

// A fresh read as recorded.
struct Reading {
	uint64_t root = 0; // MakeRootKey as the update began
	Position out;      // what the read wrote
	// `at` holds the transforms the read came from (SocketReads read them
	// again and they give `out`); only such a reading is rebuilt from.
	bool model = false;
	ModelReading at;
};

enum class Recorded {
	New,              // a pose or root not kept before
	Again,            // the same value again
	Conflicting,      // the same pose and root with another value (the newer is kept)
	RebuildDisagrees, // a certified rebuild from another root gave another value: this pose is never rebuilt again
};
enum class Found {
	Exact,     // the same pose and root
	Rebuilt,   // the same pose at another root, certified
	NoPose,    // this pose was never read fresh
	OtherRoot, // read fresh only at other roots, and no certain rebuild
};

// Recorded reads: up to kRootsPerPose roots per (unit, socket, pose), the
// oldest root replaced; the oldest pose dropped past `capacity` poses. Not
// thread-safe: the hook holds a lock.
class Cache {
public:
	static constexpr size_t kRootsPerPose = 4;
	explicit Cache(size_t capacity = 8192) : capacity_(capacity) {}
	Recorded Record(const Key& key, const Reading& reading);
	// A stale read's value. `now`: the model's transform as the read found it,
	// when the models stand where the update's state puts them (else null,
	// and nothing is rebuilt).
	Found Find(const Key& key, uint64_t root, const ModelTransform* now, Position* out);
	// False once a rebuild for this pose disagreed with a fresh read, or two
	// readings of it rebuilt to different values: that one pose (all its
	// sockets' slots are separate) is never rebuilt again this battle (until
	// Clear); the others still are.
	bool Rebuilding(const Key& key) const;
	// How many (unit, socket, pose) slots are never rebuilt.
	size_t NeverRebuilt() const { return neverRebuilt_; }
	void Clear();
	size_t Size() const { return values_.size(); }

private:
	struct Slot {
		std::array<Reading, kRootsPerPose> readings;
		size_t count = 0;
		size_t next = 0; // the reading replaced next once all are taken
		bool neverRebuild = false;
	};
	size_t capacity_;
	size_t neverRebuilt_ = 0;
	std::unordered_map<Key, Slot, KeyHash> values_;
	std::deque<Key> order_; // insertion order, for dropping the oldest
};

// Why a stale read got the live value (the counts' misses, one by one).
enum class MissReason : uint8_t {
	NeverSeenPose,           // this pose was never read fresh
	OtherRootNotCertifiable, // read fresh at other roots only, and no certain rebuild (or no model, or not placed)
	RebuildingOff,           // read fresh at other roots only, and this pose is never rebuilt
};
const char* MissReasonName(MissReason reason);

// One miss, for the desync report: where it was and why.
struct Miss {
	int32_t frame = -1; // the input frame of the update that made the read
	int32_t unit = -1;  // the fighter unit (-1: not known)
	uint32_t from = 0;  // the call site's rva
	MissReason reason = MissReason::NeverSeenPose;
	char socket[32] = {};
};

// The latest misses of a battle, a ring of kMostMisses: the oldest goes. Not
// thread-safe: the hook holds a lock.
class MissRing {
public:
	static constexpr size_t kMostMisses = 64;
	void Add(const Miss& miss);
	// The misses whose frame lies in [first, last], oldest first.
	std::vector<Miss> Between(int32_t first, int32_t last) const;
	size_t Size() const { return count_; }
	void Clear() { next_ = count_ = 0; }

private:
	std::array<Miss, kMostMisses> misses_;
	size_t next_ = 0;
	size_t count_ = 0;
};

// One battle's counts.
struct Counts {
	uint64_t recorded = 0;     // fresh reads
	uint64_t exact = 0;        // stale reads given a recorded value of the same pose and root
	uint64_t rebuilt = 0;      // ...of the same pose at another root, certified
	uint64_t noPose = 0;       // missed: the pose never read fresh
	uint64_t otherRoot = 0;    // missed: read fresh at other roots only
	uint64_t conflicting = 0;  // Recorded::Conflicting
	uint64_t disagreeing = 0;  // Recorded::RebuildDisagrees
	uint64_t unkeyed = 0;      // a unit with no keys this update
};
// "N fresh read(s) recorded, ..., K conflicting (D of them a rebuild's), ...":
// every count, and `kept`. K counts both kinds, as tools/synctest.ps1 -check
// fails on "[1-9]... conflicting".
std::string CountsText(const Counts& counts, size_t kept, size_t neverRebuilt);

} // namespace socket_reads

} // namespace sfve::mod
