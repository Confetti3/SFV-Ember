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
//   -sfve_link=1         netplay through Ember's helper ("host" / "join <invitation>")
//   -sfve_fighter=F      linked netplay: this copy's fighter, CODE[:costume[:colour]]
//   -sfve_stage=S        linked netplay, host: the stage code
//   -sfve_rounds=N       linked netplay, host: rounds, 1|3|5|7 (default 3)
//   -sfve_time=S         linked netplay, host: round seconds or inf (default 99)
//   -sfve_after=A        linked netplay, host: rematch|menu at the match end (default rematch)
//   -sfve_ownership=1    the match launch runs the game's ownership check
//                        (off while developing; on in the public version)
//   -sfve_dialogs=N      offline start-up dialogs hidden: 0 none, 1 download errors,
//                        2 those and the offline notice (default)

#include <stdint.h>

#include <string>

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
	bool helperLink = false; // netplay endpoints, seed and delay come from a HelperLink
	// Offline start-up dialogs kept from showing: 0 none, 1 the download
	// failures, 2 those and the offline-mode notice.
	int offlineDialogs = 2;
	// The launch runs the game's ownership check. Off for development (the maintainer,
	// 2026-11-04); the public version of Ember turns it on.
	bool ownershipCheck = false;
	std::string fighter; // linked netplay: this copy's pick (empty: the game's default)
	std::string stage;   // linked netplay, host: the stage (empty: the game's default)
	std::string rounds;  // linked netplay, host: 1, 3, 5 or 7 (empty: 3)
	std::string time;    // linked netplay, host: round seconds or "inf" (empty: 99)
	std::string after;   // linked netplay, host: "rematch" or "menu" at the match's end (empty: rematch)
};

const ModConfig& Config();
void LoadConfigFromCommandLine(const wchar_t* commandLine);
const char* ModeName(ModMode mode);

} // namespace sfve::mod
