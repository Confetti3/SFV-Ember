// The colour pack (docs/design/COLOUR_PACK.md), checked on files without a
// network: the descriptor (SfvColourPack), what the launcher does with a
// download (ColourPackStore: size and SHA-256 first, an unsafe entry refuses
// the whole archive, one rename into place, only the current pack kept), and
// the order the game reads pictures in (PictureSources, CharacterFiles: the
// pack's file, then the package's, and the costume's first colour when the
// pack lacks the look).

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvColourPack.hxx"
#include "ember/SfvSelection.hxx"
#include "launcher/ColourPackStore.hxx"

#include "common/ProductIdentity.hxx"
#include "launcher/update/PackageInstaller.hxx"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using sfve::colours::Pack;
using sfve::colours::PictureSources;

namespace {

fs::path FreshFolder(const char* name)
{
	const fs::path folder = fs::temp_directory_path() / (std::string("sfve-colours-") + name + "-" + std::to_string(GetCurrentProcessId()));
	fs::remove_all(folder);
	fs::create_directories(folder);
	return folder;
}

void Write(const fs::path& file, const std::string& bytes)
{
	fs::create_directories(file.parent_path());
	std::ofstream(file, std::ios::binary) << bytes;
}

std::string U16(std::uint64_t value) { return {char(value & 255), char((value >> 8) & 255)}; }
std::string U32(std::uint64_t value) { return U16(value & 0xffff) + U16((value >> 16) & 0xffff); }

// A zip of stored entries, as scripts/ColourPack.ps1 makes the pack.
std::string StoredZip(const std::vector<std::pair<std::string, std::string>>& entries)
{
	std::string bytes, directory;
	for (const auto& [name, data] : entries) {
		const std::uint64_t crc = crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
		const std::string common = U16(20) + U16(0) + U16(0) + U32(0) + U32(crc) + U32(data.size()) + U32(data.size()) +
			U16(name.size()) + U16(0);
		directory += U32(0x02014b50) + U16(20) + common + U16(0) + U16(0) + U16(0) + U32(0) + U32(bytes.size()) + name;
		bytes += U32(0x04034b50) + common + name + data;
	}
	const std::uint64_t count = entries.size();
	return bytes + directory + U32(0x06054b50) + U16(0) + U16(0) + U16(count) + U16(count) + U32(directory.size()) +
		U32(bytes.size()) + U16(0);
}

std::string Url(const std::string& id, const std::string& tag = "colour-packs")
{
	return "https://github.com/" + std::string(ember::Product().stableRepo) + "/releases/download/" + tag + "/sfve-colours-" + id + ".zip";
}

nlohmann::json Descriptor(const Pack& pack)
{
	return {{"format", 1}, {"id", pack.id}, {"bytes", pack.bytes}, {"sha256", pack.sha256}, {"url", pack.url}};
}

std::optional<Pack> Parse(const nlohmann::json& descriptor)
{
	std::string error;
	std::optional<Pack> pack = sfve::colours::ParsePack(descriptor.dump(), ember::Product().stableRepo, error);
	CHECK(pack.has_value() == error.empty());
	return pack;
}

// The pack of `zip`'s bytes, downloaded into `data`'s packs folder as the
// launcher would have it.
Pack Downloaded(const fs::path& data, const std::string& zip)
{
	const fs::path scratch = data / "scratch.zip";
	Write(scratch, zip);
	Pack pack;
	pack.sha256 = sf4e::launcher::Sha256Hex(scratch);
	pack.id = pack.sha256.substr(0, 16);
	pack.bytes = zip.size();
	pack.url = Url(pack.id);
	fs::create_directories(sfve::colours::PacksFolder(data));
	fs::rename(scratch, sfve::launcher::PackDownload(data, pack));
	return pack;
}

const std::atomic<bool> kGoOn{false};

} // namespace

TEST_CASE("a colour pack descriptor names one archive of the product's own releases")
{
	const std::string sha(64, 'a');
	Pack expected{sha.substr(0, 16), 81256692, sha, Url(sha.substr(0, 16))};
	const std::optional<Pack> pack = Parse(Descriptor(expected));
	REQUIRE(pack.has_value());
	CHECK(*pack == expected);
	// Fields it does not know are passed over; another release tag is fine.
	nlohmann::json more = Descriptor(expected);
	more["cutouts"] = 5506;
	CHECK(Parse(more).has_value());
	more["url"] = Url(expected.id, "v0.1.1");
	CHECK(Parse(more).has_value());

	const auto refused = [&](const char* key, nlohmann::json value) {
		nlohmann::json changed = Descriptor(expected);
		if (value.is_null()) changed.erase(key);
		else changed[key] = std::move(value);
		CHECK_MESSAGE(!Parse(changed).has_value(), std::string(key) + " = " + changed.dump());
	};
	refused("format", 2);
	refused("format", nullptr);
	refused("format", "1");
	refused("sha256", std::string(64, 'A'));
	refused("sha256", std::string(63, 'a'));
	refused("sha256", std::string(63, 'a') + "g");
	refused("id", std::string(16, 'b'));
	refused("id", sha.substr(0, 15));
	refused("bytes", 0);
	refused("bytes", -5);
	refused("bytes", "81256692");
	refused("bytes", (std::uint64_t{1} << 30) + 1);
	refused("bytes", nullptr);
	refused("url", nullptr);
	refused("url", "http://github.com/" + std::string(ember::Product().stableRepo) + "/releases/download/colour-packs/sfve-colours-" + expected.id + ".zip");
	refused("url", "https://github.com/Someone/Else/releases/download/colour-packs/sfve-colours-" + expected.id + ".zip");
	refused("url", "https://example.invalid/" + std::string(ember::Product().stableRepo) + "/releases/download/colour-packs/sfve-colours-" + expected.id + ".zip");
	refused("url", Url(std::string(16, 'b')));
	refused("url", Url(expected.id, ""));
	refused("url", Url(expected.id, "a/b"));
	refused("url", Url(expected.id, ".."));
	refused("url", Url(expected.id, "colour%2Fpacks"));
	refused("url", Url(expected.id) + "?x");
	std::string error;
	CHECK(!sfve::colours::ParsePack("not json", ember::Product().stableRepo, error));
	CHECK(!error.empty());
	CHECK(!sfve::colours::ParsePack("[1]", ember::Product().stableRepo, error));
}

TEST_CASE("the package names its colour pack in colour-pack.json, or none")
{
	const fs::path package = FreshFolder("package");
	std::string error;
	// A development package has none, and that is no problem.
	CHECK(!sfve::colours::PackagePack(package, error));
	CHECK(error.empty());
	const std::string sha(64, 'c');
	const Pack expected{sha.substr(0, 16), 1000, sha, Url(sha.substr(0, 16))};
	Write(package / sfve::colours::kDescriptorFile, Descriptor(expected).dump(2));
	const std::optional<Pack> pack = sfve::colours::PackagePack(package, error);
	REQUIRE(pack.has_value());
	CHECK(*pack == expected);
	CHECK(error.empty());
	Write(package / sfve::colours::kDescriptorFile, "{\"format\": 1}");
	CHECK(!sfve::colours::PackagePack(package, error));
	CHECK(error.find("colour-pack.json") == 0);
	// The pack lives in the player's data folder, by its id.
	CHECK(sfve::colours::PacksFolder(L"C:\\data") == fs::path(L"C:\\data\\colour-packs"));
	CHECK(sfve::colours::PackFolder(L"C:\\data", expected) == fs::path(L"C:\\data\\colour-packs\\cccccccccccccccc"));
	fs::remove_all(package);
}

TEST_CASE("a downloaded colour pack is checked, then extracted and moved into place whole")
{
	const fs::path data = FreshFolder("install");
	const std::string picture = "\x89PNG pixels";
	const std::string zip = StoredZip({{"manifest.json", "{}"}, {"assets/selection/RYU/costume-0/color-1-cutout.png", picture}});
	const Pack pack = Downloaded(data, zip);
	std::string error;
	REQUIRE(sfve::launcher::InstallPack(data, pack, error, kGoOn));
	CHECK(error.empty());
	const fs::path folder = sfve::colours::PackFolder(data, pack);
	std::ifstream in(folder / L"assets\\selection\\RYU\\costume-0\\color-1-cutout.png", std::ios::binary);
	CHECK(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) == picture);
	CHECK(fs::exists(folder / L"manifest.json"));
	// Nothing else is left in the packs folder.
	CHECK(!fs::exists(sfve::launcher::PackDownload(data, pack)));
	CHECK(std::distance(fs::directory_iterator(sfve::colours::PacksFolder(data)), fs::directory_iterator()) == 1);
	in.close();
	fs::remove_all(data);
}

TEST_CASE("a colour pack that is not the one the package names is refused, and leaves nothing")
{
	const fs::path data = FreshFolder("refused");
	const std::string zip = StoredZip({{"assets/selection/RYU/costume-0/color-1-cutout.png", "pixels"}});
	const auto refused = [&](Pack pack, const char* why) {
		std::string error;
		CHECK_FALSE(sfve::launcher::InstallPack(data, pack, error, kGoOn));
		CHECK_MESSAGE(error.find(why) != std::string::npos, error);
		CHECK(!fs::exists(sfve::colours::PackFolder(data, pack)));
		CHECK(!fs::exists(sfve::launcher::PackDownload(data, pack)));
		CHECK(fs::is_empty(sfve::colours::PacksFolder(data)));
	};
	// Its size first, before any hashing.
	Pack pack = Downloaded(data, zip);
	pack.bytes += 1;
	refused(pack, "bytes, the package names");
	// Then its SHA-256: the right size with other bytes.
	pack = Downloaded(data, zip);
	const Pack named = pack;
	fs::remove(sfve::launcher::PackDownload(data, pack));
	std::string other = zip;
	other[other.size() / 2] ^= 1;
	Write(sfve::launcher::PackDownload(data, named), other);
	refused(named, "SHA-256");
	// A download that never came.
	refused(named, "not there");
	// A cancel stops it.
	pack = Downloaded(data, zip);
	const std::atomic<bool> stop{true};
	std::string error;
	CHECK_FALSE(sfve::launcher::InstallPack(data, pack, error, stop));
	CHECK(!fs::exists(sfve::colours::PackFolder(data, pack)));
	fs::remove_all(data);
}

TEST_CASE("a colour pack with one entry outside its folder is refused whole")
{
	const fs::path data = FreshFolder("slip");
	for (const char* unsafe : {"../escape.png", "assets/../../escape.png", "/rooted.png", "C:/rooted.png", "assets/selection/a:b.png"}) {
		const Pack pack = Downloaded(data, StoredZip({{"assets/selection/RYU/costume-0/color-1-cutout.png", "fine"}, {unsafe, "escaped"}}));
		std::string error;
		CHECK_FALSE(sfve::launcher::InstallPack(data, pack, error, kGoOn));
		CHECK_MESSAGE(error.find("unsafe entry name") != std::string::npos, std::string(unsafe) + ": " + error);
		// Nothing of it stays: not the entry that came before, not the download.
		CHECK(!fs::exists(sfve::colours::PackFolder(data, pack)));
		CHECK(fs::is_empty(sfve::colours::PacksFolder(data)));
	}
	CHECK(!fs::exists(data / "escape.png"));
	CHECK(!fs::exists(data.parent_path() / "escape.png"));
	fs::remove_all(data);
}

TEST_CASE("the packs folder keeps only the pack the package names")
{
	const fs::path data = FreshFolder("prune");
	const std::string sha(64, 'd');
	const Pack pack{sha.substr(0, 16), 10, sha, Url(sha.substr(0, 16))};
	const fs::path packs = sfve::colours::PacksFolder(data);
	Write(sfve::colours::PackFolder(data, pack) / "manifest.json", "{}");
	Write(packs / "0123456789abcdef" / "manifest.json", "{}");               // an older pack
	Write(packs / (pack.id + ".download"), "half");                          // an interrupted download
	Write(packs / (pack.id + ".incoming") / "assets" / "x.png", "partial");  // and extraction
	const sfve::launcher::Pruned pruned = sfve::launcher::PruneOtherPacks(data, pack);
	CHECK(pruned.removed.size() == 3);
	CHECK(pruned.left.empty());
	CHECK(fs::exists(sfve::colours::PackFolder(data, pack) / "manifest.json"));
	CHECK(std::distance(fs::directory_iterator(packs), fs::directory_iterator()) == 1);
	// No packs folder yet: nothing to do.
	fs::remove_all(packs);
	CHECK(sfve::launcher::PruneOtherPacks(data, pack).removed.empty());
	fs::remove_all(data);
}

TEST_CASE("the game reads a look from the colour pack first, then the package, then the costume's first colour")
{
	sfve::catalog_fixture::FillTestCatalog();
	const fs::path root = FreshFolder("lookup");
	const fs::path pack = root / "pack", package = root / "package";
	const PictureSources sources({pack, package});
	// Every picture is its one path under each source, the pack first.
	CHECK(sources.Cutout("RYU", 4, 100) == std::vector<std::wstring>{
		(pack / L"assets\\selection\\RYU\\costume-4\\color-100-cutout.png").wstring(),
		(package / L"assets\\selection\\RYU\\costume-4\\color-100-cutout.png").wstring()});
	CHECK(sources.Loadout("RYU", sfve::picks::LoadoutPart::VTrigger, 1) == std::vector<std::wstring>{
		(pack / L"assets\\selection\\RYU\\vt2.png").wstring(), (package / L"assets\\selection\\RYU\\vt2.png").wstring()});
	CHECK(sources.Cutout("", 0, 0).empty());
	CHECK(sources.Loadout("RYU", sfve::picks::LoadoutPart::VSkill, 2).empty());

	// The decoder takes the first file that is there.
	const auto shown = [&](int fighter, int costume, int colour) {
		for (const std::wstring& file : sfve::adapter::CharacterFiles(sources, fighter, costume, colour)) {
			if (fs::exists(file)) return fs::path(file);
		}
		return fs::path();
	};
	// The package ships each costume's first colour (RYU's costume 4: 0, 1, 100, 101).
	const fs::path first = package / L"assets\\selection\\RYU\\costume-4\\color-0-cutout.png";
	Write(first, "first");
	// No pack yet: the look falls back to the costume's first colour, as 0.1.0 did.
	CHECK(shown(0, 4, 100) == first);
	// The pack arrives with the look: its picture is the look's.
	const fs::path inPack = pack / L"assets\\selection\\RYU\\costume-4\\color-100-cutout.png";
	Write(inPack, "ex");
	CHECK(shown(0, 4, 100) == inPack);
	// A look the pack lacks still falls back to the package's first colour.
	CHECK(shown(0, 4, 101) == first);
	CHECK(shown(0, 4, 0) == first);
	// A colour the costume does not have is not asked for.
	for (const std::wstring& file : sfve::adapter::CharacterFiles(sources, 0, 4, 7)) CHECK(file.find(L"color-7-") == std::wstring::npos);
	CHECK(sfve::adapter::CharacterFiles(sources, 5, 0, 0).empty()); // not a fighter of the catalog
	fs::remove_all(root);
}

TEST_CASE("the installed sources are the pack the package names, then the package")
{
	const fs::path root = FreshFolder("installed");
	const fs::path package = root / "package", data = root / "data";
	fs::create_directories(package);
	std::string problem;
	// No descriptor: the package alone.
	CHECK(sfve::colours::InstalledSources(package, data, problem).Cutout("RYU", 0, 0) ==
		PictureSources({package}).Cutout("RYU", 0, 0));
	CHECK(problem.empty());
	// A descriptor: its pack first, whether or not it has arrived.
	const std::string sha(64, 'e');
	const Pack pack{sha.substr(0, 16), 10, sha, Url(sha.substr(0, 16))};
	Write(package / sfve::colours::kDescriptorFile, Descriptor(pack).dump());
	CHECK(sfve::colours::InstalledSources(package, data, problem).Cutout("RYU", 0, 0) ==
		PictureSources({sfve::colours::PackFolder(data, pack), package}).Cutout("RYU", 0, 0));
	// Without a data folder there is no pack to read.
	CHECK(sfve::colours::InstalledSources(package, fs::path(), problem).Cutout("RYU", 0, 0) ==
		PictureSources({package}).Cutout("RYU", 0, 0));
	// A descriptor that is not one: the package alone, and why.
	Write(package / sfve::colours::kDescriptorFile, "{");
	CHECK(sfve::colours::InstalledSources(package, data, problem).Cutout("RYU", 0, 0) ==
		PictureSources({package}).Cutout("RYU", 0, 0));
	CHECK(!problem.empty());
	fs::remove_all(root);
}
