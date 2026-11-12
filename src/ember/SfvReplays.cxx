#include "ember/SfvReplays.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/ModConfig.hxx"
#include "mod/VersusLaunch.hxx"
#include "mod/XInputHooks.hxx"

#include "common/Localization.hxx"
#include "common/PadKind.hxx"
#include "common/ProductIdentity.hxx"
#include "common/ReplayProvenance.hxx"
#include "platform/DurableFile.hxx"
#include "platform/ReplayNotes.hxx"
#include "platform/Utf8.hxx"
#include "sf4e/sf4e__RuntimeSession.hxx"

#include <windows.h>
#include <shlobj.h>
#include <xxhash.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

namespace sfve::adapter {

namespace {

namespace fs = std::filesystem;
namespace rt = sf4e::replaytransport;
namespace notes = sf4e::platform::replays::notes;
using ArchivedReplay = sf4e::platform::replays::ArchivedReplay;
using mod::Log;

// The lists of marks beside the replays (ReplayNotes.hxx): the player's
// Keep, and the replays played with Watch now (the list's Seen).
constexpr const wchar_t* kKeptList = L"kept.txt";
constexpr const wchar_t* kWatchedList = L"watched.txt";

std::optional<std::vector<uint8_t>> ReadFile(const fs::path& path, size_t most)
{
	std::error_code error;
	const uintmax_t size = fs::file_size(path, error);
	if (error || size > most) return std::nullopt;
	std::ifstream in(path, std::ios::binary);
	if (!in) return std::nullopt;
	std::vector<uint8_t> bytes((size_t)size);
	if (size && !in.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)size)) return std::nullopt;
	return bytes;
}

std::string NameOf(const fs::path& file)
{
	return sf4e::platform::WideToUtf8(file.filename().wstring());
}

// The names a list holds; none for a list that is not there. A list that
// cannot be read marks nothing (unlike SF4's sent list, nothing is lost by
// showing a replay unmarked).
std::unordered_set<std::string> ReadList(const fs::path& folder, const wchar_t* list)
{
	const auto bytes = ReadFile(folder / list, notes::kMostBytes);
	return bytes ? notes::Parse(*bytes) : std::unordered_set<std::string>{};
}

// Adds a name to a list, or takes it out, written whole (ReplayNotes.hxx).
bool Mark(const fs::path& file, const wchar_t* list, bool on)
{
	const fs::path folder = file.parent_path();
	const fs::path path = folder / list;
	const auto bytes = ReadFile(path, notes::kMostBytes);
	// A list past the bound, unreadable, or not known to be absent is left
	// alone; only a list that is not there starts empty.
	std::error_code error;
	if (!bytes && (fs::exists(path, error) || error)) return false;
	const std::vector<uint8_t> text = bytes ? *bytes : std::vector<uint8_t>{};
	const std::string name = NameOf(file);
	std::unordered_set<std::string> names = notes::Parse(text);
	if (on == (names.count(name) != 0)) return true;
	std::string out = notes::Pruned(text, [&](const std::string& line) { return line != name; });
	if (on) out += name + "\n";
	return notes::Write(path, out);
}

// A file the listing may name: an .sfvreplay directly in the replay folder.
// The folder and the file's own are compared as the file system knows them
// (however each is spelled); either missing, or unreadable, is not a match.
bool InReplayFolder(const fs::path& file)
{
	const fs::path folder = ReplayFolder();
	if (folder.empty() || file.extension() != replay::kExtension) return false;
	std::error_code error;
	return fs::equivalent(file.parent_path(), folder, error) && !error;
}

// What the list shows of a battle: the score (a battle of one game, its
// rounds; of several in place, its games), the rounds judged, its fight's
// length, and each side's look in the catalog's numbers.
sf4e::replayinputs::Summary SummaryOf(const replay::Header& h)
{
	sf4e::replayinputs::Summary s;
	if (h.games.size() == 1 && h.games[0].decided) {
		s.scored = true;
		s.score[0] = h.games[0].wins[0];
		s.score[1] = h.games[0].wins[1];
	} else if (h.games.size() > 1) {
		s.scored = std::all_of(h.games.begin(), h.games.end(), [](const replay::Game& g) { return g.decided; });
		for (const replay::Game& g : h.games) {
			if (g.winner == 0 || g.winner == 1) s.score[g.winner]++;
		}
	}
	for (const replay::Game& g : h.games) s.rounds += (uint32_t)std::max(g.rounds, 0);
	if (!s.rounds) s.rounds = h.fightStretches;
	s.frames = h.fightFrames;
	for (int side = 0; side < 2; side++) {
		const replay::Player& p = h.players[side];
		s.players[side].fighter = p.fighter;
		s.players[side].costume = p.costume;
		s.players[side].color = p.colour;
		// The Ultra number is the loadout (section 40); a header's numbers out
		// of range read as the first loadout, as the pick's slot does.
		s.players[side].ultra = picks::LoadoutIndex(picks::LoadoutOfNumbers(p.vSkill, p.vTrigger).value_or(picks::Loadout{}));
		s.players[side].handicap = 0;
	}
	return s;
}

ArchivedReplay EntryOf(const fs::path& file, const replay::Header& h)
{
	ArchivedReplay r;
	r.path = file;
	r.time = h.startedAt;
	r.label = sf4e::replayfiles::ReplayDateLabel(h.startedAt);
	for (int side = 0; side < 2; side++) {
		r.fighters[side] = h.players[side].fighter;
		r.names[side] = h.players[side].name;
	}
	r.provenance = h.slot >= 2 ? sf4e::replayfiles::Provenance::Spectated : sf4e::replayfiles::Provenance::Played;
	r.summary = SummaryOf(h);
	return r;
}

// Lists the replay folder on a thread of its own, at most once every two
// seconds (as SF4's lister does). A file is read again only when its size or
// time changed; each must decode whole (its hash), so the list never names a
// replay that will not play.
class Lister {
public:
	// StopListing ends the thread as the runtime stops; never joined at the
	// DLL's detach, under the loader lock.
	~Lister()
	{
		if (thread_.joinable()) thread_.detach();
	}
	void Want()
	{
		std::lock_guard lock(mutex_);
		if (stopped_) return;
		wanted_ = true;
		if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
		wake_.notify_one();
	}
	std::shared_ptr<const std::vector<ArchivedReplay>> Latest()
	{
		std::lock_guard lock(mutex_);
		return latest_;
	}
	void Stop()
	{
		{
			std::lock_guard lock(mutex_);
			stopped_ = true;
			wake_.notify_one();
		}
		if (thread_.joinable()) thread_.join();
	}

private:
	struct Cached {
		uintmax_t size = 0;
		fs::file_time_type time{};
		std::optional<ArchivedReplay> entry; // none: the file is not a replay that plays
	};

	void Run()
	{
		std::unique_lock lock(mutex_);
		for (;;) {
			wake_.wait(lock, [&] { return wanted_ || stopped_; });
			if (stopped_) return;
			const auto due = listedAt_ + std::chrono::seconds(2);
			if (std::chrono::steady_clock::now() < due && wake_.wait_until(lock, due, [&] { return stopped_; })) return;
			wanted_ = false;
			lock.unlock();
			auto listing = std::make_shared<std::vector<ArchivedReplay>>(List());
			lock.lock();
			latest_ = std::move(listing);
			listedAt_ = std::chrono::steady_clock::now();
		}
	}

	std::vector<ArchivedReplay> List()
	{
		std::vector<ArchivedReplay> out;
		const fs::path folder = ReplayFolder();
		if (folder.empty()) return out;
		std::map<fs::path, Cached> seen;
		std::error_code error;
		for (fs::directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
			const fs::path file = at->path();
			if (file.extension() != replay::kExtension || !at->is_regular_file(error)) continue;
			Cached c;
			c.size = at->file_size(error);
			c.time = at->last_write_time(error);
			if (error) continue;
			const auto known = cache_.find(file);
			if (known != cache_.end() && known->second.size == c.size && known->second.time == c.time) {
				c.entry = known->second.entry;
			} else if (const auto bytes = ReadFile(file, replay::kMostFileBytes)) {
				replay::Header header;
				std::string why;
				if (replay::DecodeHeader(bytes->data(), bytes->size(), &header, &why)) {
					c.entry = EntryOf(file, header);
				} else {
					Log("replays: %ls is not listed: %s", file.filename().c_str(), why.c_str());
				}
			}
			seen[file] = c;
		}
		cache_ = std::move(seen);
		const auto kept = ReadList(folder, kKeptList), watched = ReadList(folder, kWatchedList);
		for (const auto& [file, c] : cache_) {
			if (!c.entry) continue;
			ArchivedReplay r = *c.entry;
			const std::string name = NameOf(file);
			r.kept = kept.count(name) != 0;
			r.watched = watched.count(name) != 0;
			out.push_back(std::move(r));
		}
		std::sort(out.begin(), out.end(), [](const ArchivedReplay& a, const ArchivedReplay& b) {
			return a.time != b.time ? a.time > b.time : a.path.filename() > b.path.filename();
		});
		return out;
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	std::thread thread_;
	bool wanted_ = false, stopped_ = false;
	std::chrono::steady_clock::time_point listedAt_{};
	std::shared_ptr<const std::vector<ArchivedReplay>> latest_;
	std::map<fs::path, Cached> cache_; // the lister's thread only
};

// The launch a replay asks for, as the room's match was launched; empty for
// a header this copy's catalog cannot launch, its stage among them when this
// copy has not installed it (the recording's stage is the room's, which this
// copy may lack; SfvCatalog's StageInstalled).
std::optional<mod::MatchPicks> PicksOf(const replay::Header& h)
{
	mod::MatchPicks picks;
	picks.target = mod::LaunchTarget::Versus;
	for (int side = 0; side < 2; side++) {
		const replay::Player& p = h.players[side];
		picks.players[side].fighter = sf4e::platform::Utf8ToWide(p.code.c_str());
		picks.players[side].costume = p.launchCostume;
		picks.players[side].colour = p.launchColour;
		if (!FighterId(picks.players[side].fighter) || p.launchCostume < 0 || p.launchColour < 0 ||
			!sfve::picks::LoadoutOfNumbers(p.vSkill, p.vTrigger)) {
			return std::nullopt;
		}
		picks.players[side].vSkill = p.vSkill;
		picks.players[side].vTrigger = p.vTrigger;
		// As the recording loaded: its costume codes, or none.
		picks.players[side].loadCode = p.loadCode;
		// As the recording was drawn (Tiny fighters, EMBER_LIBRARY.md section 59).
		picks.players[side].tiny = p.size == sfve::picks::kSizeTiny;
	}
	if (picks.players[0].loadCode < 0 || picks.players[1].loadCode < 0) picks.players[0].loadCode = picks.players[1].loadCode = mod::kNoLoadCode;
	picks.rules.stage = sf4e::platform::Utf8ToWide(h.stageCode.c_str());
	const std::optional<int> stage = StageId(picks.rules.stage);
	if (!stage || !StageInstalled(*stage) || !mod::PlayableRounds(h.rounds) || h.timeFrames == 0) return std::nullopt;
	picks.rules.rounds = h.rounds;
	picks.rules.timeFrames = h.timeFrames;
	picks.replay = true;
	return picks;
}

// The playback, from Watch now to the main menu's return. Game thread, but
// for the command queue and the published session.
struct PlaybackState {
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
	unsigned game = 0;
	bool inFight = false;
	int stretches = 0;
	int frames = 0;
	int compared = 0, differing = 0;
	bool stopAsked = false;
};
PlaybackState s_playback;
// s_playback.playing, for other threads (the window's keys).
std::atomic<bool> s_running{false};
rt::CommandQueue s_commands;
std::atomic<uint32_t> s_session{0};
// The stop asked from any thread, under the session it was asked in.
std::mutex s_stopMutex;
std::optional<uint32_t> s_stopFor;

// The pipe's requests, for the game thread's next Tick.
std::mutex s_requestsMutex;
std::vector<sf4e::replay::Request> s_requests;

// What the pipe's status reports, as the game thread last left it.
std::mutex s_statusMutex;
std::string s_statusJson = "null";

class SfvReplaysImpl final : public ember::game::Replays {
public:
	bool Ready() override { return !ReplayFolder().empty(); }
	const sf4e::replaystore::Status& Status() override { return status_; }

	void Start(const sf4e::replay::Request& request, bool atMainMenu, bool noRoom) override
	{
		switch (request.mode) {
		case sf4e::replay::Mode::Watch: Watch(request.path, atMainMenu, noRoom); break;
		case sf4e::replay::Mode::DismissLink: break;
		default:
			// The Replays screen offers SFV nothing else (Capabilities).
			Log("replays: request %d is not one SFV Ember offers", (int)request.mode);
			Notice("replays.not_ready", true);
			break;
		}
	}

	void Tick(bool atMainMenu, bool noRoom) override
	{
		std::vector<sf4e::replay::Request> requests;
		{
			std::lock_guard lock(s_requestsMutex);
			requests.swap(s_requests);
		}
		for (const auto& request : requests) Start(request, atMainMenu, noRoom);
		PlaybackState& p = s_playback;
		if (status_.step == sf4e::replaystore::Step::Playing && p.ended && atMainMenu) {
			// Back at the main menu: Ember's menu comes back on the Replays screen.
			status_.step = sf4e::replaystore::Step::Idle;
			status_.returns++;
			if (p.failed) Notice("replays.not_added", true);
			Mark(p.file, kWatchedList, true);
			p = PlaybackState{};
			lister_.Want();
		}
		PublishStatus();
	}

	void TickPlayback(int deviceType, int deviceIndex, bool) override
	{
		PlaybackState& p = s_playback;
		p.view.playback = p.playing != nullptr;
		p.view.armed = p.view.playback;
		p.view.session = s_session;
		if (!p.playing || deviceType != sf4e::input::PadXInput) return;
		// The pad's own buttons, as the game last read them before the shell
		// hid them from it (XInputHooks): RB, RT, LB, and View to stop.
		const mod::PadSeen seen = mod::LastPadState(deviceIndex);
		uint32_t physical = seen.connected ? sf4e::input::xinput::Physical(seen.buttons) : 0;
		if (seen.leftTrigger) physical |= sf4e::input::xinput::LT;
		if (seen.rightTrigger) physical |= sf4e::input::xinput::RT;
		rt::Command commands[4];
		const int count = p.pad.Sample(physical, commands);
		for (int i = 0; i < count; i++) s_commands.Submit(commands[i], rt::Device::Pad, s_session);
		const bool view = (physical & sf4e::input::xinput::View) != 0;
		if (view && !p.viewHeld) SubmitPlaybackStop(s_session);
		p.viewHeld = view;
	}

	const sf4e::replaytransport::View& Playback() override { return s_playback.view; }
	std::shared_ptr<const sf4e::replaylane::Lanes> PlaybackLanes() override { return nullptr; }

	std::filesystem::path ArchivePath() override { return ReplayFolder(); }
	void WantListing() override { lister_.Want(); }
	std::shared_ptr<const std::vector<ArchivedReplay>> LatestListing() override { return lister_.Latest(); }
	// No Inputs and stats screen for SFV (Capabilities::replayInputs).
	void WantDetail(const std::string&, std::uint64_t) override {}
	sf4e::replayinputs::DetailCompletion LatestDetail() override { return {}; }
	void StopListing() override { lister_.Stop(); }

	void MarkKept(const std::filesystem::path& file, bool kept) override
	{
		if (!InReplayFolder(file) || !Mark(file, kKeptList, kept)) {
			Log("replays: the Keep mark of %ls could not be %s", file.filename().c_str(), kept ? "set" : "taken off");
		}
	}
	// No set's bridge or archive takes SFV's replays (section 26's follow-ups).
	void MarkSent(const std::filesystem::path&) override {}
	void MarkSetClosed(const std::filesystem::path&) override {}
	std::filesystem::path SetPackPath(const std::string&) override { return {}; }
	std::size_t ImportSetPack(const std::filesystem::path&) override { return 0; }

	// For the pipe: the latest listing, asking for the next.
	std::shared_ptr<const std::vector<ArchivedReplay>> Listing()
	{
		lister_.Want();
		return lister_.Latest();
	}

private:
	void Notice(const char* key, bool error)
	{
		status_.notice = key ? sf4e::loc::T(key) : "";
		status_.noticeError = error;
	}

	void Watch(const std::string& file, bool atMainMenu, bool noRoom)
	{
		// As SF4's Watch now: at the main menu, with no room, nothing else playing.
		if (status_.step != sf4e::replaystore::Step::Idle || !atMainMenu || !noRoom || s_playback.armed || s_playback.playing) {
			Notice("replays.not_ready", true);
			return;
		}
		const fs::path path = sf4e::platform::Utf8ToWide(file.c_str());
		auto loaded = std::make_shared<replay::Replay>();
		std::string why;
		const auto bytes = InReplayFolder(path) ? ReadFile(path, replay::kMostFileBytes) : std::nullopt;
		if (!bytes) {
			why = "not a file in the replay folder that can be read";
		} else if (!replay::Decode(bytes->data(), bytes->size(), loaded.get(), &why)) {
			// why says it
		} else if (loaded->header.build != MatchBuildId() && !mod::Config().replayAnyBuild) {
			why = "made with build " + loaded->header.build + ", this copy is " + MatchBuildId();
		} else if (loaded->frames.empty()) {
			why = "it holds no frame";
		}
		std::optional<mod::MatchPicks> picks = why.empty() ? PicksOf(loaded->header) : std::nullopt;
		if (why.empty() && !picks) why = "its fighters, stage or rules are not ones this copy can launch, or its stage is not installed here";
		if (!why.empty()) {
			Log("replays: %ls cannot be watched: %s", path.filename().c_str(), why.c_str());
			Notice("inputs.unreadable_detail", true);
			return;
		}
		if (loaded->header.build != MatchBuildId()) {
			// Development diagnostics only (a player build never reads the option).
			Log("replays: %ls was made with build %s, this copy is %s; played anyway (-sfve_replay_any_build), so its "
				"checkpoints are a comparison, not a verdict", path.filename().c_str(), loaded->header.build.c_str(),
				MatchBuildId().c_str());
		}
		Log("replays: watching %ls: %s vs %s on %s, %zu frame(s), %zu game(s), %zu checkpoint(s)", path.filename().c_str(),
			loaded->header.players[0].code.c_str(), loaded->header.players[1].code.c_str(), loaded->header.stageCode.c_str(),
			loaded->frames.size(), loaded->header.games.size(), loaded->checkpoints.size());
		s_playback = PlaybackState{};
		s_playback.armed = std::move(loaded);
		s_playback.file = path;
		status_.step = sf4e::replaystore::Step::Playing;
		status_.logOpens++; // Ember's menu gets out of the way
		Notice(nullptr, false);
		mod::QueueLaunch(*picks, [](const std::string& launchWhy) {
			// The game thread: no battle came. The menu comes back with why.
			Log("replays: the replay's battle could not be launched: %s", launchWhy.c_str());
			s_playback.armed.reset();
			s_playback.failed = s_playback.ended = true;
			s_playback.why = launchWhy;
		});
	}

	void PublishStatus()
	{
		const PlaybackState& p = s_playback;
		nlohmann::json j = {{"step", (int)status_.step}, {"armed", p.armed != nullptr}, {"playing", p.playing != nullptr},
			{"session", s_session.load()}, {"paused", p.transport.Paused()}, {"divisor", p.transport.Divisor()},
			{"round", p.view.round}, {"cursor", p.view.cursor}, {"frames", p.frames}, {"compared", p.compared},
			{"differing", p.differing}, {"notice", status_.notice}, {"logOpens", status_.logOpens},
			{"returns", status_.returns}, {"last", last_}};
		std::lock_guard lock(s_statusMutex);
		s_statusJson = j.dump();
	}

public:
	std::string last_ = "none"; // how the last playback ended

private:
	sf4e::replaystore::Status status_;
	Lister lister_;
};

SfvReplaysImpl& Impl()
{
	static SfvReplaysImpl replays;
	return replays;
}

} // namespace

ember::game::Replays& SfvReplays()
{
	return Impl();
}

fs::path ReplayFolder()
{
	// The tests' and test tools' own folder, so their battles stay out of
	// the player's.
	wchar_t named[MAX_PATH];
	const DWORD length = GetEnvironmentVariableW(L"SFVE_REPLAY_DIR", named, MAX_PATH);
	if (length > 0 && length < MAX_PATH) return fs::path(named);
	PWSTR appData = nullptr;
	fs::path folder;
	if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData) == S_OK) {
		folder = fs::path(appData) / ember::Product().appDataFolder / L"replays";
	}
	CoTaskMemFree(appData);
	return folder;
}

bool RecordingHeader(int slot, std::uint32_t seed, int delay, replay::Header* out)
{
	const auto& netplay = ember::RuntimeSession::netplay;
	const std::optional<RoomMatch> room = RoomMatchNow();
	const std::optional<mod::MatchPicks> picks = room ? LaunchPicks(*room).picks : std::nullopt;
	if (!netplay || !room || !picks) return false;
	replay::Header h;
	h.product = "SFV Ember";
	h.build = MatchBuildId();
	h.startedAt = (uint64_t)_time64(nullptr);
	h.slot = slot;
	for (int side = 0; side < 2; side++) {
		replay::Player& p = h.players[side];
		p.name = netplay->matchSides[side].name;
		p.fighter = room->picks[side].fighter;
		p.costume = room->picks[side].costume;
		p.colour = room->picks[side].color;
		p.code = sf4e::platform::WideToUtf8(picks->players[side].fighter);
		p.launchCostume = picks->players[side].costume;
		p.launchColour = picks->players[side].colour;
		p.vSkill = picks->players[side].vSkill;
		p.vTrigger = picks->players[side].vTrigger;
		p.loadCode = picks->players[side].loadCode;
		p.size = picks->players[side].tiny ? sfve::picks::kSizeTiny : sfve::picks::kSizeNormal;
	}
	h.stage = room->stage;
	h.stageCode = sf4e::platform::WideToUtf8(picks->rules.stage);
	h.rounds = picks->rules.rounds;
	h.roundSeconds = room->roundSeconds;
	h.timeFrames = picks->rules.timeFrames;
	h.seed = seed;
	h.inputDelay = delay;
	*out = std::move(h);
	return true;
}

fs::path SaveReplay(replay::Replay r)
{
	replay::CountFights(r.frames, &r.header.fightFrames, &r.header.fightStretches);
	const std::vector<uint8_t> bytes = replay::Encode(r);
	const fs::path folder = ReplayFolder();
	std::error_code error;
	if (folder.empty() || (!fs::create_directories(folder, error) && error)) {
		Log("replays: no replay folder (%s); the battle's replay is not saved", error.message().c_str());
		return {};
	}
	// Named by its start (UTC) and its hash, as SF4 Ember names its archive's
	// files by save time and CRC: two copies' replays of one battle differ.
	std::tm utc = {};
	const std::time_t at = (std::time_t)r.header.startedAt;
	char stamp[32] = "00000000-000000";
	if (!gmtime_s(&utc, &at)) std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &utc);
	char name[64];
	snprintf(name, sizeof(name), "%s-%08x", stamp, (unsigned)XXH64(bytes.data(), bytes.size(), 0));
	const fs::path file = folder / (std::wstring(sf4e::platform::Utf8ToWide(name)) + replay::kExtension);
	const auto written = sf4e::durable::PublishReplace(file, sf4e::durable::PartialPath(file), bytes.data(), bytes.size());
	if (!written) {
		Log("replays: %ls could not be written (step %d, error %lu)", file.filename().c_str(), (int)written.failed,
			written.error);
		return {};
	}
	Log("replays: saved %ls: %zu frame(s), %zu game(s), %zu checkpoint(s), %s, %zu bytes", file.filename().c_str(),
		r.frames.size(), r.header.games.size(), r.checkpoints.size(), r.header.complete ? "complete" : "cut short",
		bytes.size());
	Impl().WantListing();
	return file;
}

std::shared_ptr<const replay::Replay> TakeArmedReplay()
{
	PlaybackState& p = s_playback;
	if (!p.armed) return nullptr;
	p.playing = std::move(p.armed);
	s_running = true;
	p.transport.Reset();
	p.pad.Reset();
	p.view = rt::View{};
	p.view.session = ++s_session;
	p.view.playback = p.view.armed = true;
	s_commands.Clear();
	return p.playing;
}

bool PlaybackFrameWanted()
{
	PlaybackState& p = s_playback;
	if (!p.playing) return true;
	for (const auto& entry : s_commands.Take(p.view.session)) {
		p.view.device = entry.device;
		p.view.taken++;
		if (rt::MovesTransport(entry.command)) p.transport.Apply(entry.command);
	}
	{
		std::lock_guard lock(s_stopMutex);
		if (s_stopFor && *s_stopFor == p.view.session) p.stopAsked = true;
		s_stopFor.reset();
	}
	p.view.paused = p.transport.Paused();
	p.view.divisor = p.transport.Divisor();
	return !p.transport.Hold();
}

bool TakePlaybackStop()
{
	PlaybackState& p = s_playback;
	const bool asked = p.playing && p.stopAsked;
	p.stopAsked = false;
	return asked;
}

void NotePlaybackFrame(bool fight, unsigned game)
{
	PlaybackState& p = s_playback;
	if (!p.playing) return;
	p.frames++;
	if (game != p.game) {
		// A game in place starts its rounds over.
		p.game = game;
		p.stretches = 0;
		p.inFight = false;
		p.view.round = 0;
		p.view.cursor = 0;
	}
	if (fight && !p.inFight) {
		// A round's fight begins: its clock starts over.
		p.view.round = p.stretches++;
		p.view.cursor = 0;
	}
	if (fight) p.view.cursor++;
	p.inFight = fight;
}

void NotePlaybackCheckpoint(int frame, std::uint64_t hash)
{
	PlaybackState& p = s_playback;
	if (!p.playing) return;
	const auto& recorded = p.playing->checkpoints;
	const auto it = std::lower_bound(recorded.begin(), recorded.end(), frame,
		[](const std::pair<int, uint64_t>& c, int f) { return c.first < f; });
	if (it == recorded.end() || it->first != frame) return;
	p.compared++;
	if (it->second != hash) {
		if (p.differing++ == 0) {
			Log("replays: desync: checkpoint frame %d hash %016llx, the recording's %016llx", frame, (unsigned long long)hash,
				(unsigned long long)it->second);
		}
	}
}

void NotePlaybackStart(std::uint64_t recorded, std::uint64_t played)
{
	PlaybackState& p = s_playback;
	if (!p.playing || !recorded) return;
	p.compared++;
	if (recorded == played) {
		Log("replays: the playback starts from the recording's start state (%016llx)", (unsigned long long)played);
		return;
	}
	p.differing++;
	Log("replays: desync: the playback starts from state %016llx, P1's recorded battle from %016llx; its frames may play "
		"another fight", (unsigned long long)played, (unsigned long long)recorded);
}

void EndPlayback(const std::string& why)
{
	PlaybackState& p = s_playback;
	if (!p.playing) return;
	const bool whole = (size_t)p.frames == p.playing->frames.size();
	p.failed = p.differing > 0 || (!whole && why.find("stopped") == std::string::npos);
	char line[320];
	snprintf(line, sizeof(line),
		"%s; %d of %zu frame(s) played, %d checkpoint(s) compared with the recording, %d differing", why.c_str(), p.frames,
		p.playing->frames.size(), p.compared, p.differing);
	Log("replays: playback ended: %s", line);
	Impl().last_ = line;
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

void SubmitPlaybackStop(std::uint32_t session)
{
	std::lock_guard lock(s_stopMutex);
	s_stopFor = session;
}

void RegisterReplayCommands()
{
	mod::RegisterStatus("replays", [] {
		std::lock_guard lock(s_statusMutex);
		return s_statusJson;
	});
	mod::RegisterCommand("replays", [](const std::string&) {
		const auto listing = Impl().Listing();
		if (!listing) return std::string("error no listing yet");
		nlohmann::json list = nlohmann::json::array();
		for (const ArchivedReplay& r : *listing) {
			nlohmann::json entry = {{"file", sf4e::platform::WideToUtf8(r.path.wstring())}, {"label", r.label},
				{"names", {r.names[0], r.names[1]}}, {"fighters", {r.fighters[0], r.fighters[1]}},
				{"spectated", r.provenance == sf4e::replayfiles::Provenance::Spectated}, {"kept", r.kept},
				{"watched", r.watched}};
			if (r.summary) {
				entry["score"] = {r.summary->score[0], r.summary->score[1]};
				entry["scored"] = r.summary->scored;
				entry["rounds"] = r.summary->rounds;
				entry["frames"] = r.summary->frames;
			}
			list.push_back(entry);
		}
		return "ok " + list.dump();
	});
	mod::RegisterCommand("replay", [](const std::string& args) {
		const size_t space = args.find(' ');
		const std::string verb = args.substr(0, space);
		const std::string rest = space == std::string::npos ? std::string() : args.substr(space + 1);
		const uint32_t session = s_session;
		if (verb == "pause") SubmitPlaybackCommand(rt::Command::TogglePause, rt::Device::Keyboard, session);
		else if (verb == "step") SubmitPlaybackCommand(rt::Command::Step, rt::Device::Keyboard, session);
		else if (verb == "slower") SubmitPlaybackCommand(rt::Command::Slower, rt::Device::Keyboard, session);
		else if (verb == "faster") SubmitPlaybackCommand(rt::Command::Faster, rt::Device::Keyboard, session);
		else if (verb == "stop") SubmitPlaybackStop(session);
		else if (verb == "watch" || verb == "keep") {
			sf4e::replay::Request request;
			std::string file = rest;
			if (verb == "keep") {
				const size_t last = rest.rfind(' ');
				if (last == std::string::npos) return std::string("error usage: replay keep <file> 0|1");
				file = rest.substr(0, last);
				request.keep = rest.substr(last + 1) == "1";
			}
			if (file == "latest") {
				const auto listing = Impl().Listing();
				if (!listing || listing->empty()) return std::string("error no replay listed");
				file = sf4e::platform::WideToUtf8(listing->front().path.wstring());
			}
			if (file.empty()) return std::string("error usage: replay watch <file|latest>");
			request.mode = verb == "watch" ? sf4e::replay::Mode::Watch : sf4e::replay::Mode::Keep;
			request.path = file;
			if (request.mode == sf4e::replay::Mode::Keep) {
				mod::RunOnGameThread([request] { Impl().MarkKept(sf4e::platform::Utf8ToWide(request.path.c_str()), request.keep); });
				return std::string("ok queued");
			}
			std::lock_guard lock(s_requestsMutex);
			s_requests.push_back(std::move(request));
			return "ok queued " + file;
		} else {
			return std::string("error usage: replay watch <file|latest> | keep <file> 0|1 | pause | step | slower | faster | stop");
		}
		return "ok session " + std::to_string(session);
	});
}

} // namespace sfve::adapter
