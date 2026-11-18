#include "ember/SfvColourPack.hxx"

#include "ember/SfvSelectionAssetPath.hxx"

#include "common/ProductIdentity.hxx"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace sfve::colours {

namespace {

// Far above the 2026-11-17 pack (81 MB); a descriptor that names more is not one of ours.
constexpr std::uint64_t kLargestPack = std::uint64_t{1} << 30;
// A descriptor is a few hundred bytes.
constexpr std::uintmax_t kLargestDescriptor = 64 * 1024;

bool LowerHex(std::string_view text, std::size_t length)
{
	return text.size() == length && text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

// A release tag as one segment of a download URL.
bool TagSegment(std::string_view text)
{
	return !text.empty() && text != "." && text != ".." &&
		text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == std::string_view::npos;
}

std::string Text(const nlohmann::json& object, const char* key)
{
	const auto field = object.find(key);
	return field != object.end() && field->is_string() ? field->get<std::string>() : std::string();
}

} // namespace

std::optional<Pack> ParsePack(std::string_view text, std::string_view repository, std::string& error)
{
	const auto fail = [&](std::string why) -> std::optional<Pack> {
		error = std::move(why);
		return std::nullopt;
	};
	const nlohmann::json json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
	if (json.is_discarded() || !json.is_object()) return fail("not a JSON object");
	const auto format = json.find("format");
	if (format == json.end() || !format->is_number_unsigned() || format->get<std::uint64_t>() != 1) return fail("not format 1");
	Pack pack;
	pack.sha256 = Text(json, "sha256");
	if (!LowerHex(pack.sha256, 64)) return fail("no SHA-256 (64 lowercase hex digits)");
	pack.id = Text(json, "id");
	if (pack.id != pack.sha256.substr(0, 16)) return fail("the id is not the start of the SHA-256");
	const auto bytes = json.find("bytes");
	if (bytes == json.end() || !bytes->is_number_unsigned()) return fail("no size");
	pack.bytes = bytes->get<std::uint64_t>();
	if (pack.bytes == 0 || pack.bytes > kLargestPack) return fail("a size of " + std::to_string(pack.bytes) + " bytes");
	pack.url = Text(json, "url");
	const std::string prefix = "https://github.com/" + std::string(repository) + "/releases/download/";
	const std::string name = "/sfve-colours-" + pack.id + ".zip";
	const std::string_view url = pack.url;
	if (url.size() <= prefix.size() + name.size() || !url.starts_with(prefix) || !url.ends_with(name) ||
		!TagSegment(url.substr(prefix.size(), url.size() - prefix.size() - name.size()))) {
		return fail("not a release download of " + std::string(repository) + " of " + name.substr(1) + ": " + pack.url);
	}
	error.clear();
	return pack;
}

std::optional<Pack> PackagePack(const std::filesystem::path& package, std::string& error)
{
	error.clear();
	const std::filesystem::path file = package / kDescriptorFile;
	std::error_code ec;
	const std::filesystem::file_status state = std::filesystem::status(file, ec);
	if (state.type() == std::filesystem::file_type::not_found) return std::nullopt;
	const std::uintmax_t size = state.type() == std::filesystem::file_type::regular ? std::filesystem::file_size(file, ec) : 0;
	std::ifstream in(file, std::ios::binary);
	if (ec || size == 0 || size > kLargestDescriptor || !in) {
		error = "colour-pack.json cannot be read";
		return std::nullopt;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::optional<Pack> pack = ParsePack(text, ember::Product().stableRepo, error);
	if (!pack) error = "colour-pack.json: " + error;
	return pack;
}

std::filesystem::path PacksFolder(const std::filesystem::path& data)
{
	return data / L"colour-packs";
}

std::filesystem::path PackFolder(const std::filesystem::path& data, const Pack& pack)
{
	return PacksFolder(data) / std::filesystem::path(pack.id);
}

PictureSources::PictureSources(std::vector<std::filesystem::path> roots) : roots_(std::move(roots)) {}

std::vector<std::wstring> PictureSources::Cutout(std::string_view fighterCode, int costume, int colour) const
{
	return Under(assets::CutoutPath(fighterCode, costume, colour));
}

std::vector<std::wstring> PictureSources::Loadout(std::string_view fighterCode, picks::LoadoutPart part, int choice) const
{
	return Under(assets::LoadoutPicturePath(fighterCode, part, choice));
}

std::vector<std::wstring> PictureSources::Under(const std::wstring& relative) const
{
	std::vector<std::wstring> files;
	if (relative.empty()) return files;
	files.reserve(roots_.size());
	for (const std::filesystem::path& root : roots_) files.push_back((root / relative).wstring());
	return files;
}

PictureSources InstalledSources(const std::filesystem::path& package, const std::filesystem::path& data, std::string& problem)
{
	std::vector<std::filesystem::path> roots;
	// Without a data folder (Windows gave no roaming folder) the launcher had
	// nowhere to put a pack either.
	if (const std::optional<Pack> pack = PackagePack(package, problem); pack && !data.empty()) roots.push_back(PackFolder(data, *pack));
	roots.push_back(package);
	return PictureSources(std::move(roots));
}

} // namespace sfve::colours
