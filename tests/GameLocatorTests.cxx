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

TEST_CASE("a picked top folder finds the game below it, not UE4's starter beside it")
{
	// Steam's "Browse local files" opens the install folder, which holds
	// UE4's 175 KB starter StreetFighterV.exe; the game is in
	// StreetFighterV\Binaries\Win64 below it. Both exist here, as on disk.
	const std::wstring top = L"G:\\SteamLibrary\\steamapps\\common\\StreetFighterV";
	const std::wstring inner = top + L"\\StreetFighterV\\Binaries\\Win64";
	const std::wstring starter = top + L"\\StreetFighterV.exe", game = inner + L"\\StreetFighterV.exe";
	const auto installed = [&](const std::wstring& path) { return path == starter || path == game; };
	for (const std::wstring& picked : {top, top + L"\\"}) {
		const sf4e::launcher::GameLocation location = sf4e::launcher::LocateGame(picked, L"", installed, nullptr);
		CHECK(location.fromRecovery);
		CHECK(location.directory == inner);
		CHECK(location.executable == game);
	}
	// The game's own folder, picked or saved, is used as it is.
	CHECK(sf4e::launcher::LocateGame(inner, L"", installed, nullptr).executable == game);
	const sf4e::launcher::GameLocation saved = sf4e::launcher::LocateGame(L"", inner, installed, nullptr);
	CHECK(saved.directory == inner);
	CHECK(saved.executable == game);
	// With the game missing below, the starter is what the folder holds: the
	// launch's build check (SfvLaunch's StartGame) refuses it before it runs.
	const auto starterOnly = [&](const std::wstring& path) { return path == starter; };
	CHECK(sf4e::launcher::FindGame(top, starterOnly).executable == starter);
}
