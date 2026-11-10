#include "launcher/DesyncCourier.hxx"

#include "ember/DesyncReport.hxx"

#include "common/ProductIdentity.hxx"
#include "common/sf4e__NetUtil.hxx"
#include "launcher/update/github_release_client.hxx"
#include "netplay/SettingsStore.hxx"
#include "platform/ProblemReport.hxx"
#include "platform/ReportHistory.hxx"
#include "platform/ReportWorkflow.hxx"
#include "platform/UiPreferencesStore.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
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

constexpr std::chrono::seconds kPassInterval{30};
// Tries of one report in one launcher's life that came to nothing.
constexpr int kTriesPerRun = 2;
// Larger than any report the game writes (its log's tail is at most 256 KiB).
constexpr std::uintmax_t kSpoolFileBytes = 1024 * 1024;

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
		const fs::path folder = fs::path(settings) / desync::kSpoolFolder;
		std::vector<fs::path> waiting;
		std::error_code ec;
		const std::wstring extension = desync::kSpoolExtension;
		for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			const std::wstring name = it->path().filename().wstring();
			if (name.size() > extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0) {
				waiting.push_back(it->path());
			}
		}
		// Named by the time they were written: oldest first.
		std::sort(waiting.begin(), waiting.end());
		for (const fs::path& path : waiting) {
			if (cancelled_) return;
			int& tries = tries_[path.filename().wstring()];
			if (tries >= kTriesPerRun) continue;
			if (!Deliver(path, settings)) tries++;
		}
	}

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
		report.logs.push_back({Narrow(ember::Product().gameLog), desync::Record(spooled->comment, spooled->regionLines, tail)});

		reports::ReportHistory history(reports::SentReportsPath());
		reports::Record attempt;
		attempt.kind = "problem";
		const reports::Reservation reservation = history.Reserve(attempt, true, now);
		if (reservation != reports::Reservation::Reserved) {
			spdlog::info("Desync report {}: {}; it waits", name,
				reservation == reports::Reservation::LimitReached ? "today's automatic reports are used up" :
					"the record of sent reports cannot be changed");
			return false;
		}
		bool withLog = true;
		desync::Outcome outcome = Send(report, spooled->comment, cancelled);
		if (outcome.verdict == desync::Verdict::WithoutLog) {
			spdlog::info("Desync report {}: the intake does not take {} yet; sent again without it", name, report.logs[0].name);
			report.logs.clear();
			withLog = false;
			outcome = Send(report, spooled->comment, cancelled);
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
			spdlog::info("Desync report {}: received as {}{}{}", name, outcome.id.empty() ? "(no id)" : outcome.id.c_str(),
				withLog ? "" : ", without its log", spooled->watcher ? ", a watcher's" : "");
			file.Delete();
			return true;
		case desync::Verdict::Refused:
		case desync::Verdict::WithoutLog:
			spdlog::warn("Desync report {}: refused ({}); deleted", name, outcome.reason);
			file.Delete();
			return true;
		case desync::Verdict::Later:
			break;
		}
		spdlog::info("Desync report {}: not sent ({}); it waits", name, outcome.reason);
		return false;
	}

	// One upload; Verdict::Refused with reason "invalid" when the report
	// cannot be built at all.
	desync::Outcome Send(const reports::Report& report, const std::string& comment, const std::function<bool()>& cancelled)
	{
		reports::Submission submission;
		submission.comment = comment;
		submission.includeDump = false;
		reports::Multipart multipart;
		std::string error;
		bool built = false;
		// A boundary that happens to occur in the report is drawn again.
		for (int i = 0; i < 3 && !built; ++i) built = reports::BuildMultipart(report, submission, Boundary(), multipart, error);
		if (!built) return {desync::Verdict::Refused, {}, "invalid"};
		const sf4e::HttpResponse answer =
			sf4e::HttpPostReport(sf4e::ReportIntakeHost, multipart.contentType, multipart.body, cancelled);
		return desync::Judge(answer, !report.logs.empty());
	}

	std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	bool pending_ = false;
	std::atomic<bool> cancelled_{false};
	std::map<std::wstring, int> tries_; // the courier's thread only
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
