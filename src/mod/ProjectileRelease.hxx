#pragma once

// Releasing dead projectiles on the battle's clock.
//
// A projectile's group keeps its 24 projectiles in three lists: free (taken
// last in, first out), active, and retired (dead, still fading out). A
// retired projectile returns to the free list once ProjectileReleasable
// says so, which the game decides from its visual's state reaching 0, and
// that happens only after the UE particle actor has finished on the engine's
// frame clock (sfv::projectilevisual). A rollback replays battle frames later
// in wall-clock time than they were played, when the actor has long
// finished, so the replay releases the projectile earlier than the played
// frames did; two PCs at different frame rates release it on different
// battle frames even without a rollback. Which projectile the next spawn
// takes, and so which object's fight state it writes, then differs.
//
// While a battle may roll back, a projectile is releasable from the battle
// tick's own states as well: once it died (3) or began to fade (4). The game's
// release path destroys the actor, so its fade-out is cut short.
//
// Hooked only in the modes that roll back (synctest, netplay); offline play
// is left as the game does it.

#include <stdint.h>

namespace sfve::mod {

class HookTransaction;

// Adds the release's hook to the battle's set (dllmain) in the modes that
// roll back: without it two copies release projectiles on different frames.
void AddProjectileReleaseHook(HookTransaction& hooks, uintptr_t imageBase);

} // namespace sfve::mod
