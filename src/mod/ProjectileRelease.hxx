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
// That release runs in the "Projectile PreMain" task, which the game lets its
// job workers run beside the battle's thread and the other groups' tasks: it
// never touched UE there, since a projectile whose visual reached 0 has no
// actor left. Released at 3 or 4 its actor is live, and destroying it on a
// worker hides the actor's components, which edits the UWorld's end-of-frame
// update lists (UE's MarkRenderStateDirty), lists only the game thread may
// touch. Another edit on another thread raced on them (most likely another
// group's release on the same tick): it emptied and freed a list a worker
// was searching (an access violation at rva 0x1B5BC0B, on a job worker, 64 s
// into a netplay match). So in these modes PreMain runs on the battle's
// thread, as the game's own visual tasks do.
//
// Hooked only in the modes that roll back (synctest, netplay); offline play
// is left as the game does it.
//
// The same task hook places the tick's other tasks (ModConfig's
// ChooseTickTasks): in a battle a session plays (a room's match, a watcher's,
// a replay's, the synctest's) every tick and post-tick task runs on the
// battle's thread in list order, so no two fighters' tasks race on a shared
// write; Training and the player's own battles keep the game's placement.

#include <stdint.h>
#include <string.h>

namespace sfve::mod {

class HookTransaction;

// The task that releases a group's dead projectiles (sfv::kAddBattleTask).
inline constexpr char kProjectilePreMainTask[] = "Projectile PreMain";

// The gameThread argument a battle task is registered with: 1 for the
// projectile release's task, as asked otherwise.
inline int32_t ProjectileTaskThread(const char* name, int32_t gameThread)
{
	return name && strcmp(name, kProjectilePreMainTask) == 0 ? 1 : gameThread;
}

// Whether a battle is one a session plays (the tick's tasks then run on the
// battle's thread), decided once from the launch and kept for the whole
// battle. A task registered after the battle binds (a lazily created unit, a
// rematch in place) still belongs to it, though the launch has stopped
// loading by then: SessionBattleSetUp alone would give the game's parallel
// placement to exactly those tasks.
class SessionBattleLatch {
public:
	// A task is being registered; `setUpNow`: SessionBattleSetUp for it
	// (true while the launch is loading the battle). Whether to place this
	// battle's tasks as a session's.
	bool Registering(bool setUpNow)
	{
		if (setUpNow) latched_ = true;
		return latched_;
	}
	// A new battle frame is seen (SfvAdapter::Forget). `launchLoading`: the
	// launch's battle is still loading, which the new battle's own tasks,
	// registered before its first frame, were placed by: keep it for them.
	void NewBattle(bool launchLoading)
	{
		if (!launchLoading) latched_ = false;
	}
	bool Latched() const { return latched_; }

private:
	bool latched_ = false;
};

// The process's latch, for AddBattleTask and SfvAdapter::Forget (the game thread).
inline SessionBattleLatch& BattleLatch()
{
	static SessionBattleLatch latch;
	return latch;
}

// Adds the release's hooks to the battle's set (dllmain) in the modes that
// roll back: without them two copies release projectiles on different frames,
// or release them off the battle's thread.
void AddProjectileReleaseHook(HookTransaction& hooks, uintptr_t imageBase);

} // namespace sfve::mod
