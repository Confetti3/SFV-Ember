#pragma once

// Ember's runtime hosted in the running game (EMBER_LIBRARY.md sections 3,
// 10 and 12). From the first frame the main menu is in front, on the game
// thread:
//   1. the catalog is filled from the game's content lists (SfvCatalog),
//      and the runtime starts only if the catalog is usable;
//   2. the helper (sf4-net.exe, next to the mod) is started in a
//      kill-on-close job and handed to the runtime, as SF4's launcher hands
//      over its own;
//   3. the runtime starts, with this copy's name and the game ready.
// From then on every game-thread frame runs Ember's outer tick (ember::Tick):
// the runtime, the room client and hosted server, the battle's poll (the
// GameTick the caller gives, NetplayMode's) and the published frame.
//
// The runtime logs through spdlog; its lines go to the mod's log, marked
// "ember:". The status field "ember" reports whether it runs, or why not.

#include "sf4e/sf4e__RuntimeTick.hxx"

namespace sfve::adapter {

struct HostSettings {
	// Names this copy in rooms ("SFV <instance + 1>"): a room's member names
	// are unique, and copies on one PC would otherwise share one.
	int instance = 0;
};

// Registers the tick; `battle` must outlive the process (the battle mode).
// Called once, from SfveStart, after the battle mode is made.
void InstallEmberHost(const HostSettings& settings, ember::GameTick& battle);

// The runtime has started (and the catalog is final). Any thread.
bool EmberRuntimeRunning();

} // namespace sfve::adapter
