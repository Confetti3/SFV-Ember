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
//   -sfve_mash_pad=1     ...and the pad's input before F (a test's scripted presses first)
//   -sfve_moves=1        ...playing the game's system moves (input::Script::Moves)
//   -sfve_practice=1     synctest, netplay: Training's refilling gauges from the first frame
//   -sfve_replay_regions=F  replay playback: log each checkpoint's region hashes from frame F
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
//   -sfve_ownership=0    development: Ember's ownership gate is off, so this
//                        copy's player may pick what the game does not let
//                        them (default 1: the gate is on, as in the public
//                        version; EMBER_LIBRARY.md section 23)
//   -sfve_unowned=LIST   development: content marked unowned for this copy, on
//                        top of the game's answer: CODE, CODE:costume or
//                        CODE:costume:colour, comma separated (RYU,KEN:3,CNL:0:100)
//   -sfve_uninstalled=LIST  development: stage codes treated as not installed
//                        on this copy, comma separated (S01,S25): one full
//                        install then plays a copy that lacks those stages
//                        (EMBER_LIBRARY.md section 38)
//   -sfve_game_ownership=1  a launch that is not a room's runs the game's own
//                        ownership check (default off; a room's never does)
//   -sfve_dialogs=N      offline start-up dialogs hidden: 0 none, 1 download errors,
//                        2 those and the offline notice (default)
//   -sfve_dump_channel=R:D:M  the launcher's crash dump channel: the request
//                        and done events and the mailbox, as this process's
//                        handle values in hex (SfvLaunch.cxx; CrashRecord.hxx)
//   -sfve_crash_command=1  test: the pipe's "crash game|thread" faults on purpose
//
// All of them are a development build's (mod/BuildKind.hxx). A player build
// reads only what its launcher passes, -sfve_mode=netplay, -sfve_room and
// -sfve_dump_channel, and every other option or value as if it were absent:
// its ownership gate is on, nothing drives its player, it is copy 0. (The
// launcher's -sfve_loader=launcher is EarlyStart's, read before this.)

#include "mod/BuildKind.hxx"
#include "mod/GameInput.hxx"

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

enum class ModMode { Inert, Probe, SyncTest, Netplay };

// One -sfve_unowned entry: a fighter, one costume of it (colour -1), or one
// colour of that costume.
struct UnownedContent {
	std::string fighter; // the game's code
	int costume = -1;
	int colour = -1;
};

// The launcher's crash dump channel (common/CrashDump.hxx) as this process's
// handles to it, -sfve_dump_channel. All zero: no launcher.
struct DumpChannelHandles {
	uint64_t request = 0;
	uint64_t done = 0;
	uint64_t mailbox = 0;
	bool Present() const { return request && done && mailbox; }
};

struct ModConfig {
	int instance = 0;
	ModMode mode = ModMode::Probe;
	int player = 0;
	uint16_t port = 7000;
	uint16_t peerPort = 7001;
	int delay = 0;
	// Scripted play for automated tests (-sfve_mash, -sfve_mash_from,
	// -sfve_mash_pad, -sfve_moves): the seed 0 leaves the pad playing.
	input::ScriptConfig script;
	// Training's refilling gauges from the battle's first frame on both
	// players, as a Practice room's match has them, so every V-Trigger and
	// Critical Art a script asks for is played (synctest and netplay).
	bool practiceGauges = false;
	int distance = 7;
	uint32_t virtualPadMask = 0; // bit i: slot i is virtual from the start
	bool backgroundPlay = false;
	bool learn = false;
	bool perturb = false;
	uint32_t scheduleSeed = 0;
	int lagMs = 0;
	int hashEvery = 0; // 0: checkpoints only
	// A replay's playback logs each checkpoint's region hashes from this frame
	// on, in the desync dump's form, so two PCs' playbacks (or a playback and
	// a match's log) can be compared region by region (tools/compare.py).
	// 0: none.
	int replayRegionsFrom = 0;
	bool room = false; // netplay: Ember's runtime runs, and its rooms' matches give the endpoints, seed and delay
	// Offline start-up dialogs kept from showing: 0 none, 1 the download
	// failures, 2 those and the offline-mode notice.
	int offlineDialogs = 2;
	// Ember's ownership gate: this copy's player may pick only what the game
	// lets them (GameContent's `owned`). Off only for development.
	bool ownershipGate = true;
	// Content this copy treats as unowned on top of the game's answer, to try
	// the gate's screens and refusals with an account that owns everything.
	std::vector<UnownedContent> unowned;
	// Stage codes this copy treats as not installed (GameContent's
	// MarkUninstalled): never the stage universe, only `installed`.
	std::vector<std::string> uninstalled;
	// A launch that is not a room's runs the game's own check (the battle
	// jump's, VersusLaunch). A room's launch never does.
	bool gameOwnershipCheck = false;
	// The launcher's crash dump channel; none under the proxy.
	DumpChannelHandles dumpChannel;
	// Test only: the pipe may be asked to crash the game.
	bool crashCommand = false;
};

const ModConfig& Config();
void LoadConfigFromCommandLine(const wchar_t* commandLine, BuildKind kind = kBuildKind);
// A setting the command line gave that its mode cannot take, and so left
// off, for the log; null when there is none. -sfve_room=1 hosts Ember's
// runtime in the netplay mode only, so with any other mode rooms stay off.
const char* IgnoredSetting();
// "-sfve_unowned=..." as the command line gave it, when an entry did not
// parse (and was left out); empty otherwise. For the log.
const std::string& UnownedProblem();
// "-sfve_uninstalled=..." likewise, when an entry was not a stage code.
const std::string& UninstalledProblem();
// The development options a player build was given and did not read
// ("-sfve_ownership, -sfve_mash"), for the log; empty when there were none.
const std::string& PlayerBuildIgnored();
const char* ModeName(ModMode mode);

} // namespace sfve::mod
