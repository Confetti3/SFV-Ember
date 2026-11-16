#pragma once

// A launch's balance versions in the game (BalanceVersionRules.hxx has the
// rule; docs/design/beta10/desync.md). The jump writes the picks into the
// game's mode data, whose records carry no version; the battle's resource
// load then resolves each player record's version (sfv::kResolvePlayerVersion)
// right before it adds the fighter's assets. Armed with the launch's
// versions, the hook writes each record's version, by the fighter the
// record holds, just before the game resolves it, so every copy's battle
// loads the version the launch named and never what the record kept from
// the previous battle or the copy's own newest.

#include "mod/GameHooks.hxx"

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

struct LaunchVersion {
	std::wstring fighter; // the game's code
	int32_t version = 0;
};

// Game thread. From the jump's second step until the launched battle is
// there (or the launch failed): every player record the game resolves
// whose fighter one of `versions` names gets that version first. Both
// players' fighters are named; a mirror names one code once.
void ArmLaunchVersions(std::vector<LaunchVersion> versions);
void DisarmLaunchVersions();

// The resolve's hook, added to the battle jump's set (VersusLaunch): a
// launch without it would play each copy's own resolution.
void AddBalanceVersionHook(HookTransaction& hooks, uintptr_t imageBase);

} // namespace sfve::mod
