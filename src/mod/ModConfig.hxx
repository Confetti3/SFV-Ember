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
//   -sfve_moveset=S      ...from a set of moves: system (default), supers, cody,
//                        projectiles (input::MoveSet; any but system implies -sfve_moves=1)
//   -sfve_idle=P         the scripted player P (0 P1, 1 P2) stands still; the other
//                        walks forward at the start of each block of moves
//   -sfve_tiny=MASK      test: a launch that is no room's or replay's (the pipe's, for a
//                        synctest) draws these sides small: 1 P1, 2 P2, 3 both
//   -sfve_socket_log=1   diagnostics: log every read of a fighter's model socket the
//                        fight makes (SocketProbe), but the ones made every tick
//   -sfve_replay_any_build=1  diagnostics: Watch now plays a replay made with
//                        another build (its result says nothing of this build)
//   -sfve_practice=1     synctest, netplay: Training's refilling gauges from the first frame
//   -sfve_replay_regions=F  replay playback: log each checkpoint's region hashes from frame F
//   -sfve_replay_check=N    replay playback: also make rollbacks up to N frames deep and log
//                        what they leave different (random spacing from -sfve_schedule)
//   -sfve_tasks=0|1|2    test: battle tasks as the game places them (0), all of the tick on
//                        the battle's thread (1), and P2's fighter tasks before P1's (2)
//   -sfve_fma3=0|1       test: the game's CRT math (msvcr120) without or with its FMA3 code
//   -sfve_replay_lag=N      replay playback: play as a fighter's copy does when P1's inputs
//                        (-sfve_replay_lag_player=1: P2's) arrive N frames late, with
//                        guesses and rollbacks; checkpoints once final
//   -sfve_replay_jitter=SEED  ...with the delay wandering up to N, a new seed each playback
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
//   -sfve_ownership=N    development: Ember's ownership gate on (1) or off
//                        (0); off, this copy's player may pick what the
//                        game does not let them. The default is
//                        kOwnershipGateByDefault (EMBER_LIBRARY.md sections
//                        23 and 54)
//   -sfve_unowned=LIST   development: content marked unowned for this copy, on
//                        top of the game's answer: CODE, CODE:costume or
//                        CODE:costume:colour, comma separated (RYU,KEN:3,CNL:0:100)
//   -sfve_uninstalled=LIST  development: stage codes treated as not installed
//                        on this copy, comma separated (S01,S25): one full
//                        install then plays a copy that lacks those stages
//                        (EMBER_LIBRARY.md section 38)
//   -sfve_grant_all=0    development, diagnostics: the game grants what the
//                        account's own cache holds, as SFV did before; by
//                        default it grants every player everything
//                        (EntitlementGrant, EMBER_LIBRARY.md section 57)
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
// its ownership gate is kOwnershipGateByDefault, the game grants everything,
// nothing drives its player, it is copy 0. (The
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

// Whether Ember's ownership gate is on when nothing says otherwise, in a
// player build always. Off since 2026-11-10: the game learns what an account
// owns only from Capcom's entitlement server (POST /user/steam/entitlement,
// rva 0x10C1660, into the save's account cache), which no longer answers, so
// an owner whose cache was never filled could pick only vanilla content.
// Every copy then offers what it has installed. Back on once the server
// answers again.
constexpr bool kOwnershipGateByDefault = false;

struct ModConfig {
	int instance = 0;
	ModMode mode = ModMode::Probe;
	int player = 0;
	uint16_t port = 7000;
	uint16_t peerPort = 7001;
	int delay = 0;
	// Scripted play for automated tests (-sfve_mash, -sfve_mash_from,
	// -sfve_mash_pad, -sfve_moves, -sfve_moveset, -sfve_idle): the seed 0
	// leaves the pad playing.
	input::ScriptConfig script;
	// Sides a launch that is no room's or replay's draws small (bit 0 P1,
	// bit 1 P2; -sfve_tiny): a synctest's battle with a small side.
	uint32_t testTiny = 0;
	// Log the fight's reads of a fighter's model socket (-sfve_socket_log).
	bool socketLog = false;
	// Watch now plays a replay of another build (-sfve_replay_any_build).
	bool replayAnyBuild = false;
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
	// A replay's playback also checks rollbacks of up to this many frames on
	// the recording's inputs (RollbackSession::Config::replayCheckDistance),
	// spaced from scheduleSeed. 0: none.
	int replayCheck = 0;
	// A replay's playback plays as a fighter's copy does when player
	// replayLagPlayer's inputs arrive this many frames late
	// (RollbackSession::Config::replayLag). 0: none.
	int replayLag = 0;
	// msvcr120's FMA3 code for the game's math: -1 as the CRT chose it at
	// start (FMA3 where the CPU has it), 0 off, 1 on (DllMain: SetCrtFma3).
	int crtFma3 = -1;
	// Where the battle's tick tasks run (ProjectileRelease's task hook, the
	// modes that roll back): 0 as the game places them, 1 all on the battle's
	// thread in the scheduler's order, 2 as 1 with P2's fighter tasks first.
	int battleTasks = 0;
	int replayLagPlayer = 0;
	// Nonzero: the lagged replay's delay wanders up to replayLag, seeded from
	// this plus the playback's number in this process (-sfve_replay_jitter).
	uint32_t replayLagJitter = 0;
	bool room = false; // netplay: Ember's runtime runs, and its rooms' matches give the endpoints, seed and delay
	// Offline start-up dialogs kept from showing: 0 none, 1 the download
	// failures, 2 those and the offline-mode notice.
	int offlineDialogs = 2;
	// Ember's ownership gate: this copy's player may pick only what the game
	// lets them (GameContent's `owned`).
	bool ownershipGate = kOwnershipGateByDefault;
	// The game grants every player everything it has installed
	// (EntitlementGrant): always in a player build; -sfve_grant_all=0 turns
	// it off in a development build, to see the account's own answer in the
	// game's menus.
	bool grantAll = true;
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
