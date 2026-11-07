#pragma once

// SFV's own names for its league tiers, as the game names them in the
// player's language (EMBER_LIBRARY.md section 28): the localized text keyed
// "KW" / ID_CMN_LEAGUE_0000 to _0019, the game's twenty leagues from Rookie
// to Warlord, which the game builds itself (rva 0x6BDE20) and Ember reads at
// the main menu (mod::LocalizedText). SFV Ember's ten ranks are ten of those
// leagues; until the game has given the names, or where it gives none, the
// shell shows the product table's English names. Defined in SfvProduct.cxx,
// which every SFV Ember executable links; only the mod publishes.

#include <array>
#include <string>

namespace sfve::ranks {

// SFV's leagues in the game's order, ID_CMN_LEAGUE_0000 first.
constexpr int kLeagues = 20;
// The league each band (1 to 10) shows: the ten tiers without the Super and
// Ultra steps (Rookie, Bronze, Silver, Gold, Platinum, Diamond, Master,
// Grand Master, Ultimate Grand Master, Warlord).
constexpr std::array<int, 10> kBandLeague = {0, 1, 4, 7, 10, 13, 16, 17, 18, 19};

// The game's name for `band`'s league, UTF-8; empty until published, or for
// a band out of range. Any thread; a name once given never changes.
const char* Name(int band);

// Publishes the ten bands' names once (later calls are ignored). Game thread.
void Publish(const std::array<std::string, 10>& names);

} // namespace sfve::ranks
