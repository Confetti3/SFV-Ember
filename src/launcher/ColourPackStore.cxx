#include "launcher/ColourPackStore.hxx"

#include "launcher/update/PackageInstaller.hxx"
#include "launcher/update/ZipExtract.hxx"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <iterator>
#include <system_error>
#include <thread>
#include <utility>

namespace sfve::launcher {

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kLockFile[] = L"fetch.lock";
// The final rename's tries, and the wait before each after the first: about two seconds in all.
constexpr int kRenameTries = 8;
constexpr std::chrono::milliseconds kRenameWait{250};

} // namespace

fs::path PackDownload(const fs::path& data, const colours::Pack& pack)
{
	return colours::PacksFolder(data) / (pack.id + ".download");
}

PacksLock::PacksLock(const fs::path& data)
{
	std::error_code ec;
	fs::create_directories(colours::PacksFolder(data), ec);
	const HANDLE file = CreateFileW((colours::PacksFolder(data) / kLockFile).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) file_ = file;
}

PacksLock::~PacksLock()
{
	if (file_) CloseHandle(file_);
}

Pruned PruneOtherPacks(const fs::path& data, const colours::Pack& pack)
{
	const fs::path keep[] = {fs::path(pack.id), PackDownload(data, pack).filename(), fs::path(kLockFile)};
	std::vector<fs::path> others;
	std::error_code ec;
	for (fs::directory_iterator entry(colours::PacksFolder(data), ec), end; !ec && entry != end; entry.increment(ec)) {
		if (std::find(std::begin(keep), std::end(keep), entry->path().filename()) == std::end(keep)) others.push_back(entry->path());
	}
	Pruned pruned;
	for (const fs::path& other : others) {
		std::error_code removed;
		fs::remove_all(other, removed);
		(removed ? pruned.left : pruned.removed).push_back(other.filename().wstring());
	}
	return pruned;
}

bool InstallPack(const fs::path& data, const colours::Pack& pack, std::string& error, const std::atomic<bool>& cancel)
{
	const fs::path download = PackDownload(data, pack);
	const fs::path incoming = colours::PacksFolder(data) / (pack.id + ".incoming");
	const fs::path target = colours::PackFolder(data, pack);
	// Until its SHA-256 matched the download is not the pack and goes with a
	// failure; after, it stays for the next start.
	bool matched = false;
	const auto fail = [&](std::string why) {
		std::error_code ignored;
		fs::remove_all(incoming, ignored);
		if (!matched) fs::remove(download, ignored);
		error = std::move(why);
		return false;
	};
	std::error_code ec;
	const std::uintmax_t size = fs::file_size(download, ec);
	if (ec) return fail("the download is not there");
	if (size != pack.bytes) return fail("the download is " + std::to_string(size) + " bytes, the package names " + std::to_string(pack.bytes));
	std::string sha256;
	try {
		sha256 = sf4e::launcher::Sha256Hex(download, cancel);
	} catch (const std::exception& failure) {
		return fail(cancel ? std::string("cancelled") : std::string("could not hash the download: ") + failure.what());
	}
	if (sha256 != pack.sha256) return fail("the download's SHA-256 is " + sha256 + ", the package names " + pack.sha256);
	matched = true;
	fs::remove_all(incoming, ec);
	std::string why;
	if (!sf4e::launcher::ExtractZip(download.wstring(), incoming.wstring(), why, cancel)) return fail("could not extract the download: " + why);
	for (int tried = 0; tried < kRenameTries; ++tried) {
		if (tried) std::this_thread::sleep_for(kRenameWait);
		if (cancel) return fail("cancelled");
		fs::rename(incoming, target, ec);
		if (!ec) break;
	}
	if (ec) return fail("could not move the pack into place: " + ec.message());
	std::error_code ignored;
	fs::remove(download, ignored);
	error.clear();
	return true;
}

} // namespace sfve::launcher
