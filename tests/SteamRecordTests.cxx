// The account Steam's record is put right with (launcher/SteamRecord.hxx).

#include <doctest/doctest.h>

#include "launcher/SteamRecord.hxx"

#include <vector>

using sfve::launcher::MostRecentAccount;

TEST_CASE("the account Steam signed in last is the one marked MostRecent")
{
	const char* vdf = R"("users"
{
	"76561198000000001"
	{
		"AccountName"		"first"
		"PersonaName"		"First"
		"MostRecent"		"0"
	}
	"76561198135903080"
	{
		"AccountName"		"second"
		"PersonaName"		"Second"
		"mostrecent"		"1"
		"Timestamp"		"1794230000"
	}
}
)";
	// 76561198135903080 - 76561197960265728
	CHECK(MostRecentAccount(vdf) == 175637352u);
}

TEST_CASE("a single user is the account even unmarked; several unmarked name none")
{
	CHECK(MostRecentAccount(R"("users" { "76561197960265729" { "AccountName" "only" } })") == 1u);
	CHECK_FALSE(MostRecentAccount(R"("users" { "76561197960265729" { "MostRecent" "0" } "76561197960265730" { "MostRecent" "0" } })"));
	CHECK_FALSE(MostRecentAccount(""));
	CHECK_FALSE(MostRecentAccount(R"("users" { })"));
	// Not a SteamID64 of an individual account: no account.
	CHECK_FALSE(MostRecentAccount(R"("users" { "12345678901234567" { "MostRecent" "1" } })"));
	CHECK_FALSE(MostRecentAccount(R"("users" { "7656119800000000x" { "MostRecent" "1" } })"));
}

TEST_CASE("only the Steam clients of this session count: another user's, left running by fast user switching, is not the one the game talks to")
{
	using sfve::launcher::ProcessEntry;
	const std::vector<ProcessEntry> processes = {
		{100, 1, L"explorer.exe"},
		{200, 1, L"Steam.exe"},       // this user's, in this session
		{300, 2, L"steam.exe"},       // another user's, in another session
		{310, 2, L"steamwebhelper.exe"},
		{400, 1, L"steamwebhelper.exe"},
		{500, 1, L"SteamService.exe"},
		{600, 0xFFFFFFFFu, L"steam.exe"}, // a process whose session could not be asked
	};
	CHECK(sfve::launcher::SteamClientsInSession(processes, 1) == std::vector<uint32_t>{200});
	CHECK(sfve::launcher::SteamClientsInSession(processes, 2) == std::vector<uint32_t>{300});
	CHECK(sfve::launcher::SteamClientsInSession(processes, 3).empty());
	CHECK(sfve::launcher::SteamClientsInSession({}, 1).empty());
}
