// The paks the engine mounts, judged against Steam's own (mod/PakRules.hxx).

#include <doctest/doctest.h>

#include "mod/PakRules.hxx"

#include <string>
#include <vector>

using namespace sfve::mod::paks;

namespace {

std::vector<Found> StockInstall()
{
	std::vector<Found> found;
	for (size_t i = 0; i < kStockCount; i++) found.push_back({Folder::Content, kStock[i].name, kStock[i].size});
	return found;
}

} // namespace

TEST_CASE("the stock set is the build's 69 chunks, each once")
{
	CHECK(kStockCount == 69);
	CHECK(std::string(kStock[0].name) == "pakchunk0-WindowsNoEditor.pak");
	CHECK(kStock[0].size == 6336284478ull);
	CHECK(std::string(kStock[68].name) == "pakchunk68-WindowsNoEditor.pak");
	for (size_t i = 0; i < kStockCount; i++) {
		for (size_t j = i + 1; j < kStockCount; j++) CHECK(std::string(kStock[i].name) != kStock[j].name);
	}
}

TEST_CASE("an untouched install is all stock")
{
	const Census census = Judge(StockInstall());
	CHECK(AllStock(census));
	CHECK(census.missing.empty());
	CHECK(Summary(census) == "69 of 69 stock paks");
	CHECK(Line(census.paks[0]) == "Content/pakchunk0-WindowsNoEditor.pak, 6336284478 bytes: stock");
}

TEST_CASE("a mod pak, wherever the engine finds it, is not stock")
{
	std::vector<Found> found = StockInstall();
	found.push_back({Folder::Content, "~mods/GUL_moves_P.pak", 1234});
	found.push_back({Folder::Saved, "pakchunk3-WindowsNoEditor.pak", kStock[3].size}); // a stock name, in another folder
	found.push_back({Folder::Content, "sub/pakchunk4-WindowsNoEditor.pak", kStock[4].size});
	const Census census = Judge(found);
	CHECK_FALSE(AllStock(census));
	CHECK(census.missing.empty());
	const std::string summary = Summary(census);
	CHECK(summary.find("69 of 69 stock paks; NOT STOCK: Content/~mods/GUL_moves_P.pak (1234 bytes)") == 0);
	CHECK(summary.find("Saved/pakchunk3-WindowsNoEditor.pak") != std::string::npos);
	CHECK(summary.find("Content/sub/pakchunk4-WindowsNoEditor.pak") != std::string::npos);
}

TEST_CASE("a stock name with another size, and a missing chunk, are named")
{
	std::vector<Found> found = StockInstall();
	found[17].size += 1; // an altered or stale chunk
	found.erase(found.begin() + 20);
	const Census census = Judge(found);
	CHECK_FALSE(AllStock(census));
	REQUIRE(census.missing.size() == 1);
	CHECK(census.missing[0] == "pakchunk20-WindowsNoEditor.pak");
	const std::string summary = Summary(census);
	CHECK(summary.find("67 of 69 stock paks") == 0);
	CHECK(summary.find("a stock name with another size: Content/pakchunk17-WindowsNoEditor.pak") != std::string::npos);
	CHECK(summary.find("missing: pakchunk20-WindowsNoEditor.pak") != std::string::npos);
	CHECK(Line(census.paks[17]).find(": a stock name with another size") != std::string::npos);
}

TEST_CASE("names compare as Windows compares file names")
{
	std::vector<Found> found = StockInstall();
	found[0].path = "PAKCHUNK0-WindowsNoEditor.pak";
	CHECK(AllStock(Judge(found)));
}
