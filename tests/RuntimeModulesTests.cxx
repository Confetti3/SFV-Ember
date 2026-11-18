#include "mod/RuntimeModules.hxx"

#include <doctest/doctest.h>

#include <windows.h>

#include <string>
#include <vector>

TEST_CASE("a file's folder is compared without case or slash direction")
{
	using sfve::mod::InFolder;
	CHECK(InFolder(L"C:\\Games\\SFV\\Binaries\\Win64\\msvcp140.dll", L"c:/games/sfv/binaries/WIN64"));
	CHECK_FALSE(InFolder(L"C:\\Windows\\System32\\msvcp140.dll", L"C:\\Games\\SFV\\Binaries\\Win64"));
	CHECK_FALSE(InFolder(L"C:\\Games\\SFV\\Binaries\\Win64\\sub\\msvcp140.dll", L"C:\\Games\\SFV\\Binaries\\Win64"));
	CHECK_FALSE(InFolder(L"msvcp140.dll", L"C:\\Games"));
	CHECK_FALSE(InFolder(L"C:\\Games\\a.dll", L"")); // no game folder known: never the game's
}

TEST_CASE("a loaded module is described by its path and version, and one that is not loaded says so")
{
	wchar_t system[MAX_PATH] = {};
	GetSystemDirectoryW(system, MAX_PATH);
	const std::string kernel = sfve::mod::DescribeModule(L"kernel32.dll", L"C:\\Games\\SFV\\Win64");
	CAPTURE(kernel);
	// Its version is the file's own fixed version (6.2.x for kernel32 on
	// Windows 10 and 11, not the OS's 10.0), read the way the description reads it.
	const std::string version = sfve::mod::FileVersionText(std::wstring(system) + L"\\kernel32.dll");
	CHECK_FALSE(version.empty());
	CHECK(kernel.rfind("kernel32.dll " + version + " at ", 0) == 0);
	CHECK(kernel.find(" at ") != std::string::npos);
	CHECK(kernel.find("the game's own folder") == std::string::npos);
	// The same module, with its own folder as the game's folder: named as such.
	CHECK(sfve::mod::DescribeModule(L"kernel32.dll", system).find("the game's own folder") != std::string::npos);
	CHECK(sfve::mod::DescribeModule(L"no_such_runtime_140.dll", system) == "no_such_runtime_140.dll: not loaded");
	CHECK_FALSE(sfve::mod::FileVersionText(std::wstring(system) + L"\\kernel32.dll").empty());
	CHECK(sfve::mod::FileVersionText(L"C:\\no\\such\\file.dll").empty());
}

TEST_CASE("the modules besides Windows' own are named, the game folder's apart from the rest")
{
	using sfve::mod::OtherModules;
	const std::vector<std::wstring> paths = {
		L"C:\\Games\\SFV\\StreetFighterV\\Binaries\\Win64\\StreetFighterV.exe",
		L"C:\\Games\\SFV\\StreetFighterV\\Binaries\\Win64\\sfv_ember.dll",
		L"c:/games/sfv/Engine/Binaries/ThirdParty/PhysX/PhysX3_x64.dll",
		L"C:\\WINDOWS\\System32\\KERNEL32.DLL",
		L"C:\\Windows\\WinSxS\\amd64_microsoft.windows.common-controls\\comctl32.dll",
		L"C:\\Users\\someone\\Polarity\\polarity.live.0.0.dll",
		L"C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll",
		L"C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll",
		L"C:\\Games\\SFVOther\\other.dll", // a folder whose name only starts like the game's
	};
	const OtherModules modules = sfve::mod::SortModules(paths, L"C:\\Windows", L"C:\\Games\\SFV");
	CHECK(modules.game == std::vector<std::string>{"PhysX3_x64.dll", "StreetFighterV.exe", "sfv_ember.dll"});
	CHECK(modules.elsewhere == std::vector<std::string>{"gameoverlayrenderer64.dll", "other.dll", "polarity.live.0.0.dll"});
	CHECK(sfve::mod::OtherModulesText(modules) ==
		"elsewhere: gameoverlayrenderer64.dll, other.dll, polarity.live.0.0.dll; in the game's folder: PhysX3_x64.dll, "
		"StreetFighterV.exe, sfv_ember.dll");
	CHECK(sfve::mod::OtherModulesText({}) == "none");
	// No game folder known: every module not Windows' is elsewhere.
	CHECK(sfve::mod::SortModules(paths, L"C:\Windows", L"").game.empty());
}

TEST_CASE("this process's own exe is among its other modules")
{
	const sfve::mod::OtherModules modules = sfve::mod::LoadedOtherModules();
	CHECK(modules.game.size() + modules.elsewhere.size() >= 1);
}
