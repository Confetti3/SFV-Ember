#include "ember/SfvPlayback.hxx"

#include "ember/PlaybackBook.hxx"
#include "ember/SfvReplays.hxx"
#include "mod/Log.hxx"
#include "mod/XInputHooks.hxx"

#include "common/PadKind.hxx"
#include "core/PlaybackSpan.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace sfve::adapter {

namespace {

namespace fs = std::filesystem;
namespace rt = sf4e::replaytransport;
using mod::Log;

// Differences between checkpoints that are logged, as the log is not to fill when a playback is off the recording.
constexpr int kMostLoggedDifferences = 8;

// The playback, from Watch now to the main menu's return. Game thread, but
// for the queues below and the published session.
struct State {
	std::shared_ptr<const replay::Replay> armed;   // launched, its battle not bound yet
	std::shared_ptr<const replay::Replay> playing; // its battle bound
	fs::path file;
	bool ended = false;  // the battle closed; the Replays screen comes back
	bool failed = false; // ...short of the recording's end, or off it
	std::string why;
	rt::Transport transport;
	rt::PadControls pad;
	bool viewHeld = false; // the pad's View, as last read
	rt::View view;
	PlaybackBook book;
	rt::Seek seek;
	// What the driver said of the game this turn, and the reach it gives a
	// seek: the first fight frame after the first state held, to the game's last.
	SeekGround ground;
	rt::SeekBounds bounds;
	bool seekable = false;
	bool stopAsked = false;
	int differenceLogs = 0;
};
State s_playback;
// s_playback.playing, for other threads (the window's keys).
std::atomic<bool> s_running{false};
rt::CommandQueue s_commands;
std::atomic<uint32_t> s_session{0};
// The stop asked from any thread, under the session it was asked in.
std::mutex s_stopMutex;
std::optional<uint32_t> s_stopFor;
// The seeks the pipe asked for by number, each under its session.
struct NumberedAsk {
	PlaybackSeekAsk ask;
	uint32_t session;
};
constexpr size_t kMostAsks = 8;
std::mutex s_asksMutex;
std::vector<NumberedAsk> s_asks;
std::string s_last = "none"; // how the last playback ended

// The strip's round and clock, from the book.
void ShowClock(State& p)
{
	p.view.round = p.book.Round();
	p.view.cursor = p.book.Cursor();
}

// What the driver said of the game, and the reach it makes: seeks only where
// a state can be put back, from the first fight frame after the first state
// held to the game's last fight frame.
void TakeGround(State& p, const SeekGround& ground)
{
	p.ground = ground;
	p.seekable = false;
	p.bounds = {};
	if (!ground.canRestore || ground.ringFirst < 0) return;
	const int floor = reach::LandingAfter(p.playing->frames, ground.ringFirst + 1, ground.span.last);
	if (floor < 0) return;
	p.bounds = {floor, ground.span.last};
	p.seekable = true;
}

// The legal landing an order rounds to, or -1.
int LandingOf(const State& p, const rt::SeekOrder& order)
{
	const ReplayFrames& frames = p.playing->frames;
	switch (order.rounding) {
	case rt::Rounding::Earlier: return reach::LandingBefore(frames, order.target, p.bounds.floor);
	case rt::Rounding::Later: return reach::LandingAfter(frames, order.target, p.bounds.last);
	case rt::Rounding::Exact: break;
	}
	return reach::LandingAfter(frames, order.target, p.bounds.last);
}

// The seek an order asks for begins, with the log line that says where from.
void BeginSeek(State& p, const rt::SeekOrder& order, const char* what)
{
	if (!order.made) return;
	const int landing = LandingOf(p, order);
	if (landing < 0) return;
	p.seek.Begin(landing, p.ground.frame);
	Log("replays: %s: from frame %d to frame %d%s", what, p.ground.frame, landing, landing < p.ground.frame ? " (back)" : "");
}

// A seek command (Command, Seeks). Only where a state can be put back: the
// fight of a game, once a state is held; elsewhere it is refused.
void TakeSeekCommand(State& p, rt::Command command)
{
	if (!p.seekable || (command == rt::Command::Loop && !p.playing->frames[(size_t)p.ground.frame].Fight())) {
		p.view.refused++;
		return;
	}
	const rt::SeekOrder order = p.seek.Plan(command, p.ground.frame, p.bounds);
	if (order.pause) p.transport.Pause();
	BeginSeek(p, order, "seek");
}

// A seek by number: to frame `value`, or `value` frames on from where the
// playback is (or is going).
void TakeNumberedSeek(State& p, const PlaybackSeekAsk& ask)
{
	if (!p.seekable) {
		p.view.refused++;
		return;
	}
	const int base = p.seek.Seeking() ? p.seek.Target() : p.ground.frame;
	BeginSeek(p, p.seek.To(ask.relative ? base + ask.value : ask.value, p.ground.frame, p.bounds), "seek by number");
}

void LogDifference(State& p, int frame, uint64_t hash, uint64_t recorded)
{
	if (p.differenceLogs++ >= kMostLoggedDifferences) return;
	if (p.seek.Landed()) {
		Log("replays: desync: checkpoint frame %d hash %016llx, the recording's %016llx (after %u seek(s))", frame,
			(unsigned long long)hash, (unsigned long long)recorded, p.seek.Landed());
		return;
	}
	Log("replays: desync: checkpoint frame %d hash %016llx, the recording's %016llx", frame, (unsigned long long)hash,
		(unsigned long long)recorded);
}

} // namespace

namespace playback {

void Arm(std::shared_ptr<const replay::Replay> replay, fs::path file)
{
	s_playback = State{};
	s_playback.armed = std::move(replay);
	s_playback.file = std::move(file);
}

bool Busy()
{
	return s_playback.armed || s_playback.playing;
}

void LaunchFailed(const std::string& why)
{
	State& p = s_playback;
	p.armed.reset();
	p.failed = p.ended = true;
	p.why = why;
}

bool Ended()
{
	return s_playback.ended;
}

bool Failed()
{
	return s_playback.failed;
}

const fs::path& File()
{
	return s_playback.file;
}

void Collect()
{
	s_playback = State{};
}

void TickPad(int deviceType, int deviceIndex)
{
	State& p = s_playback;
	p.view.playback = p.playing != nullptr;
	p.view.armed = p.view.playback;
	p.view.session = s_session;
	if (!p.playing || deviceType != sf4e::input::PadXInput) return;
	// The pad's own buttons, as the game last read them before the shell
	// hid them from it (XInputHooks): RB, RT, LB, X and the D-pad, and View to stop.
	const mod::PadSeen seen = mod::LastPadState(deviceIndex);
	uint32_t physical = seen.connected ? sf4e::input::xinput::Physical(seen.buttons) | sf4e::input::xinput::Directions(seen.buttons) : 0;
	if (seen.leftTrigger) physical |= sf4e::input::xinput::LT;
	if (seen.rightTrigger) physical |= sf4e::input::xinput::RT;
	rt::Command commands[rt::kMostSampled];
	const int count = p.pad.Sample(physical, commands);
	for (int i = 0; i < count; i++) s_commands.Submit(commands[i], rt::Device::Pad, s_session);
	const bool view = (physical & sf4e::input::xinput::View) != 0;
	if (view && !p.viewHeld) SubmitPlaybackStop(s_session);
	p.viewHeld = view;
}

const rt::View& View()
{
	return s_playback.view;
}

void StatusFields(nlohmann::json& out)
{
	const State& p = s_playback;
	out["armed"] = p.armed != nullptr;
	out["playing"] = p.playing != nullptr;
	out["session"] = s_session.load();
	out["paused"] = p.transport.Paused();
	out["divisor"] = p.transport.Divisor();
	out["round"] = p.view.round;
	out["cursor"] = p.view.cursor;
	// The playback's place: the frame the next update plays, of the recording's total, the earliest and
	// latest landing (-1 where no seek can be made now), the loop's marks, the states held and the seeks made.
	out["frame"] = p.ground.frame;
	out["total"] = p.playing ? p.playing->frames.size() : 0;
	out["floor"] = p.seekable ? p.bounds.floor : -1;
	out["last"] = p.seekable ? p.bounds.last : -1;
	out["loop"] = {{"phase", (int)p.seek.Loop()}, {"a", p.seek.MarkA()}, {"b", p.seek.MarkB()}};
	out["snapshots"] = p.ground.snapshots;
	out["seeks"] = p.seek.Landed();
	out["seeking"] = p.seek.Seeking();
	out["furthest"] = p.book.Furthest();
	out["frames"] = p.book.Played();
	out["compared"] = p.book.Compared();
	out["differing"] = p.book.Differing();
	out["comparisons"] = p.book.Comparisons();
}

const std::string& LastEnd()
{
	return s_last;
}

} // namespace playback

std::shared_ptr<const replay::Replay> TakeArmedReplay()
{
	State& p = s_playback;
	if (!p.armed) return nullptr;
	p.playing = std::move(p.armed);
	s_running = true;
	p.transport.Reset();
	p.pad.Reset();
	p.pad.Offer(true);
	p.book.Start(p.playing);
	p.seek.Reset();
	p.view = rt::View{};
	p.view.session = ++s_session;
	p.view.playback = p.view.armed = true;
	p.view.seek.offered = true;
	p.view.seek.total = (uint32_t)p.playing->frames.size();
	s_commands.Clear();
	return p.playing;
}

PlaybackTurn PlaybackTurnFor(const SeekGround& ground)
{
	State& p = s_playback;
	PlaybackTurn turn;
	if (!p.playing) return turn;
	TakeGround(p, ground);
	// What the player asked since the last frame, in the order they asked it.
	for (const auto& entry : s_commands.Take(p.view.session)) {
		p.view.device = entry.device;
		p.view.taken++;
		if (rt::MovesTransport(entry.command)) p.transport.Apply(entry.command);
		else if (rt::Seeks(entry.command)) TakeSeekCommand(p, entry.command);
	}
	std::vector<NumberedAsk> asks;
	{
		std::lock_guard lock(s_asksMutex);
		asks.swap(s_asks);
	}
	for (const NumberedAsk& entry : asks) {
		if (entry.session != p.view.session) continue;
		p.view.taken++;
		TakeNumberedSeek(p, entry.ask);
	}
	{
		std::lock_guard lock(s_stopMutex);
		if (s_stopFor && *s_stopFor == p.view.session) p.stopAsked = true;
		s_stopFor.reset();
	}
	// A seek and a loop are as long as a state can be put back for them: out
	// of the game's fight (the WIN, the next game) they are gone.
	if (!p.seekable) {
		if (p.seek.Seeking()) Log("replays: the seek to frame %d lost its ground at frame %d; dropped", p.seek.Target(), ground.frame);
		p.seek.Cancel();
		p.seek.ClearLoop();
	}
	// A loop that has reached its B goes back to A.
	if (p.seekable && !p.seek.Seeking()) BeginSeek(p, p.seek.Wrap(ground.frame), "loop");
	if (p.seek.Seeking()) {
		if (!p.seek.Progress(ground.frame)) {
			Log("replays: the seek to frame %d did not reach it from frame %d; dropped", p.seek.Target(), ground.frame);
			p.seek.Cancel();
		} else if (p.seek.Reached(ground.frame)) {
			p.seek.Land();
			Log("replays: the seek landed at frame %d (seek %u)", ground.frame, p.seek.Landed());
		}
	}
	p.view.paused = p.transport.Paused();
	p.view.divisor = p.transport.Divisor();
	p.view.seek.position = (uint32_t)ground.frame;
	p.view.seek.floor = p.seekable ? (uint32_t)p.bounds.floor : 0;
	p.view.seek.last = p.seekable ? (uint32_t)p.bounds.last : 0;
	p.seek.Show(p.view.seek);
	if (p.seek.Seeking()) {
		// Fast: a few engine frames of up to kMostSeekFrames updates, whatever the transport says.
		if (p.seek.TakeRestore()) turn.restoreBefore = p.seek.Target();
		turn.goal = p.seek.Target();
		turn.frames = 0;
		return turn;
	}
	turn.frames = p.transport.Hold() ? 0 : 1;
	return turn;
}

void NotePlaybackRestored(int frame)
{
	State& p = s_playback;
	if (!p.playing) return;
	if (frame < 0) {
		Log("replays: no state could be put back for the seek to frame %d; it is dropped", p.seek.Target());
		p.seek.Cancel();
		return;
	}
	p.book.NoteSeek(frame, p.ground.span.from);
	ShowClock(p);
}

bool TakePlaybackStop()
{
	State& p = s_playback;
	const bool asked = p.playing && p.stopAsked;
	p.stopAsked = false;
	return asked;
}

void NotePlaybackFrame(int frame, bool fight, unsigned game)
{
	State& p = s_playback;
	if (!p.playing) return;
	p.book.NoteFrame(frame, fight, game);
	ShowClock(p);
}

void NotePlaybackCheckpoint(int frame, std::uint64_t hash)
{
	State& p = s_playback;
	if (!p.playing) return;
	uint64_t recorded = 0;
	if (p.book.NoteCheckpoint(frame, hash, &recorded) == PlaybackBook::Compared::FirstDifference) {
		LogDifference(p, frame, hash, recorded);
	}
}

void NotePlaybackStart(std::uint64_t recorded, std::uint64_t played)
{
	State& p = s_playback;
	if (!p.playing || !recorded) return;
	p.book.NoteStart(recorded == played);
	if (recorded == played) {
		Log("replays: the playback starts from the recording's start state (%016llx)", (unsigned long long)played);
		return;
	}
	Log("replays: desync: the playback starts from state %016llx, P1's recorded battle from %016llx; its frames may play "
		"another fight", (unsigned long long)played, (unsigned long long)recorded);
}

void EndPlayback(const std::string& why)
{
	State& p = s_playback;
	if (!p.playing) return;
	// Whole is having been to the recording's last frame, however the playback
	// got there: seeks play frames again, or skip them.
	p.failed = p.book.Differing() > 0 || (!p.book.Whole() && why.find("stopped") == std::string::npos);
	char line[400];
	snprintf(line, sizeof(line),
		"%s; %d of %zu frame(s) played, %d checkpoint(s) compared with the recording, %d differing; %d frame(s) played in all, "
		"%u seek(s)", why.c_str(), p.book.Furthest(), p.playing->frames.size(), p.book.Compared(), p.book.Differing(),
		p.book.Played(), p.seek.Landed());
	Log("replays: playback ended: %s", line);
	s_last = line;
	p.why = why;
	p.playing.reset();
	s_running = false;
	p.ended = true;
	p.view = rt::View{};
	p.view.session = ++s_session;
	s_commands.Clear();
}

bool PlaybackRunning()
{
	return s_running;
}

void SubmitPlaybackCommand(rt::Command command, rt::Device device, std::uint32_t session)
{
	s_commands.Submit(command, device, session);
}

void SubmitPlaybackSeek(const PlaybackSeekAsk& ask, std::uint32_t session)
{
	std::lock_guard lock(s_asksMutex);
	if (s_asks.size() < kMostAsks) s_asks.push_back({ask, session});
}

void SubmitPlaybackStop(std::uint32_t session)
{
	std::lock_guard lock(s_stopMutex);
	s_stopFor = session;
}

std::uint32_t PlaybackSession()
{
	return s_session;
}

} // namespace sfve::adapter
