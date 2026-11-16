#include "mod/RuntimeModules.hxx"

#include <doctest/doctest.h>

#include <windows.h>

#include <string>

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
	CHECK(kernel.rfind("kernel32.dll 10.", 0) == 0);
	CHECK(kernel.find(" at ") != std::string::npos);
	CHECK(kernel.find("the game's own folder") == std::string::npos);
	// The same module, with its own folder as the game's folder: named as such.
	CHECK(sfve::mod::DescribeModule(L"kernel32.dll", system).find("the game's own folder") != std::string::npos);
	CHECK(sfve::mod::DescribeModule(L"no_such_runtime_140.dll", system) == "no_such_runtime_140.dll: not loaded");
	CHECK_FALSE(sfve::mod::FileVersionText(std::wstring(system) + L"\\kernel32.dll").empty());
	CHECK(sfve::mod::FileVersionText(L"C:\\no\\such\\file.dll").empty());
}
