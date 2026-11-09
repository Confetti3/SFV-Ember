#pragma once

// The name a copy's player plays under (EMBER_LIBRARY.md section 41). Rooms
// refuse a second member with a name already in them, so every player's
// copy must play under their own: the profile's saved name, which the
// launcher gives a first start as SF4 Ember's does ("Player 1234", unique per
// player; sf4e::launcher::EnsureUniqueDisplayName) and which is never
// overwritten here.
//
// A test copy (instance N > 0, a development build's) shares the player's
// settings, so it plays as "SFV N+1" and never writes that name back
// (SavedSettings' PreferencesToSave).

#include <string>

namespace sfve::adapter {

// The name for this copy, from its instance and the profile's saved
// displayName: a test copy's own, else the saved one. Empty when nothing is
// saved (no launcher started this copy): the runtime then plays as "Player".
std::string PlayerNameFor(int instance, const std::string& saved);

// Whether this copy is a test copy, whose own name stays out of the settings.
bool TestCopy(int instance);

// The profile's saved displayName (settings.json); empty when there is none
// or the settings cannot be read (logged).
std::string SavedDisplayName();

} // namespace sfve::adapter
