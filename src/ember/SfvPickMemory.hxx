#pragma once

// What the player's pick remembers, as a model with no game in it: for each
// fighter the look it was last stored in (costume, colour, V-Skill and
// V-Trigger, and the rest of the stored pick for this run), and the fighter
// the player chose last. Choosing a fighter shows that fighter's own look, as
// the game's character select does, and it is the memory that is saved
// between runs (SfvPickStore) so the next start finds the same fighter in
// the same looks.
//
// The saved form names fighters by their codes, not by their numbers, which
// are positions in the pick space and so move when the game's roster does.
// It is read back against the pick space: a fighter the space lacks, a
// costume, colour or loadout it does not hold for that fighter, or an entry
// that is not the saved form's own is left out, so the fighter starts in its
// default look (UnchosenLook). What this player may wear is the catalog's to
// judge (SfvPick fits a look to it as it takes it back).
//
// No game code: the tests build it from a pick space alone.

#include "ember/SfvPickSpace.hxx"

#include "common/GameCatalog.hxx"
#include "common/SelectionPick.hxx"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>

namespace sfve::picks {

// A pick of `fighter` that names no costume or colour and no loadout: the
// fitting makes it the fighter's first costume it ships, in that costume's
// first colour, with V-Skill I and V-Trigger I (picks::FitPick; the catalog
// then moves it to what the player owns). The look a fighter starts in
// before anyone has chosen one.
sf4e::selection::Pick UnchosenLook(int fighter);

class PickMemory {
public:
	// The look stored last for the fighter; none for one never stored.
	std::optional<sf4e::selection::Pick> LookOf(int fighter) const;
	// Stores the pick as its fighter's look. True when the costume, colour or
	// loadout changed, which is what is saved; the rest of the pick (its size
	// and costume code) is remembered for this run only.
	bool RememberLook(const sf4e::selection::Pick& pick);

	// The saved memory read after this run began to remember (a late
	// restore): the saved look of every fighter this run has stored none for,
	// and the saved fighter last chosen when this run chose none. This run's
	// own choices win, fighter by fighter. True when something was taken.
	bool MergeUnder(const PickMemory& saved);

	// The fighter the player chose last (not one Random rolled).
	std::optional<int> ChosenFighter() const { return chosen_; }
	// True when it changed.
	bool Choose(int fighter);

	// The saved form: {"version": 1, "fighter": "RYU", "looks": {"RYU":
	// {"costume", "colour", "vSkill", "vTrigger"}}}, the looks in the pick
	// space's order. Looks of a fighter outside the space are not written.
	nlohmann::json ToJson(const PickSpace& space) const;
	// The memory the saved form holds that the space allows; empty for
	// anything that is not the saved form.
	static PickMemory FromJson(const nlohmann::json& saved, const PickSpace& space);

private:
	std::array<std::optional<sf4e::selection::Pick>, ::ember::MaxFighters> looks_;
	std::optional<int> chosen_;
};

} // namespace sfve::picks
