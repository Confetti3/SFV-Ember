#pragma once

// The playback of a replay in SFV Ember (EMBER_LIBRARY.md section 26,
// docs/design/beta10/replay-seek.md): from Watch now's arming to the main
// menu's return, its transport (pause, step, speeds), its seeks (step back,
// jumps, restart, an A-B loop) and what it keeps of itself (PlaybackBook).
// The Replays screen's adapter (SfvReplays.cxx) arms it, ticks it and reports
// it; the netplay mode drives it each engine frame through the functions
// declared in SfvReplays.hxx.
//
// Game thread, but for the command queues and the published session.

#include "core/ReplayFile.hxx"

#include "common/ReplayTransport.hxx"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace sfve::adapter::playback {

// A replay's battle is launched for: armed, its battle not bound yet. Only
// one at a time; `Busy` is true from here to its end's collection.
void Arm(std::shared_ptr<const replay::Replay> replay, std::filesystem::path file);
bool Busy();
// The launch came to nothing: the Replays screen comes back with why.
void LaunchFailed(const std::string& why);
// The playback ended and the main menu is back (the Replays screen's to take): the file it was
// of, and whether it ended failed (short of the recording's end, or off it).
bool Ended();
bool Failed();
const std::filesystem::path& File();
// The screen has taken it: nothing of the playback is kept.
void Collect();
// Once a game tick with the controller the player uses: the pad's controls.
void TickPad(int deviceType, int deviceIndex);
const sf4e::replaytransport::View& View();
// The playback's fields of the pipe's "replays" status.
void StatusFields(nlohmann::json& out);
// How the last playback ended, "none" before any.
const std::string& LastEnd();

} // namespace sfve::adapter::playback
