// The launcher reads an executable's build before it starts it
// (launcher/ExecutableBuild): the PE stamp the mod's BuildCheck verifies.

#include <doctest/doctest.h>

#include "launcher/ExecutableBuild.hxx"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("an executable's build is its PE stamp, read from the file")
{
	// This test program, on disk and as Windows mapped it.
	wchar_t self[MAX_PATH] = {};
	REQUIRE(GetModuleFileNameW(nullptr, self, MAX_PATH));
	const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const std::optional<uint32_t> stamp = sfve::launcher::ExecutableTimestamp(self);
	REQUIRE(stamp.has_value());
	CHECK(*stamp == nt->FileHeader.TimeDateStamp);

	// Not an image, or no file at all.
	const std::filesystem::path text = std::filesystem::temp_directory_path() / ("sfve-exe-test-" + std::to_string(GetCurrentProcessId()) + ".exe");
	std::ofstream(text) << "MZ but no image";
	CHECK_FALSE(sfve::launcher::ExecutableTimestamp(text.wstring()).has_value());
	std::filesystem::remove(text);
	CHECK_FALSE(sfve::launcher::ExecutableTimestamp(text.wstring()).has_value());
}
