#pragma once

// What the launcher's colour pack fetch (ColourPackFetch.hxx) does on disk,
// apart from the network so that the tests run it on files: the packs folder
// (colours::PacksFolder) keeps only the pack the package names, and a
// downloaded archive becomes that pack's folder only checked and whole.

#include "ember/SfvColourPack.hxx"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace sfve::launcher {

// Where the archive is downloaded to: <id>.download in the packs folder.
std::filesystem::path PackDownload(const std::filesystem::path& data, const colours::Pack& pack);

// One fetch at a time works in the packs folder (pruning, downloading,
// installing): this holds fetch.lock there, open with no sharing, for as long
// as it lives. Held() is false when another launcher's fetch holds it (or
// the folder cannot be made); that fetch then leaves everything to it. A
// launcher that ends, however it ends, lets go of it.
class PacksLock {
public:
	explicit PacksLock(const std::filesystem::path& data);
	~PacksLock();
	PacksLock(const PacksLock&) = delete;
	PacksLock& operator=(const PacksLock&) = delete;
	bool Held() const { return file_ != nullptr; }

private:
	void* file_ = nullptr;
};

// What PruneOtherPacks did, by name in the packs folder.
struct Pruned {
	std::vector<std::wstring> removed;
	std::vector<std::wstring> left; // could not be removed (another program holds a file); tried again at the next start
};
// Removes everything in the packs folder but the pack's own folder, its
// download (InstallPack checks it again before anything else) and the lock:
// older packs and their downloads, and an interrupted extraction.
Pruned PruneOtherPacks(const std::filesystem::path& data, const colours::Pack& pack);

// The download (PackDownload), checked (its size, then its SHA-256) and
// extracted by the updater's zip reader, which refuses the whole archive for
// one unsafe entry name, into <id>.incoming, which is then renamed to the
// pack's folder in one step, tried a few times over about two seconds while
// another program (the search indexer, a virus scanner) has a new file in it
// open. A partial extraction is removed whatever happens. The download goes
// once the pack is in place, or when it is not the pack (size, SHA-256); one
// that matched is kept when a later step failed, for the next start to
// install without downloading it again. False, with why in `error`, when
// anything failed or `cancel` was set.
bool InstallPack(const std::filesystem::path& data, const colours::Pack& pack, std::string& error, const std::atomic<bool>& cancel);

} // namespace sfve::launcher
