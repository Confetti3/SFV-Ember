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
//   -sfve_mash=N         drive this copy's player with seeded random inputs
//   -sfve_mash_from=F    ...starting at session frame F (no input before)
//   -sfve_distance=N     synctest rollback distance (default 7)
//   -sfve_pads=0,1       XInput slots that are virtual pads from the start
//   -sfve_background=1   keep playing and reading pads while unfocused
//   -sfve_learn=1        synctest: keep going after a difference, tallying regions
//   -sfve_perturb=1      synctest: play each rolled-back stretch with wrong inputs first
//   -sfve_lag=MS         test: hold back outgoing UDP by MS milliseconds
//   -sfve_link=1         netplay through Ember's helper ("host" / "join <invitation>")
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
	int lagMs = 0;
	bool helperLink = false; // netplay endpoints, seed and delay come from a HelperLink
	// Offline start-up dialogs kept from showing: 0 none, 1 the download
	// failures, 2 those and the offline-mode notice.
	int offlineDialogs = 2;
};

const ModConfig& Config();
void LoadConfigFromCommandLine(const wchar_t* commandLine);
const char* ModeName(ModMode mode);

} // namespace sfve::mod
