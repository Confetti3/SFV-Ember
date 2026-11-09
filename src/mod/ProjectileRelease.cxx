#include "mod/ProjectileRelease.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

namespace {

using ReleasableFn = uint64_t (*)(uint8_t* projectile);

ReleasableFn o_releasable;

uint64_t h_Releasable(uint8_t* projectile)
{
	if (o_releasable(projectile)) return 1; // no visual part, or its actor is gone
	uint8_t* visual = GameFindPart(projectile, sfv::projectilevisual::kType);
	if (!visual) return 1;
	const int32_t state = Field<int32_t>(visual, sfv::projectilevisual::kState);
	return state == sfv::projectilevisual::kStateDied || state == sfv::projectilevisual::kStateFading;
}

} // namespace

void AddProjectileReleaseHook(HookTransaction& hooks, uintptr_t imageBase)
{
	hooks.Add(imageBase, sfv::kProjectileReleasable, (void*)h_Releasable, (void**)&o_releasable);
}

} // namespace sfve::mod
