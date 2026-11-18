#pragma once

// The colour pack (docs/design/COLOUR_PACK.md): every colour of every costume
// but the first, which the package does not carry. The package names it in a
// small descriptor; the launcher downloads it once into the player's data
// folder (launcher/ColourPackFetch.hxx) and the game reads its pictures before
// the package's own (PictureSources, SfvSelection.cxx). Here, so that both
// programs read the descriptor and find the pack's folder alike. No game
// code, no network.

#include "ember/SfvPickSpace.hxx"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::colours {

// The descriptor at the package folder's top, written by scripts/package.ps1.
inline constexpr wchar_t kDescriptorFile[] = L"colour-pack.json";

// A pack: one archive, named by its content.
struct Pack {
	std::string id;          // the SHA-256's first 16 digits: the archive's name and its folder's
	std::uint64_t bytes = 0; // the archive's size
	std::string sha256;      // the archive's, 64 lowercase hex digits
	std::string url;         // where it is downloaded from (ParsePack)
	bool operator==(const Pack&) const = default;
};

// A descriptor's text as a pack: format 1; the SHA-256 64 lowercase hex
// digits and the id its first 16; a size from one byte to 1 GiB; the URL
// https://github.com/<repository>/releases/download/<tag>/sfve-colours-<id>.zip,
// <repository> being the product's own (owner/name). Other fields are passed
// over. Nothing, with why in `error`, for anything else.
std::optional<Pack> ParsePack(std::string_view text, std::string_view repository, std::string& error);

// The pack the package folder's descriptor names, over the product's release
// repository (ProductIdentity::stableRepo). Nothing with `error` empty for a
// package without a descriptor (a development package, or one whose renders
// left no colour over); nothing with why for one that cannot be read.
std::optional<Pack> PackagePack(const std::filesystem::path& package, std::string& error);

// Where packs live in the player's data folder (%APPDATA%\SFVEmber), never
// the install folder: <data>\colour-packs, and in it each pack's own folder,
// named by its id, which holds the archive's files (manifest.json and
// assets\selection\...). The launcher renames a pack's folder into place
// complete, so one that is there is whole.
std::filesystem::path PacksFolder(const std::filesystem::path& data);
std::filesystem::path PackFolder(const std::filesystem::path& data, const Pack& pack);

// Where the selection pictures are read from, in order: folders that each
// hold the package's layout, so a picture is its one path
// (SfvSelectionAssetPath's builders) under each. A file that is not there is
// skipped by the decoder, which logs nothing for it.
class PictureSources {
public:
	explicit PictureSources(std::vector<std::filesystem::path> roots);
	// A look's cutout and a loadout picture under every root, first first;
	// empty for what has no path (an empty code, a negative number, a choice
	// outside I and II).
	std::vector<std::wstring> Cutout(std::string_view fighterCode, int costume, int colour) const;
	std::vector<std::wstring> Loadout(std::string_view fighterCode, picks::LoadoutPart part, int choice) const;

private:
	std::vector<std::wstring> Under(const std::wstring& relative) const;
	std::vector<std::filesystem::path> roots_;
};

// The game's sources: the pack the package names, in the player's data
// folder `data`, whether or not it has arrived (one that arrives during the
// session is read from then on), then the package folder. A descriptor that
// cannot be read leaves the package alone, and says why in `problem`.
PictureSources InstalledSources(const std::filesystem::path& package, const std::filesystem::path& data, std::string& problem);

} // namespace sfve::colours
