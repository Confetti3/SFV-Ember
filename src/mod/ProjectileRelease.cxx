#include "mod/ProjectileRelease.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

namespace sfve::mod {

namespace {

using ReleasableFn = uint64_t (*)(uint8_t* projectile);
using FindPartFn = GameSharedPtr* (*)(uint8_t* unit, GameSharedPtr* out, const void* type, uint32_t index);

ReleasableFn o_releasable;
FindPartFn s_findPart;
const void* s_visualType;

uint64_t h_Releasable(uint8_t* projectile)
{
	if (o_releasable(projectile)) return 1; // no visual part, or its actor is gone
	GameSharedPtr visual{};
	s_findPart(projectile, &visual, s_visualType, 0);
	// The projectile holds its parts; give back the reference the lookup took.
	if (visual.control) ReleaseKeepingAlive(visual.control);
	if (!visual.object) return 1;
	const int32_t state = Field<int32_t>(visual.object, sfv::projectilevisual::kState);
	return state == sfv::projectilevisual::kStateDied || state == sfv::projectilevisual::kStateFading;
}

} // namespace

bool InstallDeterministicProjectileRelease(uintptr_t imageBase)
{
	s_findPart = reinterpret_cast<FindPartFn>(imageBase + sfv::kFindPart.rva);
	s_visualType = reinterpret_cast<const void*>(imageBase + sfv::projectilevisual::kType);
	const bool ok = HookCodeSite(imageBase, sfv::kProjectileReleasable, (void*)h_Releasable, (void**)&o_releasable);
	if (ok) Log("projectiles: dead projectiles are released on the battle's clock");
	return ok;
}

} // namespace sfve::mod
