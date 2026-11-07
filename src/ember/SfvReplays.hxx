#pragma once

// SFV Ember's replays (EMBER_LIBRARY.md section 26): the library's Replays
// screen over SFV's own replay files, and their playback in the game.
//
// SFV keeps no replay of an offline battle (its replays are Capcom's online
// service's), so SFV Ember records a room battle itself: the mod's netplay
// mode keeps the frames its session played (core/ReplayRecorder.hxx) and
// saves them with the battle's launch, its seed and its checkpoints when the
// battle closes, one file a battle (core/ReplayFile.hxx), in
// %APPDATA%\SFVEmber\replays. A fighter's copy and a watcher's each save
// their own.
//
// The Replays screen lists them (newest first, with the players, fighters,
// score and length) and offers what SFV can do with one: Watch now, Keep and
// Show in folder (Capabilities' replay flags). Watch now launches the
// recorded battle from the main menu, as a room's match is launched, and the
// netplay mode plays the recorded frames through the watcher's path
// (RollbackSession's Config::replay): the same decisions on the same frames,
// the checkpoints compared with the recording's as it goes. Its controls are
// the library's transport: pause, step, half and quarter speed, and Stop
// watching, from the pad (RB, RT, LB, View) or the keyboard (F1 to F4, Esc).
//
// Threads: the adapter's methods run on the game thread (the runtime's
// tick), but for the listing's, which any thread may call; the netplay mode
// calls the playback's functions below on the game thread; Submit* may be
// called from any thread.

#include "core/ReplayFile.hxx"
#include "core/RollbackSession.hxx"

#include "common/ReplayTransport.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace sfve::adapter {

// The game adapter's replays (SfvGame's Adapter).
ember::game::Replays& SfvReplays();

// Where replays are saved: %APPDATA%\SFVEmber\replays (the product's
// folder, as SF4 Ember's archive is its own); empty when Windows names no
// roaming application data folder. SFVE_REPLAY_DIR names another, for the
// tests and the test tools.
std::filesystem::path ReplayFolder();

// --- Recording (the netplay mode, game thread)

// What the room's battle starting now is, for its replay's header: the
// launch both copies made (the picks, stage and rules), the room's seed,
// this copy's slot and delay, the room's names for both sides and the build.
// False when the runtime has no such match.
bool RecordingHeader(int slot, std::uint32_t seed, int delay, replay::Header* out);
// Writes a battle's replay (`replay` complete but for its fight counts,
// which are made here); the file, or empty when it could not be written
// (logged). At the main menu, between battles.
std::filesystem::path SaveReplay(replay::Replay replay);

// --- Playback (the netplay mode, game thread)

// The battle a replay's launch loaded binds: the replay armed by Watch now,
// once; null when none is armed.
std::shared_ptr<const replay::Replay> TakeArmedReplay();
// Each rendered frame of the replay's battle: whether the battle plays a
// frame now, by the transport (playing at 1x, every frame; at 1/2 or 1/4,
// one in two or four; paused, only a step's).
bool PlaybackFrameWanted();
// The player asked to stop watching (View, Esc, or the pipe): true once.
bool TakePlaybackStop();
// After each frame the replay's battle played: whether it read the inputs
// (a fight frame) and the game's number in the battle, for the strip's
// round and clock.
void NotePlaybackFrame(bool fight, unsigned game);
// A checkpoint the playback reached, compared with the recording's.
void NotePlaybackCheckpoint(int frame, std::uint64_t hash);
// The replay's battle is gone, the main menu back: why it ended.
void EndPlayback(const std::string& why);
// While a replay's battle plays: the shell hides the pads from the game
// (the battle's inputs are the recording's) and takes the playback keys.
bool PlaybackRunning();

// --- Controls (any thread)

// A transport command (common/ReplayTransport.hxx) made under the playback
// `session` (View::session): taken only by that playback.
void SubmitPlaybackCommand(sf4e::replaytransport::Command command, sf4e::replaytransport::Device device,
	std::uint32_t session);
void SubmitPlaybackStop(std::uint32_t session);

// The pipe's commands: "replays" (the listing), "replay watch <file|latest>",
// "replay keep <file> 0|1", "replay pause|step|slower|faster|stop", and the
// "replays" status field (the playback's state and its checkpoints).
void RegisterReplayCommands();

} // namespace sfve::adapter
