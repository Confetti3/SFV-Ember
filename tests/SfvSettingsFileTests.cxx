// The one helper SFV Ember's small settings files are edited through
// (ember/SfvSettingsFile.hxx): a missing or unreadable file is an empty
// object, a change keeps the file's other keys, and two owners changing their
// own keys at once lose neither.

#include <doctest/doctest.h>

#include "ember/SfvSettingsFile.hxx"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

using namespace sfve::adapter;

constexpr const wchar_t* kFile = L"settings-file-test.json";

std::filesystem::path FreshFolder(const char* name)
{
	const std::filesystem::path folder =
		std::filesystem::temp_directory_path() / (std::string("sfve-settings-") + name + "-" + std::to_string(GetCurrentProcessId()));
	std::filesystem::remove_all(folder);
	return folder;
}

} // namespace

TEST_CASE("a file that is missing is an empty object with no problem, and one that is not an object says why")
{
	const std::filesystem::path folder = FreshFolder("read");
	std::string problem = "left over";
	CHECK(ReadSettingsFile(folder, kFile, problem) == nlohmann::json::object());
	CHECK(problem.empty());
	std::filesystem::create_directories(folder);
	std::ofstream(folder / kFile, std::ios::binary) << "[1, 2]";
	CHECK(ReadSettingsFile(folder, kFile, problem) == nlohmann::json::object());
	CHECK(problem.find("settings-file-test.json is no settings object") == 0);
	std::ofstream(folder / kFile, std::ios::binary | std::ios::trunc) << "{not json";
	CHECK(ReadSettingsFile(folder, kFile, problem) == nlohmann::json::object());
	CHECK_FALSE(problem.empty());
	std::filesystem::remove_all(folder);
}

TEST_CASE("a change keeps the file's other keys, makes the folder, and replaces a file that is no object")
{
	const std::filesystem::path folder = FreshFolder("update");
	std::string error;
	REQUIRE(UpdateSettingsFile(folder, kFile, [](nlohmann::json& file) { file["a"] = 1; }, error));
	REQUIRE(UpdateSettingsFile(folder, kFile, [](nlohmann::json& file) { file["b"] = "two"; }, error));
	std::string problem;
	const nlohmann::json file = ReadSettingsFile(folder, kFile, problem);
	CHECK(problem.empty());
	CHECK(file["a"] == 1);
	CHECK(file["b"] == "two");
	std::ofstream(folder / kFile, std::ios::binary | std::ios::trunc) << "[]";
	REQUIRE(UpdateSettingsFile(folder, kFile, [](nlohmann::json& object) { object["c"] = true; }, error));
	CHECK(ReadSettingsFile(folder, kFile, problem) == nlohmann::json({{"c", true}}));
	std::filesystem::remove_all(folder);
}

TEST_CASE("two owners changing their own keys at once lose neither's")
{
	const std::filesystem::path folder = FreshFolder("threads");
	constexpr int kRounds = 60;
	const auto owner = [&folder](const char* key) {
		for (int round = 1; round <= kRounds; round++) {
			std::string error;
			CHECK(UpdateSettingsFile(folder, kFile, [&](nlohmann::json& file) { file[key] = round; }, error));
		}
	};
	std::thread first(owner, "backgroundArtwork"), second(owner, "screenMode");
	first.join();
	second.join();
	std::string problem;
	const nlohmann::json file = ReadSettingsFile(folder, kFile, problem);
	CHECK(file["backgroundArtwork"] == kRounds);
	CHECK(file["screenMode"] == kRounds);
	std::filesystem::remove_all(folder);
}
