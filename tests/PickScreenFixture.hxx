#pragma once

// What the pick screen's tests share (SfvSelectionScreenTests, SfvPickPagesTests,
// TinyFightersTests): the fixture catalog's fighters, a pick of one of them,
// rows by id, and the actions Left, Right and a choice make. The fighters are
// CatalogFixture's.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvPick.hxx"
#include "ember/SfvPickPages.hxx"
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelectionScreen.hxx"

#include "ui/ApplicationShell.hxx"
#include "ui/GameMenu.hxx"

#include <algorithm>
#include <vector>

namespace sfve::pick_screen_fixture {

using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::ui::MenuAction;
using sf4e::ui::MenuEntry;

inline constexpr int kRyu = 0, kCnl = 1, kKen = 2, kBsn = 4;

inline PlayerPick PlayerPickOf(int fighter, picks::Loadout loadout)
{
	PlayerPick pick;
	pick.pick = PickOf(fighter, 0, 0, loadout);
	return pick;
}

inline const MenuEntry& RowOf(const std::vector<MenuEntry>& rows, const char* id)
{
	const auto row = std::find_if(rows.begin(), rows.end(), [id](const MenuEntry& entry) { return entry.id == id; });
	REQUIRE(row != rows.end());
	return *row;
}

inline bool HasRow(const std::vector<MenuEntry>& rows, const char* id)
{
	return std::any_of(rows.begin(), rows.end(), [id](const MenuEntry& entry) { return entry.id == id; });
}

inline bool SameLook(const sf4e::selection::Pick& a, const sf4e::selection::Pick& b)
{
	return a.fighter == b.fighter && a.costume == b.costume && a.color == b.color && picks::LoadoutOf(a) == picks::LoadoutOf(b);
}

inline std::vector<MenuEntry> PlayerRows(const PlayerPick& pick)
{
	return pickpages::Rows(pick, true, true, true, StageOptions(MissingStages()));
}

inline MenuAction Adjust(const char* row, int delta)
{
	MenuAction action;
	action.kind = MenuAction::Adjust;
	action.id = row;
	action.delta = delta;
	return action;
}

inline MenuAction Choose(const char* row, const char* option)
{
	MenuAction action;
	action.kind = MenuAction::Chosen;
	action.id = row;
	action.text = option;
	return action;
}

inline sf4e::ui::SelectionStep Step(sf4e::ui::SelectionStep::Field field, int delta)
{
	sf4e::ui::SelectionStep step;
	step.field = field;
	step.delta = delta;
	return step;
}
} // namespace sfve::pick_screen_fixture
