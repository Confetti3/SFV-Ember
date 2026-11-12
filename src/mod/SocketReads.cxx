#include "mod/SocketReads.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <intrin.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>

namespace sfve::mod {

namespace {

// sfv::kSocketWorldPosition: (CharaVisualPart*, socket name, float out[4],
// flag); the flag is passed on untouched.
using SocketFn = void (*)(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag);

SocketFn o_socket;
uintptr_t s_base;
bool s_log;

// The update now running (NoteSocketUpdate, before the update's tasks start).
std::atomic<int32_t> s_frame{-1};
std::atomic<bool> s_replaying;
std::atomic<bool> s_fresh;
std::atomic<uint64_t> s_state[2];

// The reads run on the battle's job threads, both fighters' at once.
std::mutex s_mutex;
socket_reads::Cache s_cache;
std::atomic<uint64_t> s_recorded, s_served, s_missed, s_conflicting, s_unkeyed;
std::atomic<int> s_lines;
// A runaway caller cannot fill the disk: the log stops here.
constexpr int kMostLines = 20000;

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

// The side of the fighter a visual part belongs to, or -1.
int SideOf(const uint8_t* visualPart)
{
	const uint8_t* fighter = nullptr;
	int side = -1;
	if (!visualPart || !TryRead(visualPart + sfv::visual::kOwnerObject, &fighter) || !fighter ||
		!TryRead(fighter + sfv::fighter::kSide, &side)) {
		return -1;
	}
	return side;
}

void LogRead(const char* what, int side, const wchar_t* socket, uint32_t from, const float* p)
{
	if (!s_log || s_lines.fetch_add(1) >= kMostLines) return;
	const std::string name = SocketName(socket);
	if (!p) {
		Log("socket: frame %d%s, P%d, socket %s, from rva 0x%X: %s", s_frame.load(), s_replaying.load() ? " replayed" : "", side + 1,
			name.c_str(), from, what);
		return;
	}
	Log("socket: frame %d%s, P%d, socket %s, from rva 0x%X: %.5f %.5f %.5f %s", s_frame.load(),
		s_replaying.load() ? " replayed" : "", side + 1, name.c_str(), from, p[0], p[1], p[2], what);
}

void h_Socket(uint8_t* visualPart, const wchar_t* socket, float* out, uint64_t flag)
{
	o_socket(visualPart, socket, out, flag);
	const uint32_t from = (uint32_t)(reinterpret_cast<uintptr_t>(_ReturnAddress()) - s_base);
	if (socket_reads::EveryTick(from)) return;
	const int side = SideOf(visualPart);
	// The read writes `out` whenever the part has an actor (rva 0x3919D0).
	const uint8_t* actor = nullptr;
	if (!out || !visualPart || !TryRead(visualPart + sfv::visual::kActor, &actor) || !actor) {
		LogRead("no model, nothing written", side, socket, from, nullptr);
		return;
	}
	socket_reads::Position live;
	memcpy(live.xyzw, out, sizeof(live.xyzw));
	const uint64_t state = side == 0 || side == 1 ? s_state[side].load() : 0;
	if (!state) {
		s_unkeyed++;
		LogRead("(no key: live)", side, socket, from, live.xyzw);
		return;
	}
	const socket_reads::Key key{state, socket_reads::NameKey(socket), side};
	if (s_fresh.load()) {
		bool conflicting = false;
		{
			std::lock_guard lock(s_mutex);
			conflicting = s_cache.Record(key, live);
		}
		s_recorded++;
		if (conflicting) s_conflicting++;
		LogRead(conflicting ? "(fresh; recorded, CONFLICTING with the value kept for this state)" : "(fresh; recorded)", side, socket,
			from, live.xyzw);
		return;
	}
	socket_reads::Position kept;
	bool found = false;
	{
		std::lock_guard lock(s_mutex);
		found = s_cache.Find(key, &kept);
	}
	if (!found) {
		s_missed++;
		LogRead("(stale pose; no straight read of this state: live)", side, socket, from, live.xyzw);
		return;
	}
	memcpy(out, kept.xyzw, sizeof(kept.xyzw));
	s_served++;
	if (s_log && !(kept == live)) {
		char text[96];
		snprintf(text, sizeof(text), "(stale pose %.5f %.5f %.5f; the straight read's taken)", live.xyzw[0], live.xyzw[1], live.xyzw[2]);
		LogRead(text, side, socket, from, kept.xyzw);
	} else {
		LogRead("(stale pose; the straight read's taken, the same)", side, socket, from, kept.xyzw);
	}
}

// The fighters' meshes: each side's components logged once a battle, and the
// times the LOD had to be forced again after that (a few, then quiet).
std::atomic<bool> s_meshesLogged[2];
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
	if (seen) seen->push_back(lod);
	return lod.written ? 1 : 0;
}

std::string Describe(const std::vector<socket_reads::MeshLod>& seen)
{
	std::string text;
	for (const socket_reads::MeshLod& lod : seen) {
		char one[112];
		const std::string kind = lod.component == 0 ? "mesh" : lod.component == 1 ? "second mesh" : "layer " + std::to_string(lod.component - 2);
		snprintf(one, sizeof(one), "%s%s LOD %d (forced %d, min %d, update flag %u)%s", text.empty() ? "" : "; ", kind.c_str(),
			lod.predicted, lod.forced, lod.minLod, (unsigned)lod.updateFlag, lod.written ? ", now LOD 0" : "");
		text += one;
	}
	return text;
}

} // namespace

void KeepFighterMeshesAtLodZero(uint8_t* const visualParts[2])
{
	for (int side = 0; side < 2; side++) {
		uint8_t* actor = nullptr;
		if (!visualParts[side] || !TryRead(visualParts[side] + sfv::visual::kActor, &actor) || !actor) continue;
		if (!s_meshesLogged[side].load()) {
			std::vector<socket_reads::MeshLod> seen;
			socket_reads::ForceLodZero(actor, &seen);
			if (seen.empty()) continue; // the actor's meshes are not there yet: look again next update
			s_meshesLogged[side].store(true);
			// "fighter k": the battle's order of its fighters (CharaUnit[k]), not the side.
			Log("meshes: fighter %d: %zu skeletal mesh component(s): %s", side, seen.size(), Describe(seen).c_str());
			continue;
		}
		const int written = socket_reads::ForceLodZero(actor, nullptr);
		if (written && s_reforced.fetch_add(1) < kMostReforcedLines) {
			Log("meshes: fighter %d: %d component(s) given LOD 0 again (something reset ForcedLodModel)", side, written);
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

size_t KeyHash::operator()(const Key& k) const
{
	return (size_t)(k.state ^ (k.socket * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)(uint32_t)k.side << 61));
}

bool Position::operator==(const Position& o) const
{
	return memcmp(xyzw, o.xyzw, sizeof(xyzw)) == 0;
}

bool Cache::Record(const Key& key, const Position& value)
{
	const auto it = values_.find(key);
	if (it != values_.end()) {
		const bool other = !(it->second == value);
		it->second = value;
		return other;
	}
	if (capacity_ == 0) return false;
	while (values_.size() >= capacity_ && !order_.empty()) {
		values_.erase(order_.front());
		order_.pop_front();
	}
	values_.emplace(key, value);
	order_.push_back(key);
	return false;
}

bool Cache::Find(const Key& key, Position* out) const
{
	const auto it = values_.find(key);
	if (it == values_.end()) return false;
	*out = it->second;
	return true;
}

void Cache::Clear()
{
	values_.clear();
	order_.clear();
}

} // namespace socket_reads

void NoteSocketUpdate(const SocketUpdate& update)
{
	s_frame.store(update.inputFrame);
	s_replaying.store(update.replaying);
	s_fresh.store(update.fresh);
	s_state[0].store(update.fighterState[0]);
	s_state[1].store(update.fighterState[1]);
}

void ForgetSocketReads()
{
	{
		std::lock_guard lock(s_mutex);
		s_cache.Clear();
	}
	s_meshesLogged[0].store(false);
	s_meshesLogged[1].store(false);
	s_reforced.store(0);
}

std::string SocketReadsSummary()
{
	size_t kept = 0;
	{
		std::lock_guard lock(s_mutex);
		kept = s_cache.Size();
	}
	char text[224];
	snprintf(text, sizeof(text),
		"%llu fresh read(s) recorded, %llu stale read(s) given the straight read, %llu stale read(s) of a state never read fresh, "
		"%llu conflicting, %llu without a key; %zu kept",
		(unsigned long long)s_recorded.load(), (unsigned long long)s_served.load(), (unsigned long long)s_missed.load(),
		(unsigned long long)s_conflicting.load(), (unsigned long long)s_unkeyed.load(), kept);
	return text;
}

bool InstallSocketReads(uintptr_t imageBase, bool log)
{
	s_base = imageBase;
	s_log = log;
	const bool ok = HookCodeSite(imageBase, sfv::kSocketWorldPosition, reinterpret_cast<void*>(&h_Socket),
		reinterpret_cast<void**>(&o_socket));
	Log("socket: %s%s", ok ? "the fight's reads of the fighters' models take the straight play's pose" : "the socket read NOT hooked; replayed frames read the last drawn pose",
		ok && log ? "; every read logged (-sfve_socket_log)" : "");
	return ok;
}

} // namespace sfve::mod
