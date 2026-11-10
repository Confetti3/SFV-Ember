#include "ember/DesyncReporter.hxx"

#include "ember/DesyncReport.hxx"
#include "ember/RoomBattle.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvPickSpace.hxx"
#include "mod/Log.hxx"

#include "launcher/update/PackageInstaller.hxx"
#include "launcher/update/github_release_client.hxx"
#include "netplay/SettingsStore.hxx"
#include "platform/ReportWorkflow.hxx"
#include "session/sf4e__SessionClient.hxx"

// SFV Ember's source revision (CMakeLists.txt, sfve_build_identity).
#include "SfvBuildIdentity.hxx"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace sfve::adapter {

namespace {

namespace fs = std::filesystem;

// How much of the mod's log is read for the tail: kTailLines lines and room
// to spare.
constexpr std::size_t kLogReadBytes = 256 * 1024;
// Reports gathered and not yet written; more are dropped (the check finds at
// most one a battle, so this is never reached in play).
constexpr std::size_t kQueued = 2;

void PickFacts(desync::Facts& facts)
{
	const std::optional<RoomMatch> match = RoomMatchNow();
	if (!match) return;
	facts.picksKnown = true;
	for (int side = 0; side < 2; ++side) {
		const sf4e::selection::Pick& pick = match->picks[side];
		desync::FighterFacts& fighter = facts.fighters[side];
		fighter.code = FighterCode(pick.fighter);
		fighter.costume = pick.costume;
		fighter.colour = pick.color;
		if (const std::optional<picks::Loadout> loadout = picks::LoadoutIn(pick)) {
			fighter.vSkill = loadout->choice[picks::VSkill];
			fighter.vTrigger = loadout->choice[picks::VTrigger];
		}
	}
	facts.stage = StageCode(match->stage);
}

// The room's match this copy plays or watches: the table whose game the
// room froze this copy into (both players' and the watchers' copies name
// the same generation), else the table it sits or watches at.
void RoomFacts(desync::Facts& facts, const sf4e::room::Snapshot& room)
{
	facts.roomEpoch = room.roomEpoch;
	for (std::size_t t = 0; t < room.tables.size(); ++t) {
		if (!room.localMatchGenerations[t]) continue;
		facts.table = room.tables[t].id;
		facts.generation = room.localMatchGenerations[t];
		return;
	}
	if (!room.localMember) return;
	for (const sf4e::room::Table& table : room.tables) {
		const bool watches =
			std::find(table.spectators.begin(), table.spectators.end(), room.localMember) != table.spectators.end();
		if (table.p1 != room.localMember && table.p2 != room.localMember && !watches) continue;
		facts.table = table.id;
		facts.generation = table.matchGeneration;
		return;
	}
}

desync::Facts Gather(const RollbackSession& session, const sf4e::SessionClient& client,
	const CheckpointExchange::Mismatch& mismatch, bool player)
{
	desync::Facts facts;
	facts.watcher = !player;
	if (const RoomBattle* room = CurrentBattle().room) {
		facts.slot = static_cast<int>(room->LocalSlot());
		facts.gamesEnded = room->GamesEnded();
	}
	RoomFacts(facts, client.GetRoomSnapshot());
	PickFacts(facts);
	facts.frame = mismatch.frame;
	facts.local = mismatch.local;
	facts.remote = mismatch.remote;
	facts.confirmed = session.LastConfirmedFrame();
	facts.delay = session.AppliedDelay();
	facts.rollbacks = session.GetStats().rollbacks;
	facts.longestRollback = session.GetStats().longestRollback;
	facts.layoutId = session.Layout().LayoutId();
	facts.matchBuild = MatchBuildId();
	std::vector<RegionHash> hashes;
	if (session.RegionHashesAt(mismatch.frame, &hashes)) {
		const std::vector<StateRegion>& regions = session.Layout().Regions();
		for (const RegionHash& h : hashes) {
			facts.regions.push_back({h.region < regions.size() ? regions[h.region].name : std::string("?"), h.size, h.hash});
		}
	}
	return facts;
}

// sfv_ember.dll's SHA-256, the build id the launcher's crash reports send
// too; empty when it cannot be read.
std::string ModuleHash()
{
	static const int anchor = 0; // an address inside the mod
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&anchor), &module)) {
		return {};
	}
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return {};
	try {
		return sf4e::launcher::Sha256Hex(path);
	} catch (...) {
		return {};
	}
}

// The mod's log lines before the desync (desync::TailLines).
std::string LogTail(int frame)
{
	const std::wstring& path = mod::CurrentLogTarget().path;
	if (path.empty()) return {};
	std::ifstream file(fs::path(path), std::ios::binary | std::ios::ate);
	if (!file) return {};
	const std::streamoff end = file.tellg();
	if (end < 0) return {};
	const std::size_t size = static_cast<std::size_t>(end);
	const std::size_t kept = (std::min)(size, kLogReadBytes);
	std::string bytes(kept, '\0');
	file.seekg(static_cast<std::streamoff>(size - kept));
	if (kept && !file.read(bytes.data(), static_cast<std::streamsize>(kept))) return {};
	return desync::TailLines(bytes, size > kept, frame);
}

std::size_t ReportsWaiting(const fs::path& folder)
{
	std::size_t count = 0;
	std::error_code ec;
	for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
		const std::wstring name = it->path().filename().wstring();
		const std::wstring extension = desync::kSpoolExtension;
		if (name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
			count++;
		}
	}
	return count;
}

// The report's own thread: writes one gathered report down for the launcher.
void WriteDown(const desync::Facts& facts)
{
	const std::wstring settings = sf4e::netplay::SettingsStore::DefaultDirectory();
	if (settings.empty()) {
		mod::Log("desync report: no settings folder, so nothing is written");
		return;
	}
	const std::optional<bool> on = sf4e::reports::ReportsOn(settings);
	if (!on || !*on) {
		mod::Log("desync report: %s, so this desync's report is not written", on ? "Send problem reports is off" : "the settings cannot be read");
		return;
	}
	const fs::path folder = fs::path(settings) / desync::kSpoolFolder;
	std::error_code ec;
	fs::create_directories(folder, ec);
	if (ec) {
		mod::Log("desync report: could not make %ls (%s)", folder.c_str(), ec.message().c_str());
		return;
	}
	const std::size_t waiting = ReportsWaiting(folder);
	if (waiting >= desync::kSpoolLimit) {
		mod::Log("desync report: %zu reports already wait for the launcher; this one is not written", waiting);
		return;
	}
	desync::Spooled spooled;
	spooled.buildId = ModuleHash();
	if (spooled.buildId.size() != 64) {
		mod::Log("desync report: the mod's build id could not be read; not written");
		return;
	}
	char version[64] = {};
	sf4e::launcher::ReadInstalledVersion(version, static_cast<int>(sizeof(version)));
	spooled.appVersion = version[0] ? version : "unknown";
	spooled.sourceRevision = SF4E_SOURCE_REVISION;
	spooled.time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	spooled.watcher = facts.watcher;
	spooled.comment = desync::Comment(facts, spooled.buildId);
	spooled.regionLines = desync::RegionLines(facts);
	spooled.logTail = LogTail(facts.frame);

	const std::wstring stem = std::to_wstring(spooled.time) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
		std::to_wstring(facts.frame);
	const fs::path target = folder / (stem + desync::kSpoolExtension);
	const fs::path partial = folder / (stem + L".tmp");
	{
		std::ofstream file(partial, std::ios::binary | std::ios::trunc);
		file << desync::SpoolJson(spooled);
		file.close();
		if (!file) {
			fs::remove(partial, ec);
			mod::Log("desync report: could not write %ls", partial.c_str());
			return;
		}
	}
	// Whole or not at all: the launcher takes only the final name.
	if (!MoveFileExW(partial.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		mod::Log("desync report: could not name %ls (Win32 %lu)", target.c_str(), GetLastError());
		fs::remove(partial, ec);
		return;
	}
	mod::Log("desync report: written as %ls (%zu regions, %zu bytes of the log's tail); the launcher sends it",
		target.c_str(), facts.regions.size(), spooled.logTail.size());
}

// The thread and its queue live for the process: the thread may still be
// waiting as the game exits, so nothing of it is ever destroyed.
struct Writer {
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<desync::Facts> queue;
	bool started = false;
};

Writer& TheWriter()
{
	static Writer* writer = new Writer;
	return *writer;
}

void Run()
{
	Writer& writer = TheWriter();
	for (;;) {
		desync::Facts facts;
		{
			std::unique_lock lock(writer.mutex);
			writer.wake.wait(lock, [&] { return !writer.queue.empty(); });
			facts = std::move(writer.queue.front());
			writer.queue.pop_front();
		}
		try {
			WriteDown(facts);
		} catch (...) {
			mod::Log("desync report: writing it failed; not written");
		}
	}
}

} // namespace

void ReportDesync(const RollbackSession& session, const sf4e::SessionClient& client,
	const CheckpointExchange::Mismatch& mismatch, bool player)
{
	desync::Facts facts = Gather(session, client, mismatch, player);
	mod::Log("desync report: checkpoint %d, room %016llx table %d match %llu, %zu regions; written down off the game thread",
		facts.frame, (unsigned long long)facts.roomEpoch, facts.table, (unsigned long long)facts.generation,
		facts.regions.size());
	Writer& writer = TheWriter();
	std::lock_guard lock(writer.mutex);
	if (writer.queue.size() >= kQueued) {
		mod::Log("desync report: %zu reports still wait to be written; this one is dropped", writer.queue.size());
		return;
	}
	if (!writer.started) {
		try {
			std::thread(&Run).detach();
		} catch (const std::system_error& error) {
			mod::Log("desync report: no thread to write it (%s); not written", error.what());
			return;
		}
		writer.started = true;
	}
	writer.queue.push_back(std::move(facts));
	writer.wake.notify_one();
}

} // namespace sfve::adapter
