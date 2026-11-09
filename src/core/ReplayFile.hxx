#pragma once

// SFV Ember's replay file (EMBER_LIBRARY.md section 26): one room battle as
// this copy played or watched it, everything a later playback needs to play
// the same battle again, and the battle's own checkpoints to prove that it
// did.
//
// A battle, not a game: a game that goes on in place (section 19) starts
// from the WIN of the game before it, in the same battle, so its frames only
// play after that game's. The file holds every game of the battle, in order.
//
// Layout, little-endian:
//   "SFVEREPL"                magic, 8 bytes
//   u32 version               kVersion
//   u32 n, n bytes            the header, UTF-8 JSON (ReplayHeader)
//   u32 runs, then each run   u32 frames, u32 P1, u32 P2, u8 flags: that many
//                             frames alike (ReplayFrames, run-length coded)
//   u32 n, then n of          i32 frame, u64 hash: the match's checkpoints
//   u64 XXH64 of every byte before it, seed 0
//
// Decode refuses anything else: another magic or version, sizes past the
// file or past the limits below, a header that does not read, unknown frame
// flags, checkpoints out of order or past the frames, or a hash that does
// not match. A file it accepts plays from its first frame to its last.

#include "core/ReplayFrames.hxx"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

namespace sfve::replay {

constexpr char kMagic[8] = {'S', 'F', 'V', 'E', 'R', 'E', 'P', 'L'};
constexpr uint32_t kVersion = 1;
constexpr const wchar_t* kExtension = L".sfvreplay";

// Limits on what a file may hold: two million frames are over nine hours
// at 60 a second; the header is a few hundred bytes.
constexpr size_t kMostFrames = 2'000'000;
constexpr size_t kMostHeaderBytes = 64 * 1024;
constexpr size_t kMostFileBytes = 64ull * 1024 * 1024;

// One side of the battle.
struct Player {
	std::string name;        // as the room named the side (UTF-8); empty when it named none
	int fighter = -1;        // Ember's catalog number (the list's portraits); -1 none
	int costume = -1;        // the catalog's costume and colour numbers
	int colour = -1;
	// As the battle was launched (VersusLaunch's FighterPick): the game's
	// fighter code and its own costume and colour numbers.
	std::string code;
	int launchCostume = -1;
	int launchColour = -1;
	// The V-Skill and V-Trigger launched, as the game numbers them (0 is I, 1
	// is II; EMBER_LIBRARY.md section 40). A header written before they were
	// recorded has neither, and reads 0: such a file's build is one a
	// playback refuses anyway, so only its listing reads them.
	int vSkill = 0;
	int vTrigger = 0;
};

// One game of the battle, as the game judged it on its match end's frame.
struct Game {
	bool decided = false;
	int winner = -1;      // 0 or 1; -1 a draw
	int wins[2] = {0, 0}; // rounds won
	int rounds = 0;       // rounds judged
	int bestOf = 0;
	int endFrame = -1;    // the session frame its end was taken on
};

struct Header {
	std::string product;  // "SFV Ember"
	std::string build;    // the room's build id (MatchBuildId): a playback needs the same
	uint64_t startedAt = 0; // UTC seconds, when the battle's session started
	int slot = 0;         // this copy's match slot: 0 and 1 fight, 2 and up watched
	Player players[2];
	int64_t stage = -1;   // Ember's stage number
	std::string stageCode; // the game's stage code, as launched
	int rounds = 0;       // the room's round count and time
	int roundSeconds = 0;
	int timeFrames = 0;   // the round clock as launched, in frames
	uint32_t seed = 0;    // the room's match seed: the session's random generators
	int inputDelay = -1;  // the delay this copy played at (watchers: the fighters'); for the record
	std::vector<Game> games;
	// The battle went back to the room by its WIN's decision. False for one
	// cut short (a dropped link, the game closed): it plays to where the
	// recording ends.
	bool complete = false;
	// The frames that read the players' inputs and the stretches of them,
	// each a round's fight, over the whole battle (the list's length).
	uint32_t fightFrames = 0;
	uint32_t fightStretches = 0;
};

struct Replay {
	Header header;
	ReplayFrames frames;
	std::vector<std::pair<int, uint64_t>> checkpoints; // frame order, every kCheckpointInterval
};

// The frames that read inputs and the stretches of them (Header's counts).
void CountFights(const ReplayFrames& frames, uint32_t* fightFrames, uint32_t* stretches);

std::vector<uint8_t> Encode(const Replay& replay);
bool Decode(const uint8_t* data, size_t size, Replay* out, std::string* error);
// The header alone, from a whole file whose hash matches (the list reads it).
bool DecodeHeader(const uint8_t* data, size_t size, Header* out, std::string* error);

} // namespace sfve::replay
