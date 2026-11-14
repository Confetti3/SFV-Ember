#include "mod/SocketReads.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/ProjectileRelease.hxx"

#include <xxhash.h>

#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace sfve::mod {

namespace {

// sfv::kSocketWorldPosition: (CharaVisualPart*, socket name, float out[4],
// flag); the flag is passed on untouched.
using SocketFn = void (*)(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag);
// sfv::kMakeName and sfv::kSocketTransform, called as the socket read calls them.
using MakeNameFn = uint64_t* (*)(uint64_t* name, const wchar_t* text, int32_t find, int32_t split);
using SocketTransformFn = void (*)(uint8_t* actor, const uint64_t* name, float* transform);

SocketFn o_socket;
MakeNameFn s_makeName;
SocketTransformFn s_socketTransform;
uintptr_t s_base;
bool s_log;

// The update now running (NoteSocketUpdate, before the update's tasks
// start), the record and the battle's counts. The reads run on the battle's
// job threads, several units' at once; they are few (spawns at a bone), so
// one lock serves them all.
std::mutex s_mutex;
SocketUpdate s_update;
socket_reads::Cache s_cache;
socket_reads::MissRing s_misses;
socket_reads::Counts s_counts;
socket_reads::HoldCounts s_holds;
// The update now running's reads (TakeUpdateReads), reset by NoteSocketUpdate.
socket_reads::UpdateReads s_updateReads;
std::atomic<int> s_lines;
// A runaway caller cannot fill the disk: the log stops here.
constexpr int kMostLines = 20000;
// The conflicts logged in any build, a battle (ForgetSocketReads).
std::atomic<int> s_conflictLines;
constexpr int kMostConflictLines = 16;

// The calls of the socket read that are not the fight's, by their return
// addresses (call site + 5; read in IDA, build 13173604).
constexpr uint32_t kNotTheFights[] = {
	0x37D48E,                     // Chara PreInput (rva 0x37D250): bone 28 into CharaUnit+1984 (masked, the camera's)
	0x31B10E, 0x31B1DE,           // the camera's handler rva 0x31AF60
	0x32FA5F,                     // the camera's handler rva 0x32F790
	0x33C41F,                     // the camera's handler rva 0x33C1C0
	0x3E3EF0, 0x3E3F57,           // rva 0x3E3E60, from rva 0x3E39F0
	0x38F401, 0x38F69B, 0x38F6B7, // the head (b_C_Head) for Chara PostVisual (rva 0x3897C0 through 0x3A53E0), after the tick
};

float FloatOf(uint32_t bits)
{
	return std::bit_cast<float>(bits);
}

std::string SocketName(const wchar_t* name)
{
	std::string out;
	for (int i = 0; name && i < 48; i++) {
		wchar_t c = 0;
		if (!TryRead(name + i, &c) || !c) break;
		out += c >= 0x20 && c < 0x7F ? (char)c : '?';
	}
	return out.empty() ? "?" : out;
}

// The CharaUnit a visual part belongs to, or null.
const uint8_t* OwnerOf(const uint8_t* visualPart)
{
	const uint8_t* chara = nullptr;
	return visualPart && TryRead(visualPart + sfv::visual::kOwnerObject, &chara) ? chara : nullptr;
}

// One read as the log names it.
struct ReadContext {
	int32_t frame = -1;
	bool replaying = false;
	int32_t unit = -1;
	int32_t side = -1;
	const wchar_t* socket = nullptr;
	uint32_t from = 0;
};

void LogRead(const ReadContext& read, const char* what, const float* p)
{
	if (!s_log || s_lines.fetch_add(1) >= kMostLines) return;
	const std::string name = SocketName(read.socket);
	char who[32];
	if (read.unit >= 0) snprintf(who, sizeof(who), "unit %d (P%d)", read.unit, read.side + 1);
	else snprintf(who, sizeof(who), "no known unit");
	if (!p) {
		Log("socket: frame %d%s, %s, socket %s, from rva 0x%X: %s", read.frame, read.replaying ? " replayed" : "", who, name.c_str(),
			read.from, what);
		return;
	}
	Log("socket: frame %d%s, %s, socket %s, from rva 0x%X: %.5f %.5f %.5f %s", read.frame, read.replaying ? " replayed" : "", who,
		name.c_str(), read.from, p[0], p[1], p[2], what);
}

// The transforms the read came from, read again the way it reads them: the
// socket's world transform and, for NAME_None, the mesh component's own
// (sfv::kSocketTransform). False when either was not written (the actor has
// no skeletal mesh with a mesh asset). The same calls, on the same thread, as
// the read that just ran.
bool ReadModel(uint8_t* actor, const wchar_t* socket, socket_reads::ModelReading* reading)
{
	if (!s_makeName || !s_socketTransform || !actor) return false;
	alignas(16) float socketTransform[12];
	alignas(16) float meshTransform[12];
	// All bits set, a NaN no transform holds: what is still there was not written.
	memset(socketTransform, 0xFF, sizeof(socketTransform));
	memset(meshTransform, 0xFF, sizeof(meshTransform));
	uint64_t name = 0;
	s_makeName(&name, socket, 1, 1);
	s_socketTransform(actor, &name, socketTransform);
	const uint64_t none = 0;
	s_socketTransform(actor, &none, meshTransform);
	const auto written = [](const float* transform) { return std::bit_cast<uint32_t>(transform[4]) != 0xFFFFFFFFu; };
	if (!written(socketTransform) || !written(meshTransform)) return false;
	memcpy(reading->mesh.rotation, meshTransform, sizeof(reading->mesh.rotation));
	memcpy(reading->mesh.translation, meshTransform + 4, sizeof(reading->mesh.translation));
	memcpy(reading->mesh.scale, meshTransform + 8, sizeof(reading->mesh.scale));
	memcpy(reading->socket, socketTransform + 4, sizeof(reading->socket));
	return true;
}

const char* MissText(socket_reads::Found found, bool placed, bool model, bool rebuilding, bool bone)
{
	if (found == socket_reads::Found::NoPose) return "(stale pose; this pose never read fresh: live)";
	if (!bone) return "(stale pose; read fresh at other roots only, and this socket is not a bone: exact or live only: live)";
	if (!placed) return "(stale pose; read fresh at other roots only, and the models stand where a restored state left them: live)";
	if (!model) return "(stale pose; read fresh at other roots only, and the model's transforms could not be read: live)";
	if (!rebuilding) return "(stale pose; read fresh at other roots only, and rebuilding is off for this pose: live)";
	return "(stale pose; read fresh at other roots only, and no certain rebuild: live)";
}

void h_Socket(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag)
{
	o_socket(visualPart, socket, out, flag);
	ReadContext read;
	read.from = (uint32_t)(reinterpret_cast<uintptr_t>(_ReturnAddress()) - s_base);
	if (socket_reads::EveryTick(read.from)) return;
	read.socket = socket;
	const uint8_t* chara = OwnerOf(visualPart);
	UnitKeys unit;
	bool fresh = false, placed = false;
	{
		std::lock_guard lock(s_mutex);
		read.frame = s_update.inputFrame;
		read.replaying = s_update.replaying;
		fresh = s_update.fresh;
		placed = s_update.placed;
		for (int i = 0; chara && i < s_update.unitCount; i++) {
			if (s_update.units[i].chara == chara) unit = s_update.units[i];
		}
	}
	read.unit = unit.unit;
	read.side = unit.side;
	// The read writes `out` whenever the part has an actor (rva 0x3919D0).
	uint8_t* actor = nullptr;
	if (!out || !visualPart || !TryRead(visualPart + sfv::visual::kActor, &actor) || !actor) {
		LogRead(read, "no model, nothing written", nullptr);
		return;
	}
	// A battle a session plays hands the game its values on the grid
	// (socket_reads::kGridSteps): the fresh read, the record's and a rebuild
	// alike, so this is the one place they are made. Training and the
	// player's own battles keep the engine's raw values.
	const bool grid = BattleLatch().Latched();
	if (grid) {
		for (int axis = 0; axis < 3; axis++) out[axis] = socket_reads::SnapToGrid(out[axis]);
	}
	socket_reads::Position live;
	memcpy(live.xyzw, out, sizeof(live.xyzw));
	if (!unit.pose) {
		{
			std::lock_guard lock(s_mutex);
			s_counts.unkeyed++;
		}
		LogRead(read, "(no key: live)", live.xyzw);
		return;
	}
	socket_reads::Reading reading;
	reading.root = unit.root;
	reading.out = live;
	// Only transforms that give what the game wrote are kept or rebuilt from.
	// Only a bone's transforms are rebuilt from (RebuildableSocket).
	reading.model = socket_reads::RebuildableSocket(socket) && ReadModel(actor, socket, &reading.at);
	if (reading.model) {
		socket_reads::Position again = socket_reads::FightPosition(reading.at.socket);
		if (grid) again = socket_reads::SnapPosition(again);
		reading.model = again == live;
	}
	const socket_reads::Key key{unit.pose, socket_reads::NameKey(socket), unit.unit};
	if (fresh) {
		socket_reads::Recorded recorded = socket_reads::Recorded::New;
		{
			std::lock_guard lock(s_mutex);
			s_cache.UseGrid(grid);
			recorded = s_cache.Record(key, reading);
			s_counts.recorded++;
			s_updateReads.fresh++;
			if (recorded == socket_reads::Recorded::Conflicting) s_counts.conflicting++;
			if (recorded == socket_reads::Recorded::RebuildDisagrees) s_counts.disagreeing++;
		}
		// Either says the keys or the rebuild miss something: logged in every
		// build (a few a battle), with what the next run needs to find it.
		const bool conflicting = recorded == socket_reads::Recorded::Conflicting;
		if ((conflicting || recorded == socket_reads::Recorded::RebuildDisagrees) &&
			s_conflictLines.fetch_add(1) < kMostConflictLines) {
			Log("socket: frame %d, unit %d (P%d), socket %s, from rva 0x%X: %.5f %.5f %.5f fresh, %s", read.frame, read.unit,
				read.side + 1, SocketName(socket).c_str(), read.from, live.xyzw[0], live.xyzw[1], live.xyzw[2],
				conflicting ? "CONFLICTING with the value kept for this pose and root (the keys miss what the pose depends on)"
							: "a certain rebuild from another root DISAGREES (this pose is never rebuilt again this battle)");
		} else if (s_log) {
			char text[128];
			if (reading.model) {
				snprintf(text, sizeof(text), "(fresh; recorded; mesh at %.3f %.3f %.3f)", reading.at.mesh.translation[0],
					reading.at.mesh.translation[1], reading.at.mesh.translation[2]);
			} else {
				snprintf(text, sizeof(text), "(fresh; recorded; no model transforms)");
			}
			LogRead(read, text, live.xyzw);
		}
		return;
	}
	socket_reads::Position kept;
	socket_reads::Found found = socket_reads::Found::NoPose;
	bool rebuilding = true, turnedOff = false;
	{
		std::lock_guard lock(s_mutex);
		s_cache.UseGrid(grid);
		const bool before = s_cache.Rebuilding(key);
		found = s_cache.Find(key, unit.root, placed && reading.model ? &reading.at.mesh : nullptr, &kept);
		rebuilding = s_cache.Rebuilding(key);
		// Two readings of this pose rebuilt to different values here.
		turnedOff = before && !rebuilding;
		if (turnedOff) s_counts.disagreeing++;
		switch (found) {
		case socket_reads::Found::Exact: s_counts.exact++; break;
		case socket_reads::Found::Rebuilt: s_counts.rebuilt++; break;
		case socket_reads::Found::NoPose: s_counts.noPose++; break;
		case socket_reads::Found::OtherRoot: s_counts.otherRoot++; break;
		}
	}
	if (turnedOff && s_conflictLines.fetch_add(1) < kMostConflictLines) {
		Log("socket: frame %d, unit %d (P%d), socket %s, from rva 0x%X: two readings of this pose rebuild to different values "
			"(this pose is never rebuilt again this battle)",
			read.frame, read.unit, read.side + 1, SocketName(socket).c_str(), read.from);
	}
	if (found == socket_reads::Found::NoPose || found == socket_reads::Found::OtherRoot) {
		socket_reads::Miss miss;
		miss.frame = read.frame;
		miss.unit = read.unit;
		miss.from = read.from;
		miss.reason = found == socket_reads::Found::NoPose ? socket_reads::MissReason::NeverSeenPose :
			!rebuilding ? socket_reads::MissReason::RebuildingOff : socket_reads::MissReason::OtherRootNotCertifiable;
		snprintf(miss.socket, sizeof(miss.socket), "%s", SocketName(socket).c_str());
		{
			std::lock_guard lock(s_mutex);
			s_misses.Add(miss);
			// The update wants a drawn frame (SfvAdapter: a pose hold).
			if (s_updateReads.misses++ == 0) {
				s_updateReads.first = miss;
				s_updateReads.side = read.side;
			}
		}
		LogRead(read, MissText(found, placed, reading.model, rebuilding, socket_reads::RebuildableSocket(socket)), live.xyzw);
		return;
	}
	memcpy(out, kept.xyzw, sizeof(kept.xyzw));
	if (s_log) {
		char text[128];
		snprintf(text, sizeof(text), "(stale pose %.5f %.5f %.5f; %s)", live.xyzw[0], live.xyzw[1], live.xyzw[2],
			found == socket_reads::Found::Exact ? "the straight read of this pose and root taken" : "rebuilt at this root, certain");
		LogRead(read, text, kept.xyzw);
	}
}

// The units' meshes: each unit's components logged once a battle, and the
// times the LOD had to be forced again after that (a few, then quiet).
std::atomic<bool> s_meshesLogged[kMostFighterUnits];
std::atomic<int> s_reforced;
constexpr int kMostReforcedLines = 8;

// A component the game would use: not marked unusable, and of a class with
// the skeletal mesh component cast flag (rva 0xE5F7D0, 0xE58F10).
bool UsableSkeletalMesh(const uint8_t* component)
{
	int32_t flags = 0;
	const uint8_t* cls = nullptr;
	uint64_t cast = 0;
	return component && TryRead(component + sfv::mesh::kObjectFlags, &flags) && (flags & sfv::mesh::kUnusable) == 0 &&
		TryRead(component + sfv::mesh::kObjectClass, &cls) && cls && TryRead(cls + sfv::mesh::kClassCastFlags, &cast) &&
		(cast & sfv::mesh::kSkeletalMeshComponentFlag) != 0;
}

int ForceOne(uint8_t* component, int which, std::vector<socket_reads::MeshLod>* seen)
{
	namespace m = sfv::mesh;
	if (!UsableSkeletalMesh(component)) return 0;
	socket_reads::MeshLod lod;
	lod.component = which;
	if (!TryRead(component + m::kForcedLodModel, &lod.forced) || !TryRead(component + m::kMinLodModel, &lod.minLod) ||
		!TryRead(component + m::kPredictedLodLevel, &lod.predicted) || !TryRead(component + m::kUpdateFlag, &lod.updateFlag)) {
		return 0;
	}
	if (lod.forced != m::kForceLodZero) lod.written = TryWrite(component + m::kForcedLodModel, m::kForceLodZero);
	// A mesh that only ticks its pose when rendered would not be evaluated
	// while the window is minimised or occluded: the sockets would read the
	// last drawn pose.
	if (lod.updateFlag != m::kAlwaysTickPoseAndRefreshBones) {
		lod.flagWritten = TryWrite(component + m::kUpdateFlag, m::kAlwaysTickPoseAndRefreshBones);
	}
	if (seen) seen->push_back(lod);
	return lod.written || lod.flagWritten ? 1 : 0;
}

std::string Describe(const std::vector<socket_reads::MeshLod>& seen)
{
	std::string text;
	for (const socket_reads::MeshLod& lod : seen) {
		char one[160];
		const std::string kind = lod.component == 0 ? "mesh" : lod.component == 1 ? "second mesh" : "layer " + std::to_string(lod.component - 2);
		snprintf(one, sizeof(one), "%s%s LOD %d (forced %d, min %d, update flag %u)%s%s", text.empty() ? "" : "; ", kind.c_str(),
			lod.predicted, lod.forced, lod.minLod, (unsigned)lod.updateFlag, lod.written ? ", now LOD 0" : "",
			lod.flagWritten ? ", now ticking every frame" : "");
		text += one;
	}
	return text;
}

// The float at a place in the order of all finite floats, counted from +0:
// negative places are the negative floats (-0 is not one), so a float's
// place grows with its value and a binary search over places is one over
// values.
float FromOrdered(int64_t ordered)
{
	return ordered < 0 ? std::bit_cast<float>(0x80000000u | (uint32_t)-ordered) : std::bit_cast<float>((uint32_t)ordered);
}

// The largest finite float, ordered.
constexpr int64_t kMostOrdered = 0x7F7FFFFF;

} // namespace

void KeepFighterMeshesAtLodZero(uint8_t* const* visualParts, int count)
{
	for (int unit = 0; unit < count && unit < kMostFighterUnits; unit++) {
		uint8_t* actor = nullptr;
		if (!visualParts[unit] || !TryRead(visualParts[unit] + sfv::visual::kActor, &actor) || !actor) continue;
		if (!s_meshesLogged[unit].load()) {
			std::vector<socket_reads::MeshLod> seen;
			socket_reads::ForceLodZero(actor, &seen);
			if (seen.empty()) continue; // the actor's meshes are not there yet: look again next update
			s_meshesLogged[unit].store(true);
			// "fighter k": the battle's order of its fighter units (CharaUnit[k]), not the side.
			Log("meshes: fighter %d: %zu skeletal mesh component(s): %s", unit, seen.size(), Describe(seen).c_str());
			continue;
		}
		const int written = socket_reads::ForceLodZero(actor, nullptr);
		if (written && s_reforced.fetch_add(1) < kMostReforcedLines) {
			Log("meshes: fighter %d: %d component(s) given LOD 0 and the pose tick again (something reset ForcedLodModel or the update flag)", unit, written);
		}
	}
}

namespace socket_reads {

int ForceLodZero(uint8_t* actor, std::vector<MeshLod>* seen)
{
	namespace v = sfv::visual;
	if (!actor) return 0;
	int written = 0;
	uint8_t* component = nullptr;
	if (TryRead(actor + v::kActorMesh, &component)) written += ForceOne(component, 0, seen);
	component = nullptr;
	if (TryRead(actor + v::kActorSecondMesh, &component)) written += ForceOne(component, 1, seen);
	uint8_t* layers = nullptr;
	int32_t count = 0;
	if (TryRead(actor + v::kActorLayers, &layers) && TryRead(actor + v::kActorLayerCount, &count) && layers && count > 0 &&
		count <= v::kMostLayers) {
		for (int32_t i = 0; i < count; i++) {
			component = nullptr;
			if (TryRead(layers + sizeof(uint8_t*) * (size_t)i, &component)) written += ForceOne(component, 2 + i, seen);
		}
	}
	return written;
}

bool EveryTick(uint32_t returnRva)
{
	return std::find(std::begin(kNotTheFights), std::end(kNotTheFights), returnRva) != std::end(kNotTheFights);
}

uint64_t NameKey(const wchar_t* name)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (int i = 0; name && i < 64; i++) {
		wchar_t c = 0;
		if (!TryRead(name + i, &c) || !c) break;
		h = (h ^ (uint64_t)(uint16_t)c) * 0x100000001b3ull;
	}
	return h;
}

namespace {

// Each run's bytes, then its size, into the running hash: a missing run and
// an empty one hash alike, and two runs cannot trade bytes unseen.
uint64_t Mix(uint64_t h, const uint8_t* bytes, size_t size)
{
	if (bytes && size) h = XXH3_64bits_withSeed(bytes, size, h);
	const uint64_t n = bytes ? size : 0;
	return XXH3_64bits_withSeed(&n, sizeof(n), h);
}

// A run's bytes with every 8-byte-aligned word that looks like an address
// read as zero, as the state hashes do (StateLayout::ZeroAddresses: aligned
// by the word's real address, so the same words are zeroed on every copy): a
// heap pointer in the run differs between a play and its replay.
uint64_t MixWithoutAddresses(uint64_t h, const uint8_t* bytes, size_t size)
{
	if (!bytes || !size) return Mix(h, bytes, size);
	thread_local std::vector<uint8_t> masked;
	masked.assign(bytes, bytes + size);
	const size_t first = (8 - (reinterpret_cast<uintptr_t>(bytes) & 7)) & 7;
	for (size_t at = first; at + 8 <= size; at += 8) {
		uint64_t word;
		memcpy(&word, masked.data() + at, sizeof(word));
		if (LooksLikeAddress(word)) memset(masked.data() + at, 0, sizeof(word));
	}
	return Mix(h, masked.data(), size);
}

} // namespace

uint64_t MakePoseKey(const Run motion[2], int32_t facing, bool tiny)
{
	uint64_t h = 0x9E3779B97F4A7C15ull;
	for (int i = 0; i < 2; i++) h = MixWithoutAddresses(h, motion[i].bytes, motion[i].size);
	h = Mix(h, reinterpret_cast<const uint8_t*>(&facing), sizeof(facing));
	const uint8_t small = tiny ? 1 : 0;
	h = Mix(h, &small, sizeof(small));
	return h ? h : 1;
}

uint64_t MakeRootKey(const Run& unit, const Run& place)
{
	namespace f = sfv::fighter;
	uint64_t h = 0xC2B2AE3D27D4EB4Full;
	h = Mix(h, unit.bytes, unit.size);
	// The place run without its bone 28 words, in two pieces; a run shorter
	// than the bone's words is hashed whole (never in a battle: RegionNames'
	// size checks would show it).
	if (place.bytes && place.size >= f::kPlaceBoneTo) {
		h = Mix(h, place.bytes, f::kPlaceBoneFrom);
		h = Mix(h, place.bytes + f::kPlaceBoneTo, place.size - f::kPlaceBoneTo);
	} else {
		h = Mix(h, place.bytes, place.size);
	}
	return h ? h : 1;
}

size_t KeyHash::operator()(const Key& k) const
{
	return (size_t)(k.pose ^ (k.socket * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)k.unit << 59));
}

bool Position::operator==(const Position& o) const
{
	return memcmp(xyzw, o.xyzw, sizeof(xyzw)) == 0;
}

Position FightPosition(const float socket[3])
{
	// The game's three multiplies, each of one float by a float constant
	// (sfv::kCentimetresToFightBits), in its order of axes.
	const float toFight = FloatOf(sfv::kCentimetresToFightBits);
	const float toFightNegated = FloatOf(sfv::kCentimetresToFightNegatedBits);
	Position p;
	p.xyzw[0] = socket[0] * toFight;
	p.xyzw[1] = socket[2] * toFight;
	p.xyzw[2] = socket[1] * toFightNegated;
	p.xyzw[3] = 1.0f;
	return p;
}

bool SameTurnAndScale(const ModelTransform& a, const ModelTransform& b)
{
	return memcmp(a.rotation, b.rotation, sizeof(a.rotation)) == 0 && memcmp(a.scale, b.scale, 3 * sizeof(float)) == 0;
}

// MSVC may fuse a multiply and an add into one fma where the target has it;
// the grid's two steps must be two roundings, as on every copy.
#pragma fp_contract(off)
float SnapToGrid(float v)
{
	if (!isfinite(v)) return v;
	const float scaled = v * static_cast<float>(kGridSteps); // exact: a power of two
	if (!isfinite(scaled)) return v;
	const float stepped = floorf(scaled + 0.5f);
	return stepped * (1.0f / static_cast<float>(kGridSteps)); // exact too
}
#pragma fp_contract(on)

Position SnapPosition(Position p)
{
	for (int axis = 0; axis < 3; axis++) p.xyzw[axis] = SnapToGrid(p.xyzw[axis]);
	return p;
}

bool RebuildableSocket(const wchar_t* name)
{
	return name && name[0] && name[1] == L'_' && (name[0] == L'b' || name[0] == L'g');
}

bool CertifiedSum(float recordedSum, float recordedRoot, float root, float* out)
{
	if (!isfinite(recordedSum) || !isfinite(recordedRoot) || !isfinite(root)) return false;
	// fl(r + recordedRoot) does not decrease as r grows, so the r giving
	// recordedSum are one run of floats: the least with a sum not below it,
	// and the greatest with a sum not above it. Each sum is one single
	// precision add (an addss on x64; /fp:precise keeps it that), as the
	// engine's own.
	const auto sumAt = [](int64_t r, float to) { return FromOrdered(r) + to; };
	int64_t lo = -kMostOrdered, hi = kMostOrdered;
	while (lo < hi) { // the least r with fl(r + recordedRoot) >= recordedSum
		const int64_t mid = lo + (hi - lo) / 2;
		if (sumAt(mid, recordedRoot) >= recordedSum) hi = mid;
		else lo = mid + 1;
	}
	const int64_t least = lo;
	lo = -kMostOrdered;
	hi = kMostOrdered;
	while (lo < hi) { // the greatest r with fl(r + recordedRoot) <= recordedSum
		const int64_t mid = hi - (hi - lo) / 2;
		if (sumAt(mid, recordedRoot) <= recordedSum) lo = mid;
		else hi = mid - 1;
	}
	const int64_t greatest = lo;
	// No r gives recordedSum exactly: not the engine's add of this root.
	if (least > greatest || std::bit_cast<uint32_t>(sumAt(least, recordedRoot)) != std::bit_cast<uint32_t>(recordedSum)) return false;
	const float low = sumAt(least, root);
	const float high = sumAt(greatest, root);
	if (std::bit_cast<uint32_t>(low) != std::bit_cast<uint32_t>(high) || !isfinite(low)) return false;
	*out = low;
	return true;
}

bool Rebuild(const ModelReading& recorded, const ModelTransform& now, Position* out, bool grid)
{
	if (!SameTurnAndScale(recorded.mesh, now)) return false;
	float socket[3];
	for (int axis = 0; axis < 3; axis++) {
		if (!CertifiedSum(recorded.socket[axis], recorded.mesh.translation[axis], now.translation[axis], &socket[axis])) return false;
	}
	*out = FightPosition(socket);
	if (grid) *out = SnapPosition(*out);
	return true;
}

Recorded Cache::Record(const Key& key, const Reading& reading)
{
	auto it = values_.find(key);
	if (it == values_.end()) {
		if (capacity_ == 0) return Recorded::New;
		while (values_.size() >= capacity_ && !order_.empty()) {
			values_.erase(order_.front());
			order_.pop_front();
		}
		Slot slot;
		slot.readings[0] = reading;
		slot.count = 1;
		values_.emplace(key, slot);
		order_.push_back(key);
		return Recorded::New;
	}
	Slot& slot = it->second;
	// What a stale read would have been given here from another root, against
	// what the engine gives: the arithmetic the rebuild assumes, checked on
	// every fresh read that can check it.
	bool disagrees = false;
	if (reading.model && !slot.neverRebuild) {
		for (size_t i = 0; i < slot.count; i++) {
			const Reading& other = slot.readings[i];
			Position rebuilt;
			if (other.root == reading.root || !other.model || !Rebuild(other.at, reading.at.mesh, &rebuilt, grid_)) continue;
			if (!(rebuilt == reading.out)) disagrees = true;
		}
		if (disagrees) {
			// Nothing is rebuilt for this pose again, and what the contradicted
			// rebuilds came from goes: only the readings at this root stay.
			slot.neverRebuild = true;
			neverRebuilt_++;
			size_t kept = 0;
			for (size_t i = 0; i < slot.count; i++) {
				if (slot.readings[i].root == reading.root) slot.readings[kept++] = slot.readings[i];
			}
			slot.count = kept;
			slot.next = 0;
		}
	}
	for (size_t i = 0; i < slot.count; i++) {
		Reading& same = slot.readings[i];
		if (same.root != reading.root) continue;
		const bool other = !(same.out == reading.out);
		same = reading;
		if (disagrees) return Recorded::RebuildDisagrees;
		return other ? Recorded::Conflicting : Recorded::Again;
	}
	if (slot.count < kRootsPerPose) {
		slot.readings[slot.count++] = reading;
	} else {
		slot.readings[slot.next] = reading;
		slot.next = (slot.next + 1) % kRootsPerPose;
	}
	return disagrees ? Recorded::RebuildDisagrees : Recorded::New;
}

Found Cache::Find(const Key& key, uint64_t root, const ModelTransform* now, Position* out)
{
	const auto it = values_.find(key);
	if (it == values_.end()) return Found::NoPose;
	Slot& slot = it->second;
	for (size_t i = 0; i < slot.count; i++) {
		if (slot.readings[i].root != root) continue;
		*out = slot.readings[i].out;
		return Found::Exact;
	}
	if (!now || slot.neverRebuild) return Found::OtherRoot;
	// Every reading that certifies a value must certify the same one; two
	// that do not agree mean the arithmetic is not the engine's for this
	// pose, and it is not rebuilt again this battle.
	bool any = false;
	Position first;
	for (size_t i = 0; i < slot.count; i++) {
		Position rebuilt;
		if (!slot.readings[i].model || !Rebuild(slot.readings[i].at, *now, &rebuilt, grid_)) continue;
		if (any && !(rebuilt == first)) {
			slot.neverRebuild = true;
			neverRebuilt_++;
			return Found::OtherRoot;
		}
		first = rebuilt;
		any = true;
	}
	if (!any) return Found::OtherRoot;
	*out = first;
	return Found::Rebuilt;
}

const char* MissReasonName(MissReason reason)
{
	switch (reason) {
	case MissReason::NeverSeenPose: return "never-seen pose";
	case MissReason::OtherRootNotCertifiable: return "other root not certifiable";
	case MissReason::RebuildingOff: return "rebuilding off";
	}
	return "?";
}

void MissRing::Add(const Miss& miss)
{
	misses_[next_] = miss;
	next_ = (next_ + 1) % kMostMisses;
	if (count_ < kMostMisses) count_++;
}

std::vector<Miss> MissRing::Between(int32_t first, int32_t last) const
{
	std::vector<Miss> out;
	// Oldest first: from where the ring would write next when it is full.
	const size_t start = count_ < kMostMisses ? 0 : next_;
	for (size_t i = 0; i < count_; i++) {
		const Miss& miss = misses_[(start + i) % kMostMisses];
		if (miss.frame >= first && miss.frame <= last) out.push_back(miss);
	}
	return out;
}

void Cache::Clear()
{
	values_.clear();
	order_.clear();
	neverRebuilt_ = 0;
}

bool Cache::Rebuilding(const Key& key) const
{
	const auto it = values_.find(key);
	return it == values_.end() || !it->second.neverRebuild;
}

std::string HoldsText(const HoldCounts& c)
{
	using pose_hold::Refusal;
	const auto failed = [&](Refusal why) { return (unsigned long long)c.failed[(int)why]; };
	unsigned long long total = 0;
	for (uint64_t n : c.failed) total += n;
	char text[512];
	snprintf(text, sizeof(text),
		"%llu pose hold(s), %llu gave a fresh read; %llu failed (state not kept %llu, held frame %llu, not fresh after the "
		"hold %llu, chain limit %llu, refused by GGPO %llu, first frame %llu)",
		(unsigned long long)c.holds, (unsigned long long)c.fresh, total, failed(Refusal::StateNotKept),
		failed(Refusal::HeldFrame), failed(Refusal::NotFreshAfterHold), failed(Refusal::ChainLimit),
		failed(Refusal::Refused), failed(Refusal::FirstFrame));
	return text;
}

PoseFreshness::Begun PoseFreshness::Begin(int32_t inputFrame)
{
	Begun begun;
	begun.fresh = fresh_;
	begun.placed = placed_;
	if (awaitingHold_) {
		begun.afterHold = fresh_ && inputFrame == heldFrame_ ? AfterHold::Fresh : AfterHold::NotFresh;
		awaitingHold_ = false;
		heldFrame_ = -1;
	}
	return begun;
}

std::string CountsText(const Counts& c, size_t kept, size_t neverRebuilt)
{
	char text[640];
	char neverText[64];
	snprintf(neverText, sizeof(neverText), "; %zu pose(s) never rebuilt", neverRebuilt);
	snprintf(text, sizeof(text),
		"%llu fresh read(s) recorded; stale read(s): %llu given the straight read of their pose and root, %llu rebuilt at their "
		"root, %llu of a pose never read fresh, %llu of a pose read fresh at other roots only; %llu conflicting (%llu of them a "
		"rebuild's), %llu without a key; %zu pose(s) kept%s",
		(unsigned long long)c.recorded, (unsigned long long)c.exact, (unsigned long long)c.rebuilt, (unsigned long long)c.noPose,
		(unsigned long long)c.otherRoot, (unsigned long long)(c.conflicting + c.disagreeing), (unsigned long long)c.disagreeing,
		(unsigned long long)c.unkeyed, kept, neverRebuilt ? neverText : "");
	return text;
}

} // namespace socket_reads

void NoteSocketUpdate(const SocketUpdate& update)
{
	std::lock_guard lock(s_mutex);
	s_update = update;
	s_updateReads = {};
}

socket_reads::UpdateReads TakeUpdateReads()
{
	std::lock_guard lock(s_mutex);
	return std::exchange(s_updateReads, socket_reads::UpdateReads{});
}

void CountPoseHold()
{
	std::lock_guard lock(s_mutex);
	s_holds.holds++;
}

void CountPoseHoldFresh()
{
	std::lock_guard lock(s_mutex);
	s_holds.fresh++;
}

void CountPoseHoldFailed(pose_hold::Refusal why)
{
	const int index = (int)why;
	if (index <= 0 || index >= pose_hold::kRefusals) return;
	std::lock_guard lock(s_mutex);
	s_holds.failed[index]++;
}

std::string PoseHoldsSummary()
{
	std::lock_guard lock(s_mutex);
	return socket_reads::HoldsText(s_holds);
}

void ForgetSocketReads()
{
	{
		std::lock_guard lock(s_mutex);
		s_cache.Clear();
		s_misses.Clear();
		s_counts = {};
		s_holds = {};
		s_update = {};
		s_updateReads = {};
	}
	for (std::atomic<bool>& logged : s_meshesLogged) logged.store(false);
	s_reforced.store(0);
	s_conflictLines.store(0);
}

std::string SocketReadsSummary()
{
	std::lock_guard lock(s_mutex);
	return socket_reads::CountsText(s_counts, s_cache.Size(), s_cache.NeverRebuilt());
}

std::vector<socket_reads::Miss> SocketMissesBetween(int32_t first, int32_t last)
{
	std::lock_guard lock(s_mutex);
	return s_misses.Between(first, last);
}

bool InstallSocketReads(uintptr_t imageBase, bool log)
{
	s_base = imageBase;
	s_log = log;
	// Called, not hooked: the build's check (BuildCheck, sfv::kSites) has read their bytes.
	s_makeName = reinterpret_cast<MakeNameFn>(imageBase + sfv::kMakeName.rva);
	s_socketTransform = reinterpret_cast<SocketTransformFn>(imageBase + sfv::kSocketTransform.rva);
	const bool ok = HookCodeSite(imageBase, sfv::kSocketWorldPosition, reinterpret_cast<void*>(&h_Socket),
		reinterpret_cast<void**>(&o_socket));
	Log("socket: %s%s", ok ? "the fight's reads of the fighters' models take a straight play's pose" : "the socket read NOT hooked; replayed frames read the last drawn pose",
		ok && log ? "; every read logged (-sfve_socket_log)" : "");
	return ok;
}

} // namespace sfve::mod
