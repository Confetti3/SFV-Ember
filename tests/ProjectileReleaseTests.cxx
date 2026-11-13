// Which thread the battle's projectile tasks run on in the modes that roll
// back (mod/ProjectileRelease): the release of dead projectiles destroys live
// UE actors there, so its task must run on the battle's thread, not on a job
// worker beside the other groups' releases.

#include <doctest/doctest.h>

#include "mod/ProjectileRelease.hxx"

using sfve::mod::ProjectileTaskThread;

TEST_CASE("the projectile release's task runs on the battle's thread")
{
	CHECK(ProjectileTaskThread("Projectile PreMain", 0) == 1);
	CHECK(ProjectileTaskThread("Projectile PreMain", 1) == 1);
	// Its name read from the game's string, not the same pointer as ours.
	const char name[] = "Projectile PreMain";
	CHECK(ProjectileTaskThread(name, 0) == 1);
}

TEST_CASE("every other battle task keeps the thread the game asked for")
{
	// The group's other tasks as the game registers them; the visuals' already run on the battle's thread.
	CHECK(ProjectileTaskThread("Projectile Prepare", 0) == 0);
	CHECK(ProjectileTaskThread("Projectile PostMain", 0) == 0);
	CHECK(ProjectileTaskThread("Projectile Visual PreTick", 1) == 1);
	CHECK(ProjectileTaskThread("Projectile Visual PostUpdate", 1) == 1);
	// A name only beginning like it, an empty one and none (the game's "UNNAMED").
	CHECK(ProjectileTaskThread("Projectile PreMain2", 0) == 0);
	CHECK(ProjectileTaskThread("", 0) == 0);
	CHECK(ProjectileTaskThread(nullptr, 0) == 0);
	CHECK(ProjectileTaskThread(nullptr, 1) == 1);
}

TEST_CASE("a battle a session plays places every task it registers, also after the launch stopped loading")
{
	sfve::mod::SessionBattleLatch latch;
	// Nothing registered yet: not a session's.
	CHECK_FALSE(latch.Latched());
	// Registered while the launch loads it (SessionBattleSetUp true).
	CHECK(latch.Registering(true));
	CHECK(latch.Registering(true));
	// The first frame of the launch's battle: Forget with the launch still loading keeps it.
	latch.NewBattle(true);
	CHECK(latch.Latched());
	// Bound: the launch no longer loads, a later registration (a lazily created unit, a rematch in place) still counts.
	CHECK(latch.Registering(false));
	// The next battle frame with no launch loading (Training after the match): over.
	latch.NewBattle(false);
	CHECK_FALSE(latch.Latched());
	CHECK_FALSE(latch.Registering(false));
	// Training's own tasks never latch.
	CHECK_FALSE(latch.Registering(false));
	CHECK_FALSE(latch.Latched());
}
