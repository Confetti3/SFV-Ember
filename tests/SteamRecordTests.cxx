// The account Steam's record is put right with (launcher/SteamRecord.hxx).

#include <doctest/doctest.h>

#include "launcher/SteamRecord.hxx"

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
