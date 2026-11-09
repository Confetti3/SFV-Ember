#pragma once

// The content lists the SFV catalog tests fill the catalog from, shaped as
// the game's, and the picks and matches they ask about. The catalog is filled
// once per process and keeps the first pick space it takes, so every test
// that fills it fills it from TestContent (FillTestCatalog, which is
// harmless to repeat): no test depends on another having filled it first.
// Only CatalogLifecycleTests, in a process of its own, sees it empty or
// sealed.

#include <doctest/doctest.h>

#include "ember/SfvCatalog.hxx"
#include "mod/GameContent.hxx"

#include "common/GameCatalog.hxx"
#include "common/SelectionPick.hxx"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sfve::catalog_fixture {

// A costume the game lets this copy's player pick, in every colour but
// `notOwned`, unless `owned` says otherwise.
inline mod::CostumeContent Costume(int id, std::vector<int> colours, const char* name = "", bool installed = true,
	bool owned = true, std::vector<int> notOwned = {})
{
	std::vector<int> ownedColours;
	for (int colour : colours) {
		if (std::find(notOwned.begin(), notOwned.end(), colour) == notOwned.end()) ownedColours.push_back(colour);
	}
	return {id, std::move(colours), name, installed, owned, std::move(ownedColours)};
}

inline mod::FighterContent Fighter(const char* code, std::vector<mod::CostumeContent> costumes, const char* name = "",
	bool installed = true, bool owned = true)
{
	return {code, std::move(costumes), name, installed, owned};
}

// A content list shaped as the game's: fighters in the game's order with
// their costume numbers (with gaps, as the spec leaves some out) and each
// costume's colours (EX colours from 100); names where the game gives one;
// one fighter and one costume this copy has not installed; one fighter, one
// costume and two colours the game does not let this copy's player pick
// (KEN; CNL's costume 2; RYU's EX colours of costume 4); a costume the game
// lets them pick in none of its colours (RYU's costume 1, as -sfve_unowned
// marks of each of its colours make it), so no look of it is theirs; the
// stages' names where the game gives one (none for the second).
inline mod::ContentLists TestContent()
{
	return {{Fighter("RYU",
				 {Costume(0, {0, 1, 2}, "Story"),
					 Costume(1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, "", true, true, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}),
					 Costume(4, {0, 1, 100, 101}, "Nostalgia", true, true, {100, 101})},
				 "Ryu"),
				Fighter("CNL", {Costume(0, {0, 1}), Costume(2, {0}, "", true, false)}, "Chun-Li"),
				Fighter("KEN", {Costume(0, {0})}, "Ken", true, false),
				Fighter("CMY", {Costume(3, {5, 6})}, "", false),
				Fighter("BSN", {Costume(0, {0, 1}), Costume(1, {0, 1}, "", false)}, "M. Bison")},
		{"S01", "S02", "S25"}, {"Stage One", "", "Stage Twenty-five"}};
}

// Lists with only the codes given, each fighter in one costume and colour.
inline mod::ContentLists CodesOnly(std::vector<std::string> fighters, std::vector<std::string> stages)
{
	mod::ContentLists lists;
	for (const std::string& code : fighters) lists.fighters.push_back(Fighter(code.c_str(), {Costume(0, {0})}));
	lists.stages = std::move(stages);
	return lists;
}

inline void FillTestCatalog()
{
	REQUIRE(adapter::FillCatalog(TestContent()));
}

inline sf4e::selection::Pick PickOf(int fighter, int costume = 0, int color = 0)
{
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	pick.costume = costume;
	pick.color = color;
	return pick;
}

// A match of the fighters in their first costume and colour.
inline adapter::RoomMatch MatchOf(int p1, int p2, std::int64_t stage, int rounds = 3, int seconds = 99)
{
	adapter::RoomMatch match;
	match.picks[0] = PickOf(p1);
	match.picks[1] = PickOf(p2);
	for (sf4e::selection::Pick& pick : match.picks) ember::Catalog().normalizePick(pick, false, nullptr, nullptr);
	match.stage = stage;
	match.rounds = rounds;
	match.roundSeconds = seconds;
	return match;
}

} // namespace sfve::catalog_fixture
