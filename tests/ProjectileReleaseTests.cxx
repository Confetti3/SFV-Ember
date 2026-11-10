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
