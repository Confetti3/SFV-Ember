// SFV Ember's problem reports are on unless the player turned them off
// (launcher/ReportsDefault.hxx): turned on once, then the player's own.

#include <doctest/doctest.h>

#include "launcher/ReportsDefault.hxx"
#include "netplay/SettingsStore.hxx"
#include "platform/ReportWorkflow.hxx"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace {

std::filesystem::path FreshFolder(const char* name)
{
	const std::filesystem::path folder =
		std::filesystem::temp_directory_path() / (std::string("sfve-reports-") + name + "-" + std::to_string(GetCurrentProcessId()));
	std::filesystem::remove_all(folder);
	return folder;
}

bool SaveReports(const std::filesystem::path& folder, bool on)
{
	std::string error;
	return sf4e::netplay::SettingsStore(folder.wstring()).SaveLauncher({{"sendProblemReportsWithDump", on}}, error);
}

} // namespace

TEST_CASE("problem reports are turned on once, then stay as the player sets them")
{
	SUBCASE("nothing saved yet: on")
	{
		const std::filesystem::path folder = FreshFolder("fresh");
		CHECK(sfve::launcher::ApplyReportsDefault(folder.wstring()));
		CHECK(sf4e::reports::ReportsOn(folder.wstring()) == std::optional<bool>(true));
		std::filesystem::remove_all(folder);
	}
	SUBCASE("saved off before SFV's default: on, once")
	{
		const std::filesystem::path folder = FreshFolder("saved-off");
		REQUIRE(SaveReports(folder, false));
		CHECK(sfve::launcher::ApplyReportsDefault(folder.wstring()));
		CHECK(sf4e::reports::ReportsOn(folder.wstring()) == std::optional<bool>(true));
		// The player turns them off afterwards: the next start leaves that.
		REQUIRE(SaveReports(folder, false));
		CHECK(sfve::launcher::ApplyReportsDefault(folder.wstring()));
		CHECK(sf4e::reports::ReportsOn(folder.wstring()) == std::optional<bool>(false));
		std::filesystem::remove_all(folder);
	}
	SUBCASE("no settings folder: nothing done")
	{
		CHECK_FALSE(sfve::launcher::ApplyReportsDefault(L""));
	}
}
