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
#include "sf4e/sf4e__RuntimeSession.hxx"

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
	if (const std::vector<uint8_t>* state = session.CheckpointState(mismatch.frame)) {
		for (const StateRegion& r : session.Layout().Regions()) {
			facts.stateRegions.push_back({r.name, r.size, reinterpret_cast<std::uint64_t>(r.ptr)});
		}
		facts.state = *state;
	}
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

// This copy's state at the checkpoint, in its own folder beside the reports
// (desync::StateFile), whether or not reports are sent; the oldest beyond
// kStatesKept go.
void WriteState(const fs::path& settings, const std::wstring& stem, const desync::Facts& facts)
{
	const std::string bytes = desync::StateFile(facts);
	if (bytes.empty()) {
		mod::Log("desync report: the state at checkpoint %d was no longer kept; no state file", facts.frame);
		return;
	}
	const fs::path folder = settings / desync::kStateFolder;
	std::error_code ec;
	fs::create_directories(folder, ec);
	std::vector<fs::path> kept;
	for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
		if (entry.path().extension() == desync::kStateExtension) kept.push_back(entry.path());
	}
	std::sort(kept.begin(), kept.end()); // names start with the time
	for (std::size_t i = 0; i + desync::kStatesKept <= kept.size(); ++i) fs::remove(kept[i], ec);
	const fs::path target = folder / (stem + desync::kStateExtension);
	std::ofstream file(target, std::ios::binary | std::ios::trunc);
	file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	file.close();
	if (!file) {
		fs::remove(target, ec);
		mod::Log("desync report: could not write %ls", target.c_str());
		return;
	}
	mod::Log("desync report: this copy's state at checkpoint %d is %ls (%zu bytes); compare it with the other player's "
		"(tools/diffstate.py)", facts.frame, target.c_str(), bytes.size());
}

// The report's own thread: writes one gathered report down for the launcher.
void WriteDown(const desync::Facts& facts)
{
	const std::wstring settings = sf4e::netplay::SettingsStore::DefaultDirectory();
	if (settings.empty()) {
		mod::Log("desync report: no settings folder, so nothing is written");
		return;
	}
	const long long now =
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	const std::wstring stem =
		std::to_wstring(now) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(facts.frame);
	WriteState(fs::path(settings), stem, facts);
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
	// The newest report is the one worth keeping: when the launcher has not
	// sent the waiting ones (its daily limit), the oldest go to make room.
	const desync::Room room = desync::MakeRoom(folder, desync::kSpoolLimit);
	for (const std::wstring& name : room.removed) {
		mod::Log("desync report: %zu reports wait for the launcher; the oldest, %ls, is removed to keep this one",
			desync::kSpoolLimit, name.c_str());
	}
	if (room.waiting >= desync::kSpoolLimit) {
		mod::Log("desync report: %zu reports wait for the launcher and none can be removed; this one is not written", room.waiting);
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
	spooled.time = now;
	spooled.watcher = facts.watcher;
	spooled.comment = desync::Comment(facts, spooled.buildId);
	spooled.regionLines = desync::RegionLines(facts);
	spooled.logTail = LogTail(facts.frame);

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

// Hands gathered facts to the report's own thread.
void Queue(desync::Facts facts);

} // namespace

void ReportDesync(const RollbackSession& session, const sf4e::SessionClient& client,
	const CheckpointExchange::Mismatch& mismatch, bool player)
{
	desync::Facts facts = Gather(session, client, mismatch, player);
	mod::Log("desync report: checkpoint %d, room %016llx table %d match %llu, %zu regions; written down off the game thread",
		facts.frame, (unsigned long long)facts.roomEpoch, facts.table, (unsigned long long)facts.generation,
		facts.regions.size());
	Queue(std::move(facts));
}

void ReportStartRepair(const StartAgreement::Report& report, const StateLayout& layout, bool watcher, int delay)
{
	desync::Facts facts;
	facts.watcher = watcher;
	if (const RoomBattle* room = CurrentBattle().room) {
		facts.slot = static_cast<int>(room->LocalSlot());
		facts.gamesEnded = room->GamesEnded();
	}
	if (const auto& netplay = ember::RuntimeSession::netplay) RoomFacts(facts, netplay->client.GetRoomSnapshot());
	PickFacts(facts);
	facts.frame = 0;
	facts.local = report.localBefore;
	facts.remote = report.reference;
	facts.confirmed = -1;
	facts.delay = delay;
	facts.layoutId = layout.LayoutId();
	facts.matchBuild = MatchBuildId();
	size_t written = 0, kept = 0, still = 0;
	for (const StartAgreement::RegionOutcome& r : report.regions) {
		facts.regions.push_back({r.name, r.size, r.before});
		written += r.unitsWritten;
		kept += r.unitsKept;
		if (r.after != r.reference) still++;
	}
	if (report.stateBefore.size() == layout.TotalSize()) {
		for (const StateRegion& r : layout.Regions()) {
			facts.stateRegions.push_back({r.name, r.size, reinterpret_cast<std::uint64_t>(r.ptr)});
		}
		facts.state = report.stateBefore;
	}
	facts.startRepair = std::to_string(report.regions.size()) + " region(s) differed from P1's: " + std::to_string(written) +
		" dword(s) taken from P1, " + std::to_string(kept) + " left as they were (addresses, masked words); " +
		std::to_string(still) + " region(s) still differ, " + std::to_string(report.unpaired.size()) +
		" unpaired. Result: " + StartAgreement::ResultName(report.result) + ", now " +
		(report.localAfter == report.reference ? "the same as P1's." : "still not P1's.");
	mod::Log("desync report: the start state's repair, %zu region(s); written down off the game thread", facts.regions.size());
	Queue(std::move(facts));
}

namespace {

void Queue(desync::Facts facts)
{
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

} // namespace

} // namespace sfve::adapter
