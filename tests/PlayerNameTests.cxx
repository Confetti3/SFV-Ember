// Every player plays under their own name, and a test copy never writes its
// name over theirs (ember/PlayerName, ember/SavedSettings; EMBER_LIBRARY.md
// section 42).

#include <doctest/doctest.h>

#include "ember/PlayerName.hxx"
#include "ember/SavedSettings.hxx"
#include "launcher/OldDefaultName.hxx"

TEST_CASE("a player's copy plays under the saved name, a test copy under its own")
{
	CHECK(sfve::adapter::PlayerNameFor(0, "Player 4821") == "Player 4821");
	CHECK(sfve::adapter::PlayerNameFor(0, "Ryu main") == "Ryu main");
	// Nothing saved (no launcher started it): the runtime's own "Player".
	CHECK(sfve::adapter::PlayerNameFor(0, "").empty());
	CHECK(sfve::adapter::PlayerNameFor(1, "Player 4821") == "SFV 2");
	CHECK(sfve::adapter::PlayerNameFor(2, "") == "SFV 3");
}

TEST_CASE("only 0.1.0-beta.1's hard-coded names count as unset")
{
	using sfve::launcher::IsOldDefaultName;
	for (const char* old : {"SFV 1", "SFV 2", "SFV 3", "SFV 4"}) CHECK(IsOldDefaultName(old));
	for (const char* kept : {"SFV 5", "SFV 1 ", "SFV 10", "sfv 1", "SFV", "Player 4821", "Ryu main", ""}) CHECK_FALSE(IsOldDefaultName(kept));
	CHECK_FALSE(IsOldDefaultName(nullptr));
}

TEST_CASE("a test copy saves its settings without its name")
{
	sf4e::netplay::PlayerPreferences preferences;
	preferences.displayName = "SFV 2";
	const nlohmann::json player = sfve::adapter::PreferencesToSave(preferences, 0);
	const nlohmann::json test = sfve::adapter::PreferencesToSave(preferences, 1);
	CHECK(player.at("displayName") == "SFV 2");
	CHECK_FALSE(test.contains("displayName"));
	// Everything else is saved alike.
	nlohmann::json withoutName = player;
	withoutName.erase("displayName");
	CHECK(test == withoutName);
}
