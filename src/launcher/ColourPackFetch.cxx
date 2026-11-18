#include "launcher/ColourPackFetch.hxx"

#include "launcher/ColourPackStore.hxx"

#include "ember/SfvColourPack.hxx"

#include "common/sf4e__NetUtil.hxx"
#include "netplay/SettingsStore.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace sfve::launcher {

namespace {

namespace fs = std::filesystem;

// Each step of a transfer waits this long at most, as the updater's download does.
constexpr int kStepTimeoutMs = 20000;
constexpr char kHeaders[] = "User-Agent: sfv-ember-launcher\r\n";
// A log line at each quarter of the download.
constexpr std::uint64_t kProgressLines = 4;

// The package folder: Launcher.exe's.
fs::path LauncherFolder()
{
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	return length && length < MAX_PATH ? fs::path(path).parent_path() : fs::path();
}

std::string Megabytes(std::uint64_t bytes)
{
	char text[32] = {};
	snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
	return text;
}

class Fetch {
public:
	Fetch() = default;
	Fetch(const Fetch&) = delete;
	Fetch& operator=(const Fetch&) = delete;
	~Fetch()
	{
		cancel_ = true;
		if (thread_.joinable()) thread_.join();
	}

	void Start()
	{
		if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
	}

private:
	void Run()
	{
		std::string problem;
		const std::optional<colours::Pack> pack = colours::PackagePack(LauncherFolder(), problem);
		if (!pack) {
			if (problem.empty()) spdlog::info("Colour pack: this package names none");
			else spdlog::warn("Colour pack: {}; the game shows each costume's first colour", problem);
			return;
		}
		const fs::path data(sf4e::netplay::SettingsStore::DefaultDirectory());
		if (data.empty()) {
			spdlog::warn("Colour pack {}: no data folder to keep it in; the game shows each costume's first colour", pack->id);
			return;
		}
		const Pruned pruned = PruneOtherPacks(data, *pack);
		for (const std::wstring& name : pruned.removed) spdlog::info("Colour pack: removed {}", sf4e::platform::WideToUtf8(name));
		for (const std::wstring& name : pruned.left) {
			spdlog::warn("Colour pack: could not remove {}; tried again at the next start", sf4e::platform::WideToUtf8(name));
		}
		if (fs::is_directory(colours::PackFolder(data, *pack))) {
			spdlog::info("Colour pack {}: here", pack->id);
			return;
		}
		const auto started = std::chrono::steady_clock::now();
		if (!Download(data, *pack)) return;
		std::string error;
		if (!InstallPack(data, *pack, error, cancel_)) {
			spdlog::warn("Colour pack {}: not installed ({}); tried again at the next start", pack->id, error);
			return;
		}
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
		spdlog::info("Colour pack {}: installed in {} s; every colour the game shows from now on is the pack's", pack->id, seconds);
	}

	// The archive to PackDownload; false, with a log line and nothing left
	// behind, when it did not come whole.
	bool Download(const fs::path& data, const colours::Pack& pack)
	{
		std::error_code ec;
		fs::create_directories(colours::PacksFolder(data), ec);
		const fs::path file = PackDownload(data, pack);
		spdlog::info("Colour pack {}: downloading {} from {}", pack.id, Megabytes(pack.bytes), pack.url);
		std::uint64_t lines = 0;
		bool oversized = false;
		const auto received = [&](std::uint64_t done, std::uint64_t) {
			// More than the package names is not its pack.
			if (done > pack.bytes) {
				oversized = true;
				return false;
			}
			const std::uint64_t line = done * kProgressLines / pack.bytes;
			if (line > lines && line < kProgressLines) {
				lines = line;
				spdlog::info("Colour pack {}: {}% downloaded", pack.id, line * 100 / kProgressLines);
			}
			return !cancel_.load();
		};
		sf4e::HttpRequestResult result;
		if (sf4e::HttpDownloadUrlUtf8(pack.url.c_str(), file.c_str(), kStepTimeoutMs, kHeaders, &result, received)) return true;
		fs::remove(file, ec);
		const std::string why = cancel_ ? std::string("the launcher closed") :
			oversized ? "more than " + std::to_string(pack.bytes) + " bytes came" :
			result.error == sf4e::HttpErrorKind::HttpStatus ? "HTTP " + std::to_string(result.statusCode) :
			"network error " + std::to_string(static_cast<int>(result.error)) + ", Win32 " + std::to_string(result.win32Error);
		spdlog::warn("Colour pack {}: not downloaded ({}); tried again at the next start", pack.id, why);
		return false;
	}

	std::atomic<bool> cancel_{false};
	// Last: started after everything above, joined before it goes.
	std::thread thread_;
};

// Made at the first start, after the launcher's log; it goes before the log
// does as the launcher exits, cancelling and joining its thread.
Fetch& TheFetch()
{
	static Fetch fetch;
	return fetch;
}

} // namespace

void StartColourPackFetch()
{
	TheFetch().Start();
}

} // namespace sfve::launcher
