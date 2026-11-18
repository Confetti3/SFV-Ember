#include "launcher/ColourPackStore.hxx"

#include "launcher/update/PackageInstaller.hxx"
#include "launcher/update/ZipExtract.hxx"

#include <exception>
#include <system_error>
#include <utility>

namespace sfve::launcher {

namespace fs = std::filesystem;

fs::path PackDownload(const fs::path& data, const colours::Pack& pack)
{
	return colours::PacksFolder(data) / (pack.id + ".download");
}

Pruned PruneOtherPacks(const fs::path& data, const colours::Pack& pack)
{
	const fs::path keep(pack.id);
	std::vector<fs::path> others;
	std::error_code ec;
	for (fs::directory_iterator entry(colours::PacksFolder(data), ec), end; !ec && entry != end; entry.increment(ec)) {
		if (entry->path().filename() != keep) others.push_back(entry->path());
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
	const auto fail = [&](std::string why) {
		std::error_code ignored;
		fs::remove_all(incoming, ignored);
		fs::remove(download, ignored);
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
	fs::remove_all(incoming, ec);
	std::string why;
	if (!sf4e::launcher::ExtractZip(download.wstring(), incoming.wstring(), why, cancel)) return fail("could not extract the download: " + why);
	fs::rename(incoming, target, ec);
	// A second launcher that put the same pack in place first leaves this one's copy over.
	if (ec && !fs::is_directory(target)) return fail("could not move the pack into place: " + ec.message());
	std::error_code ignored;
	fs::remove_all(incoming, ignored);
	fs::remove(download, ignored);
	error.clear();
	return true;
}

} // namespace sfve::launcher
