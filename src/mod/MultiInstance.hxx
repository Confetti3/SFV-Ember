#pragma once

// Lets several copies of the game run side by side for testing.
//
// SFV quits when it finds a copy already running. Copies started with
// -sfve_instance=N (N > 0) give every named mutex the game creates or opens
// its own per-instance name, so each copy sees itself as the only one. Each
// renamed object is logged. Instance 0 (a normal player's game) is untouched:
// the caller installs this only for N > 0.

#include "mod/GameHooks.hxx"

namespace sfve::mod {

// Hooks the mutex calls as one set (CommitHookSet) for `instance` (> 0).
// Empty when they are in; otherwise the hook that failed, and none is.
std::optional<HookFailure> InstallMultiInstance(int instance);

} // namespace sfve::mod
