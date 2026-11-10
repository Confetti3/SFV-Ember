#include "mod/ProjectileRelease.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/ModConfig.hxx"
#include "mod/SfvOffsets.hxx"

#include <map>
#include <string>

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

using AddBattleTaskFn = uint64_t (*)(void* list, const void* task, uint32_t order, uint32_t phases, const char* name,
	int32_t gameThread);

AddBattleTaskFn o_addBattleTask;

// Registrations of each fighter task name so far: the first of a battle is
// P1's (its unit is set up first), the second P2's.
std::map<std::string, int> s_fighterTaskCount;

// The scheduler (rva 0x4CD420) runs the tasks of one order group (order with
// its low 4 bits cleared) together: the gameThread ones on the battle's
// thread in list order, the others on whichever of that thread and the job
// workers takes them first; with no workers it runs every task in list order.
constexpr uint32_t kTickPhases = 2 | 4; // Tick and PostTick: the battle's simulation

uint64_t h_AddBattleTask(void* list, const void* task, uint32_t order, uint32_t phases, const char* name, int32_t gameThread)
{
	int32_t thread = ProjectileTaskThread(name, gameThread);
	const int placement = Config().battleTasks;
	if (placement >= 1 && (phases & kTickPhases) != 0) thread = 1;
	if (placement == 2 && name && strncmp(name, "Chara ", 6) == 0) {
		const int count = ++s_fighterTaskCount[name];
		if (count % 2 == 1 && (order & 0xF) < 0xF) order++; // P1's: after P2's in the same group
	}
	if (placement >= 1 && name && strcmp(name, "Chara PreInput") == 0) {
		Log("tasks: %s (order %08x, phases %u) placed on the battle's thread%s", name, order, phases,
			placement == 2 ? ", P2's fighter tasks first" : "");
	}
	return o_addBattleTask(list, task, order, phases, name, thread);
}

} // namespace

void AddProjectileReleaseHook(HookTransaction& hooks, uintptr_t imageBase)
{
	hooks.Add(imageBase, sfv::kProjectileReleasable, (void*)h_Releasable, (void**)&o_releasable);
	hooks.Add(imageBase, sfv::kAddBattleTask, (void*)h_AddBattleTask, (void**)&o_addBattleTask);
}

} // namespace sfve::mod
