#pragma once

// The fight's reads of a fighter's Unreal model (docs/design/beta8/desync.md).
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
// So each read on a fresh pose is recorded, keyed by the reading fighter's
// side, the socket and the fighter's fight state as the update began (a hash
// of its regions, SfvAdapter), and a read on a stale pose takes the recorded
// value for the same key: what a straight play of that state read. A read
// whose key was never seen on a fresh pose keeps the live value (counted as
// missed); a fresh read that finds its key with another value is counted as
// conflicting (the key would then not hold all the pose depends on). Reads
// the fight does not make (bone 28 every tick, the camera's, the visual
// tasks') are left as they are.
//
// Refreshing the pose before a replayed tick instead was looked at and left
// out: the pose comes from the engine's component tick (the mesh's virtual
// +0x7D0), which in SFV's layer class advances looping layers by a fixed 1/60
// per call and sets material parameters (rva 0xE62E70), so extra calls during
// a rollback would change the picture after it, and the tick expects the
// engine's tick function.
//
// With -sfve_socket_log=1 (development) every fight read is logged too.

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <string>
#include <unordered_map>

namespace sfve::mod {

// The battle update about to run (SfvAdapter::RunUpdate, the game thread),
// read by the socket reads it makes.
struct SocketUpdate {
	int32_t inputFrame = -1;
	bool replaying = false;      // a rollback's replayed frame (for the log)
	bool fresh = false;          // the models show the state this update starts from
	uint64_t fighterState[2] = {}; // each side's fight state as the update starts; 0 unknown
};
void NoteSocketUpdate(const SocketUpdate& update);
// A new battle: every recorded read is forgotten.
void ForgetSocketReads();
// Hooks the socket read; false when it could not be hooked (the reads then
// stay the game's). Once, from SfveStart. `log`: -sfve_socket_log.
bool InstallSocketReads(uintptr_t imageBase, bool log);
// The counts so far, for the log (SfvAdapter, with the keys' cost).
std::string SocketReadsSummary();

namespace socket_reads {

// A read from a call site that is not the fight's, by the rva of its return
// address: left as it is, not logged.
bool EveryTick(uint32_t returnRva);

// A socket's name as a number (FNV-1a of its characters, at most 64).
uint64_t NameKey(const wchar_t* name);

struct Key {
	uint64_t state = 0; // the fighter's fight state as the update began
	uint64_t socket = 0; // NameKey
	int32_t side = -1;
	bool operator==(const Key& o) const { return state == o.state && socket == o.socket && side == o.side; }
};

struct KeyHash {
	size_t operator()(const Key& k) const;
};

// What the read wrote: x, y, z in the fight's units, w.
struct Position {
	float xyzw[4] = {};
	bool operator==(const Position& o) const;
};

// Recorded reads, the oldest dropped past `capacity`. Not thread-safe: the
// hook holds a lock.
class Cache {
public:
	explicit Cache(size_t capacity = 8192) : capacity_(capacity) {}
	// A fresh read. True when the key was known with another value (the new
	// one is kept).
	bool Record(const Key& key, const Position& value);
	// The recorded value for a stale read, if any.
	bool Find(const Key& key, Position* out) const;
	void Clear();
	size_t Size() const { return values_.size(); }

private:
	size_t capacity_;
	std::unordered_map<Key, Position, KeyHash> values_;
	std::deque<Key> order_; // insertion order, for dropping the oldest
};

} // namespace socket_reads

} // namespace sfve::mod
