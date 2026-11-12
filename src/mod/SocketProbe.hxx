#pragma once

// Diagnostics for the fight's reads of a fighter's Unreal model
// (docs/design/beta8/desync.md). The fight places some projectiles and items
// at a socket of the model (sfv::kSocketWorldPosition), where the Unreal pose
// last left it: the pose of the last presented frame, which a rollback's
// replayed frames do not refresh. With -sfve_socket_log=1 (development) every
// such read is logged with the battle's input frame, whether the update
// replays a frame, the fighter's side, the socket's name, the call site and
// the position; the reads made every tick (bone 28 in Chara PreInput, the
// camera's handlers, CharaManager's) are left out. The same frame read
// twice, played and then replayed, with two positions is the pose the replay
// did not refresh.

#include <stdint.h>

namespace sfve::mod {

// The battle update about to run (SfvAdapter): the input unit's frame and
// whether the update replays a frame. Read by the hook during the update.
void NoteSocketProbeUpdate(int32_t inputFrame, bool replaying);

// Hooks the socket read; false when it could not be hooked. Once, from
// SfveStart, and only with -sfve_socket_log=1.
bool InstallSocketProbe(uintptr_t imageBase);

namespace socket_probe {

// A read from one of the call sites that run every tick, by the rva of its
// return address: not logged.
bool EveryTick(uint32_t returnRva);

} // namespace socket_probe

} // namespace sfve::mod
