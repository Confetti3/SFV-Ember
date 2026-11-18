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

// What PruneOtherPacks did, by name in the packs folder.
struct Pruned {
	std::vector<std::wstring> removed;
	std::vector<std::wstring> left; // could not be removed (another program holds a file); tried again at the next start
};
// Removes everything in the packs folder but the pack's own folder: older
// packs, and an interrupted fetch's download and extraction.
Pruned PruneOtherPacks(const std::filesystem::path& data, const colours::Pack& pack);

// The download (PackDownload), checked (its size, then its SHA-256) and
// extracted by the updater's zip reader, which refuses the whole archive for
// one unsafe entry name, into <id>.incoming, which is then renamed to the
// pack's folder in one step. The download is removed whatever happens, and a
// partial extraction too. False, with why in `error`, when anything failed or
// `cancel` was set.
bool InstallPack(const std::filesystem::path& data, const colours::Pack& pack, std::string& error, const std::atomic<bool>& cancel);

} // namespace sfve::launcher
