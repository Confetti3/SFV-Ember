#pragma once

// Settings from the game's command line (UE4 ignores switches it does not
// know), so test scripts can start several copies side by side:
//
//   -sfve_instance=N     0, 1, ...: separate log, ports and pad slot
//   -sfve_mode=M         inert | probe | synctest | netplay   (default probe)
//   -sfve_player=P       netplay: 0 = P1, 1 = P2
//   -sfve_port=N         netplay: this copy's UDP port   (default 7000 + 2*instance)
//   -sfve_peer_port=N    netplay: the other copy's port on loopback
//   -sfve_delay=N        netplay: input delay frames (default 0: most rollbacks)
//   -sfve_mash=N         drive this copy's player (synctest: both) with seeded random inputs
//   -sfve_mash_from=F    ...starting at session frame F (no input before)
//   -sfve_distance=N     synctest rollback distance (default 7)
//   -sfve_pads=0,1       XInput slots that are virtual pads from the start
//   -sfve_background=1   keep playing and reading pads while unfocused
//   -sfve_learn=1        synctest: keep going after a difference, tallying regions
//   -sfve_perturb=1      synctest: play each rolled-back stretch with wrong inputs first
//   -sfve_schedule=SEED  synctest: rollbacks of random depth and spacing (up to the distance)
//   -sfve_lag=MS         test: hold back outgoing UDP by MS milliseconds
//   -sfve_hash_every=N   netplay diagnostics: hash every Nth frame and keep every
//                        hash for the match (pipe "hash F"; tools/compare.py)
//   -sfve_room=1         netplay: host Ember's runtime; battles are rooms' matches
//                        (the pipe's room commands, RoomCommands.hxx)
//   -sfve_ownership=1    the match launch runs the game's ownership check
//                        (off while developing; on in the public version)
//   -sfve_dialogs=N      offline start-up dialogs hidden: 0 none, 1 download errors,
//                        2 those and the offline notice (default)

#include <stdint.h>


namespace sfve::mod {

enum class ModMode { Inert, Probe, SyncTest, Netplay };

struct ModConfig {
	int instance = 0;
	ModMode mode = ModMode::Probe;
	int player = 0;
	uint16_t port = 7000;
	uint16_t peerPort = 7001;
	int delay = 0;
	uint32_t mashSeed = 0; // 0: no mashing
	int mashFrom = 0;
	int distance = 7;
	uint32_t virtualPadMask = 0; // bit i: slot i is virtual from the start
	bool backgroundPlay = false;
	bool learn = false;
	bool perturb = false;
	uint32_t scheduleSeed = 0;
	int lagMs = 0;
	int hashEvery = 0; // 0: checkpoints only
	bool room = false; // netplay: Ember's runtime runs, and its rooms' matches give the endpoints, seed and delay
	// Offline start-up dialogs kept from showing: 0 none, 1 the download
	// failures, 2 those and the offline-mode notice.
	int offlineDialogs = 2;
	// The launch runs the game's ownership check. Off for development (the maintainer,
	// 2026-11-04); the public version of Ember turns it on.
	bool ownershipCheck = false;
};

const ModConfig& Config();
void LoadConfigFromCommandLine(const wchar_t* commandLine);
// A setting the command line gave that its mode cannot take, and so left
// off, for the log; null when there is none. -sfve_room=1 hosts Ember's
// runtime in the netplay mode only, so with any other mode rooms stay off.
const char* IgnoredSetting();
const char* ModeName(ModMode mode);

} // namespace sfve::mod
