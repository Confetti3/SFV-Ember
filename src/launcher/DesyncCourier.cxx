#include "launcher/DesyncCourier.hxx"

#include "ember/DesyncReport.hxx"

#include "common/ProductIdentity.hxx"
#include "common/sf4e__NetUtil.hxx"
#include "launcher/update/PackageInstaller.hxx"
#include "launcher/update/github_release_client.hxx"
#include "netplay/SettingsStore.hxx"
#include "platform/ProblemReport.hxx"
#include "platform/ReportHistory.hxx"
#include "platform/ReportWorkflow.hxx"
#include "platform/UiPreferencesStore.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace sfve::launcher {

namespace {

namespace fs = std::filesystem;
namespace desync = sfve::adapter::desync;
namespace reports = sf4e::reports;

// A pass over the waiting reports: a report the game wrote is on its way
// within this many seconds, which is as soon as its fight ended (a fighter's
// mismatch ends the fight). The pass is a directory listing when none waits.
constexpr std::chrono::seconds kPassInterval{5};
// Tries of one report in one launcher's life that came to nothing.
constexpr int kTriesPerRun = 5;
// After an answer that asks for patience (a busy intake, a rate limit, no
// connection) nothing is sent for this long.
constexpr std::int64_t kPauseSeconds = 120;
// Larger than any report the game writes (its log's tail is at most 256 KiB).
constexpr std::uintmax_t kSpoolFileBytes = 1024 * 1024;
// A state file: 1 MB of regions and a census of at most 65536 pointer words.
constexpr std::uintmax_t kStateFileBytes = 6 * 1024 * 1024;
// What the intake's body limit (5 MiB, ProblemReport.hxx) leaves for the
// deflated state beside the report's log.
constexpr std::size_t kBodyLimit = 5 * 1024 * 1024;
constexpr std::size_t kDeflatedStateLimit = 2 * 1024 * 1024;

std::int64_t Now()
{
	return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Narrow(const wchar_t* name)
{
	std::string text;
	for (; *name; ++name) text += static_cast<char>(*name); // the product's log names are ASCII
	return text;
}

std::string WindowsVersion()
{
	OSVERSIONINFOW version{};
	version.dwOSVersionInfoSize = sizeof(version);
	const auto rtl =
		reinterpret_cast<LONG(WINAPI*)(OSVERSIONINFOW*)>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
	if (!rtl || rtl(&version) != 0) return "Windows (version unavailable)";
	return "Windows " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + " build " +
		std::to_string(version.dwBuildNumber);
}

// The intake's name for the channel this launcher updates from, as a crash
// report sends it.
std::string Channel()
{
	char installed[64] = {};
	sf4e::launcher::ReadInstalledVersion(installed, static_cast<int>(sizeof(installed)));
	return reports::ReportChannel(sf4e::launcher::ResolveUpdateChannel(sf4e::platform::UpdateChannelPreference(), installed));
}

std::string Boundary()
{
	std::random_device random;
	std::string boundary = "EmberDesync";
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < 32; ++i) boundary += digits[random() % 16];
	return boundary;
}

// The tail with the player's names and the rest a crash report's logs lose
// taken out, line by line, as valid UTF-8.
std::string Redacted(const std::string& tail, const reports::Account& account)
{
	std::string text;
	for (std::size_t start = 0; start < tail.size();) {
		const std::size_t end = tail.find('\n', start);
		text += reports::RedactLine(std::string_view(tail).substr(start, end == std::string::npos ? std::string::npos : end - start), account);
		text += '\n';
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return reports::Utf8Tail(text, desync::kLogBytes);
}

// The installed build's id: the SHA-256 of the sfv_ember.dll beside this
// launcher, which is what the game hashes of itself for a report's build id
// (DesyncReporter's ModuleHash). Empty when it cannot be read.
std::string InstalledBuildId()
{
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return {};
	try {
		return sf4e::launcher::Sha256Hex(fs::path(path).parent_path() / ember::Product().gameModule);
	} catch (...) {
		return {};
	}
}

// The installed module's size and modification time: the installed build id
// is hashed again when either changes (an update replaces the DLL while the
// launcher may still run), so a report of the freshly updated build is not
// deleted as another build's.
struct ModuleStamp {
	std::uintmax_t size = 0;
	fs::file_time_type time{};
	bool operator==(const ModuleStamp&) const = default;
};
std::optional<ModuleStamp> StampOf(const fs::path& dll)
{
	std::error_code ec;
	const std::uintmax_t size = fs::file_size(dll, ec);
	if (ec) return std::nullopt;
	const fs::file_time_type time = fs::last_write_time(dll, ec);
	if (ec) return std::nullopt;
	return ModuleStamp{size, time};
}

// The state file a report names: the report's own name with the state's
// extension (the game writes both under one stem), read whole; empty when it
// is gone (it is kept for the newest kStatesKept only), too large or unreadable.
std::string ReadState(const fs::path& settings, const fs::path& report)
{
	std::wstring name = report.filename().wstring();
	const std::wstring extension = desync::kSpoolExtension;
	if (name.size() <= extension.size()) return {};
	name.resize(name.size() - extension.size());
	const fs::path path = settings / desync::kStateFolder / (name + desync::kStateExtension);
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) return {};
	const std::streamoff size = file.tellg();
	if (size <= 0 || static_cast<std::uintmax_t>(size) > kStateFileBytes) return {};
	std::string bytes(static_cast<std::size_t>(size), '\0');
	file.seekg(0);
	if (!file.read(bytes.data(), size)) return {};
	return bytes;
}

// A zlib stream of `raw` (about 1 MB raw gives about 15 KB: its regions are
// mostly zeros); empty when it fails or does not fit the limit.
std::string Deflate(const std::string& raw)
{
	uLongf size = compressBound(static_cast<uLong>(raw.size()));
	std::string out(size, '\0');
	if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(raw.data()),
			static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK) {
		return {};
	}
	out.resize(size);
	return out.size() <= kDeflatedStateLimit ? out : std::string();
}

// A report file held open by this launcher alone, deleted when it is settled.
class HeldFile {
public:
	explicit HeldFile(const fs::path& path)
		: handle_(CreateFileW(path.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
	{
	}
	~HeldFile()
	{
		if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
	}
	HeldFile(const HeldFile&) = delete;
	HeldFile& operator=(const HeldFile&) = delete;
	bool Held() const { return handle_ != INVALID_HANDLE_VALUE; }
	// The whole file, or nothing when it is larger than `limit` or unreadable.
	std::optional<std::string> Read(std::uintmax_t limit) const
	{
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(handle_, &size) || size.QuadPart < 0 || static_cast<std::uintmax_t>(size.QuadPart) > limit) {
			return std::nullopt;
		}
		std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		if (!bytes.empty() && (!ReadFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())) {
			return std::nullopt;
		}
		return bytes;
	}
	void Delete() const
	{
		FILE_DISPOSITION_INFO disposition{TRUE}; // its one member, DeleteFile, which a macro of the same name shadows
		if (!SetFileInformationByHandle(handle_, FileDispositionInfo, &disposition, sizeof(disposition))) {
			spdlog::warn("Desync report: could not delete a settled report (Win32 {})", GetLastError());
		}
	}

private:
	HANDLE handle_;
};

class Courier {
public:
	~Courier()
	{
		{
			std::lock_guard lock(mutex_);
			stop_ = true;
		}
		cancelled_ = true;
		wake_.notify_all();
		if (thread_.joinable()) thread_.join();
	}

	void Start()
	{
		std::lock_guard lock(mutex_);
		if (thread_.joinable()) {
			pending_ = true;
			wake_.notify_all();
			return;
		}
		try {
			thread_ = std::thread(&Courier::Run, this);
		} catch (const std::system_error& error) {
			spdlog::warn("Desync report: no thread to send reports ({}); they wait for the next launcher", error.what());
		}
	}

	void Wake()
	{
		std::lock_guard lock(mutex_);
		pending_ = true;
		wake_.notify_all();
	}

private:
	void Run()
	{
		for (;;) {
			try {
				Pass();
			} catch (...) {
				spdlog::warn("Desync report: a pass over the waiting reports failed");
			}
			std::unique_lock lock(mutex_);
			wake_.wait_for(lock, kPassInterval, [&] { return stop_ || pending_; });
			if (stop_) return;
			pending_ = false;
		}
	}

	void Pass()
	{
		const std::wstring settings = sf4e::netplay::SettingsStore::DefaultDirectory();
		if (settings.empty()) return;
		// Paused or at a cap: nothing is read or parsed this pass (the next
		// one looks again), where the sends below would all stop anyway.
		if (Now() < pauseUntil_) return;
		const std::int64_t wait = Wait(settings);
		if (wait > 0) {
			if (!limitLogged_) {
				spdlog::info("Desync reports: the sending limit is reached; they wait {} s", wait);
				limitLogged_ = true;
			}
			return;
		}
		limitLogged_ = false;
		const fs::path folder = fs::path(settings) / desync::kSpoolFolder;
		std::vector<fs::path> files;
		std::map<std::wstring, std::pair<std::uintmax_t, fs::file_time_type>> stamps;
		std::error_code ec;
		const std::wstring extension = desync::kSpoolExtension;
		for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			const std::wstring name = it->path().filename().wstring();
			if (name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
				files.push_back(it->path());
				std::error_code stat;
				const std::uintmax_t size = it->file_size(stat);
				const fs::file_time_type time = stat ? fs::file_time_type{} : it->last_write_time(stat);
				if (!stat) stamps[name] = {size, time};
			}
		}
		// Headers kept for the files that are still there.
		for (auto it = headers_.begin(); it != headers_.end();) it = stamps.count(it->first) ? std::next(it) : headers_.erase(it);
		if (files.empty()) return;
		// Named by the time they were written: oldest first.
		std::sort(files.begin(), files.end());

		// Which are sent, in what order, and which are dropped unsent
		// (desync::PlanSends): the installed build's reports, newest first.
		RefreshInstalledBuild();
		// `paths[i]` is `waiting[i]`'s file.
		std::vector<desync::Waiting> waiting;
		std::vector<fs::path> paths;
		std::vector<fs::path> unreadable;
		for (const fs::path& path : files) {
			const std::wstring name = path.filename().wstring();
			const auto stamp = stamps.find(name);
			const auto kept = headers_.find(name);
			if (stamp != stamps.end() && kept != headers_.end() && kept->second.size == stamp->second.first &&
				kept->second.time == stamp->second.second) {
				waiting.push_back(kept->second.waiting); // read before, and not touched since
				paths.push_back(path);
				continue;
			}
			const std::optional<desync::Spooled> spooled = Peek(path);
			if (!spooled) {
				unreadable.push_back(path); // Deliver deletes what is not a report, and skips what another launcher holds
				continue;
			}
			waiting.push_back({spooled->time, spooled->buildId});
			paths.push_back(path);
			if (stamp != stamps.end()) headers_[name] = {stamp->second.first, stamp->second.second, waiting.back()};
		}
		const desync::SendPlan plan = desync::PlanSends(waiting, installedBuild_, Now());
		for (const std::size_t i : plan.otherBuild) {
			spdlog::info("Desync report {}: from another build than the installed one; deleted unsent",
				sf4e::platform::WideToUtf8(paths[i].filename().wstring()));
			Discard(paths[i]);
		}
		for (const std::size_t i : plan.stale) {
			spdlog::info("Desync report {}: older than a week; deleted unsent", sf4e::platform::WideToUtf8(paths[i].filename().wstring()));
			Discard(paths[i]);
		}
		for (const fs::path& path : unreadable) {
			if (cancelled_) return;
			Deliver(path, settings);
		}
		for (const std::size_t i : plan.send) {
			if (cancelled_) return;
			const fs::path& path = paths[i];
			int& tries = tries_[path.filename().wstring()];
			if (tries >= kTriesPerRun) continue;
			if (Now() < pauseUntil_) return;
			// The caps are checked before every send: the day's (a flood guard
			// over a session) and the intake's own pace (also before the pass).
			const std::int64_t waitMore = Wait(settings);
			if (waitMore > 0) {
				if (!limitLogged_) {
					spdlog::info("Desync report {}: the sending limit is reached; it waits {} s",
						sf4e::platform::WideToUtf8(path.filename().wstring()), waitMore);
				}
				limitLogged_ = true;
				return; // every other waiting report hits the same limit
			}
			limitLogged_ = false;
			if (!Deliver(path, settings)) tries++;
		}
	}

	// Hashes the installed module again when it is not the one installedBuild_
	// came from (or that could not be read).
	void RefreshInstalledBuild()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::optional<ModuleStamp> stamp;
		if (length && length < MAX_PATH) stamp = StampOf(fs::path(path).parent_path() / ember::Product().gameModule);
		if (!installedBuild_.empty() && stamp && installedStamp_ == stamp) return;
		installedBuild_ = InstalledBuildId();
		installedStamp_ = installedBuild_.empty() ? std::nullopt : stamp;
	}

	// A report's parsed file without holding it; nothing when it is gone,
	// another launcher has it, or it is not one.
	static std::optional<desync::Spooled> Peek(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) return std::nullopt;
		const std::streamoff size = file.tellg();
		if (size <= 0 || static_cast<std::uintmax_t>(size) > kSpoolFileBytes) return std::nullopt;
		std::string text(static_cast<std::size_t>(size), '\0');
		file.seekg(0);
		if (!file.read(text.data(), size)) return std::nullopt;
		return desync::ParseSpool(text);
	}

	static void Discard(const fs::path& path)
	{
		const HeldFile file(path);
		if (file.Held()) file.Delete();
	}

	// Seconds until one more send is allowed; 0 now. A record that cannot be
	// read is a wait too: nothing is counted then, so nothing is sent.
	static std::int64_t Wait(const std::wstring& settings)
	{
		const reports::ReportHistory history(DesyncRecordPath(settings));
		const reports::History current = history.Load();
		if (!current.readable) return kPauseSeconds;
		std::vector<std::int64_t> times;
		for (const reports::Record& r : current.sent) {
			// A send that never reached the intake (no connection, cancelled)
			// is not one of the day's.
			if (r.kind == "problem" && r.status != reports::Status::NotSent) times.push_back(r.time);
		}
		return desync::SendWaitSeconds(times, Now());
	}

	// The desync reports' own record of sent reports, apart from the crash
	// reports' (sent-reports.json): the crash reports' automatic limit of three
	// a day neither holds desync reports back nor is used up by them.
	static fs::path DesyncRecordPath(const std::wstring& settings) { return fs::path(settings) / desync::kSentFile; }

	// One report; false when it stays for a later try.
	bool Deliver(const fs::path& path, const std::wstring& settings)
	{
		const std::string name = sf4e::platform::WideToUtf8(path.filename().wstring());
		const HeldFile file(path);
		// Another launcher has it, or the game is still naming it.
		if (!file.Held()) return true;
		const std::optional<std::string> text = file.Read(kSpoolFileBytes);
		const std::optional<desync::Spooled> spooled = text ? desync::ParseSpool(*text) : std::nullopt;
		if (!spooled) {
			spdlog::warn("Desync report {}: not a report this launcher reads; deleted", name);
			file.Delete();
			return true;
		}
		const std::int64_t now = Now();
		if (now - spooled->time > desync::kSpoolMaxAgeSeconds) {
			spdlog::info("Desync report {}: older than a week; deleted unsent", name);
			file.Delete();
			return true;
		}
		const std::optional<bool> on = reports::ReportsOn(settings);
		if (!on) {
			spdlog::warn("Desync report {}: the settings cannot be read; it waits", name);
			return false;
		}
		if (!*on) {
			spdlog::info("Desync report {}: Send problem reports is off; deleted unsent", name);
			file.Delete();
			return true;
		}
		const std::function<bool()> cancelled = [this] { return cancelled_.load(); };
		const reports::Identity identity = reports::CollectIdentity(settings, cancelled);
		if (cancelled_) return false;
		const std::string tail = identity.ok ? Redacted(spooled->logTail, identity.account) :
			"(left out: the names to take out of it could not be read)\n";

		reports::Report report;
		report.meta.kind = "problem";
		report.meta.appVersion = spooled->appVersion;
		report.meta.channel = Channel();
		report.meta.buildId = spooled->buildId;
		report.meta.sourceRevision = spooled->sourceRevision;
		report.meta.windowsVersion = WindowsVersion();
		report.logs.push_back({Narrow(ember::Product().gameLog), desync::Record(spooled->comment, spooled->regionLines, tail, spooled->details)});
		// The state beside the report (a copy's 1 MB of regions and its pointer
		// census, deflated), unless the intake has refused one already in this
		// launcher's life. It goes only past the same switch as the report.
		std::string state;
		if (stateAccepted_) {
			const std::string raw = ReadState(fs::path(settings), path);
			if (!raw.empty()) state = Deflate(raw);
		}

		// Counted in the desync reports' own record, with no limit of the
		// library's (Pass checked the caps).
		reports::ReportHistory history(DesyncRecordPath(settings));
		reports::Record attempt;
		attempt.kind = "problem";
		const reports::Reservation reservation = history.Reserve(attempt, false, now);
		if (reservation != reports::Reservation::Reserved) {
			spdlog::info("Desync report {}: the record of sent reports cannot be changed; it waits", name);
			return false;
		}
		bool withLog = true;
		desync::Outcome outcome = Send(report, spooled->comment, state, cancelled);
		// An intake from before the state part, or before the log's name, is
		// answered once more without what it does not take; each time the same
		// counted send.
		for (int again = 0; again < 2; ++again) {
			if (outcome.verdict == desync::Verdict::WithoutState && !state.empty()) {
				// Only an intake that does not know the part turns it off; a
				// 413 is this report's state being too large for it.
				if (outcome.reason == "unknown_part") {
					spdlog::info("Desync report {}: the intake does not take the state yet; sent again without it", name);
					stateAccepted_ = false;
				} else {
					spdlog::info("Desync report {}: the intake refused its state ({}); sent again without it", name, outcome.reason);
				}
				state.clear();
			} else if (outcome.verdict == desync::Verdict::WithoutLog && withLog) {
				spdlog::info("Desync report {}: the intake does not take {} yet; sent again without it", name, report.logs[0].name);
				report.logs.clear();
				withLog = false;
			} else {
				break;
			}
			outcome = Send(report, spooled->comment, state, cancelled);
		}
		reports::Record record = attempt;
		record.id = outcome.id;
		record.reason = outcome.reason;
		record.status = outcome.verdict == desync::Verdict::Delivered ? reports::Status::Received :
			outcome.reason == "timeout" || outcome.reason == "no_connection" || outcome.reason == "cancelled" ||
					outcome.reason == "invalid" ? reports::Status::NotSent : reports::Status::Refused;
		if (!history.Complete(record)) spdlog::warn("Desync report {}: the record of sent reports was not updated", name);
		switch (outcome.verdict) {
		case desync::Verdict::Delivered:
			spdlog::info("Desync report {}: received as {}{}{}{}", name, outcome.id.empty() ? "(no id)" : outcome.id.c_str(),
				withLog ? "" : ", without its log", state.empty() ? ", without its state" : ", with its state",
				spooled->watcher ? ", a watcher's" : "");
			file.Delete();
			return true;
		case desync::Verdict::Refused:
		case desync::Verdict::WithoutLog:
		case desync::Verdict::WithoutState:
			spdlog::warn("Desync report {}: refused ({}); deleted", name, outcome.reason);
			file.Delete();
			return true;
		case desync::Verdict::Later:
			break;
		}
		// No answer, a rate limit or a busy intake: nothing is sent for a while.
		pauseUntil_ = Now() + kPauseSeconds;
		spdlog::info("Desync report {}: not sent ({}); it waits", name, outcome.reason);
		return false;
	}

	// One upload; Verdict::Refused with reason "invalid" when the report
	// cannot be built at all. `state`: the deflated state, or empty for none.
	desync::Outcome Send(const reports::Report& report, const std::string& comment, const std::string& state,
		const std::function<bool()>& cancelled)
	{
		reports::Submission submission;
		submission.comment = comment;
		submission.includeDump = false;
		reports::Multipart multipart;
		std::string error;
		bool built = false;
		std::string boundary;
		// A boundary that happens to occur in the report is drawn again.
		for (int i = 0; i < 3 && !built; ++i) {
			boundary = Boundary();
			built = reports::BuildMultipart(report, submission, boundary, multipart, error);
		}
		if (!built) return {desync::Verdict::Refused, {}, "invalid"};
		if (!state.empty()) {
			const std::string withState = desync::WithPart(multipart.body, boundary, "state", "desync.state.z", state, kBodyLimit);
			// A body that cannot take the state goes without it, as a report without one.
			if (!withState.empty()) multipart.body = withState;
		}
		const sf4e::HttpResponse answer =
			sf4e::HttpPostReport(sf4e::ReportIntakeHost, multipart.contentType, multipart.body, cancelled);
		return desync::Judge(answer, !report.logs.empty(), !state.empty());
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	bool pending_ = false;
	std::atomic<bool> cancelled_{false};
	// The courier's thread only from here.
	std::map<std::wstring, int> tries_;
	// A waiting report's parsed header by file name, valid while its size and
	// time are as they were.
	struct Header {
		std::uintmax_t size = 0;
		fs::file_time_type time{};
		desync::Waiting waiting;
	};
	std::map<std::wstring, Header> headers_;
	std::string installedBuild_; // InstalledBuildId, hashed again when the module's size or time changes
	std::optional<ModuleStamp> installedStamp_; // the module as installedBuild_ was hashed
	std::int64_t pauseUntil_ = 0;
	bool limitLogged_ = false;
	bool stateAccepted_ = true; // false once the intake refused a state part
	// Last: started after everything above, stopped (joined) before it goes.
	std::thread thread_;
};

// Made at the first start, after the launcher's log; it goes before the
// log does as the launcher exits, cancelling and joining its thread.
Courier& TheCourier()
{
	static Courier courier;
	return courier;
}

} // namespace

void StartDesyncCourier()
{
	TheCourier().Start();
}

void WakeDesyncCourier()
{
	TheCourier().Wake();
}

} // namespace sfve::launcher
