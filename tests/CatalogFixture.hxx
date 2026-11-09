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
// `notOwned`, unless `owned` says otherwise. The account's entitlement cache
// grants what the game's answer gives (that answer is made from the cache).
inline mod::CostumeContent Costume(int id, std::vector<int> colours, const char* name = "", bool installed = true,
	bool owned = true, std::vector<int> notOwned = {})
{
	mod::CostumeContent costume{id, {}, name, installed, owned, owned};
	for (int colour : colours) {
		const bool theirs = std::find(notOwned.begin(), notOwned.end(), colour) == notOwned.end();
		costume.colours.push_back({colour, theirs, theirs});
	}
	return costume;
}

// A costume installed here that the account's entitlement cache lacks, in
// none of its colours: the game's answer leaves it out, as it leaves out
// the costumes SFV's menus do not offer this copy (EMBER_LIBRARY.md section
// 41).
inline mod::CostumeContent NotEntitled(int id, std::vector<int> colours, const char* name = "")
{
	mod::CostumeContent costume{id, {}, name, true, false, false};
	for (int colour : colours) costume.colours.push_back({colour, false, false});
	return costume;
}

// The costume's colours this copy's player may pick, by number, in order.
inline std::vector<int> OwnedColours(const mod::CostumeContent& costume)
{
	std::vector<int> owned;
	for (const mod::ColourContent& colour : costume.colours) {
		if (colour.owned) owned.push_back(colour.id);
	}
	return owned;
}

inline mod::FighterContent Fighter(const char* code, std::vector<mod::CostumeContent> costumes, const char* name = "",
	bool installed = true, bool owned = true)
{
	mod::FighterContent fighter{code, std::move(costumes), name, installed, owned};
	fighter.entitled = owned;
	return fighter;
}

// The fighter with the V-Skills and V-Triggers named, I first.
inline mod::FighterContent WithChoices(mod::FighterContent fighter, std::vector<std::string> vSkills,
	std::vector<std::string> vTriggers)
{
	fighter.vSkills = std::move(vSkills);
	fighter.vTriggers = std::move(vTriggers);
	return fighter;
}

// A stage of the spec's universe, installed here and its codes granted by
// the account's entitlement cache unless said otherwise. `entitled` is the
// game's selectable flag, which needs the install as well, and `owned` is
// what the default rule (ContentOwnership::Entitlements) makes of the two.
inline mod::StageContent Stage(const char* code, const char* name = "", bool installed = true, bool granted = true)
{
	const bool entitled = installed && granted;
	return {code, name, installed, entitled, entitled};
}

// A content list shaped as the game's: fighters in the game's order with
// their costume numbers (with gaps, as the spec leaves some out) and each
// costume's colours (EX colours from 100); names where the game gives one;
// one fighter and one costume this copy has not installed; one fighter, one
// costume and two colours the game does not let this copy's player pick
// (KEN; CNL's costume 2; RYU's EX colours of costume 4); a costume the game
// lets them pick in none of its colours (RYU's costume 1, as -sfve_unowned
// marks of each of its colours make it), so no look of it is theirs; a
// one-colour costume installed here that the account's entitlement cache
// lacks (CNL's costume 3, as SFV's "EX character" skins are); the
// stages' names where the game gives one (none for the second); a stage
// installed here whose codes the account's entitlement cache lacks (S02, id
// 1), which the default rule does not offer this copy's player; and a fourth
// stage of the universe this copy has not installed (S26, id 3). The
// fighters' V-Skills and V-Triggers (section 40) are every mix: RYU and BSN
// two of each, CNL two V-Skills and one V-Trigger, CMY one V-Skill and two
// V-Triggers, KEN one of each; BSN's are unnamed.
inline mod::ContentLists TestContent()
{
	return {{WithChoices(Fighter("RYU",
							 {Costume(0, {0, 1, 2}, "Story"),
								 Costume(1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, "", true, true, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}),
								 Costume(4, {0, 1, 100, 101}, "Nostalgia", true, true, {100, 101})},
							 "Ryu"),
				 {"Skill One", "Skill Two"}, {"Trigger One", "Trigger Two"}),
				WithChoices(Fighter("CNL", {Costume(0, {0, 1}), Costume(2, {0}, "", true, false), NotEntitled(3, {0}, "EX Skin")},
								"Chun-Li"),
					{"Skill One", "Skill Two"}, {"Trigger One"}),
				Fighter("KEN", {Costume(0, {0})}, "Ken", true, false),
				WithChoices(Fighter("CMY", {Costume(3, {5, 6})}, "", false), {"Skill One"}, {"Trigger One", "Trigger Two"}),
				WithChoices(Fighter("BSN", {Costume(0, {0, 1}), Costume(1, {0, 1}, "", false)}, "M. Bison"), {"", ""}, {"", ""})},
		{Stage("S01", "Stage One"), Stage("S02", "", true, false), Stage("S25", "Stage Twenty-five"), Stage("S26", "", false)}};
}

// The id of TestContent's stage this copy has not installed.
constexpr std::int64_t kUninstalledStage = 3;
// The id of TestContent's stage installed here that the account's
// entitlement cache lacks.
constexpr std::int64_t kNotEntitledStage = 1;

// Lists with only the codes given, each fighter in one costume and colour,
// every stage installed.
inline mod::ContentLists CodesOnly(std::vector<std::string> fighters, std::vector<std::string> stages)
{
	mod::ContentLists lists;
	for (const std::string& code : fighters) lists.fighters.push_back(Fighter(code.c_str(), {Costume(0, {0})}));
	for (const std::string& code : stages) lists.stages.push_back(Stage(code.c_str()));
	return lists;
}

inline void FillTestCatalog()
{
	REQUIRE(adapter::FillCatalog(TestContent()));
}

inline sf4e::selection::Pick PickOf(int fighter, int costume = 0, int color = 0, picks::Loadout loadout = {})
{
	sf4e::selection::Pick pick;
	pick.fighter = fighter;
	pick.costume = costume;
	pick.color = color;
	picks::SetLoadout(pick, loadout);
	return pick;
}

// The loadouts by their parts' numerals: V-Skill, then V-Trigger.
constexpr picks::Loadout kOneOne{{0, 0}};
constexpr picks::Loadout kOneTwo{{0, 1}};
constexpr picks::Loadout kTwoOne{{1, 0}};
constexpr picks::Loadout kTwoTwo{{1, 1}};

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
