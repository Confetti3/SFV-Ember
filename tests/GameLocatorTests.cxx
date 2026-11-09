// Where SFV Ember's launcher finds the game (the library's GameLocator under
// SFV Ember's product), checked without Steam.

#include <doctest/doctest.h>

#include "launcher/GameLocator.hxx"

#include <string>

TEST_CASE("the game folder is found from Steam's manifest for app 310950")
{
	// A library's appmanifest_310950.acf names StreetFighterV; the game is in
	// its StreetFighterV\Binaries\Win64 folder.
	const std::string manifest = "\"AppState\"\n{\n\t\"appid\"\t\t\"310950\"\n\t\"installdir\"\t\t\"StreetFighterV\"\n}\n";
	CHECK(sf4e::launcher::ManifestInstallDir(manifest) == L"StreetFighterV");
	CHECK(sf4e::launcher::ManifestInstallDir("\"AppState\"\n{\n\t\"appid\"\t\"45760\"\n\t\"installdir\"\t\"X\"\n}\n").empty());
	const auto folders = sf4e::launcher::GameFoldersInLibrary(L"G:\\SteamLibrary", manifest);
	REQUIRE(folders.size() == 1);
	CHECK(folders[0] == L"G:\\SteamLibrary\\steamapps\\common\\StreetFighterV\\StreetFighterV\\Binaries\\Win64");
	const auto renamed = sf4e::launcher::GameFoldersInLibrary(L"D:\\Games\\",
		"\"AppState\"\n{\n\t\"appid\"\t\"310950\"\n\t\"installdir\"\t\"SFV\"\n}\n");
	REQUIRE(renamed.size() == 2);
	CHECK(renamed[0] == L"D:\\Games\\steamapps\\common\\SFV\\StreetFighterV\\Binaries\\Win64");
	CHECK(renamed[1] == L"D:\\Games\\steamapps\\common\\StreetFighterV\\StreetFighterV\\Binaries\\Win64");
}
