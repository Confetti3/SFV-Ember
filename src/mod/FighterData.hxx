#pragma once

// What each fighter unit of a bound battle computes with
// (core/MatchData.hxx; docs/design/beta10/desync.md): read from the unit's
// action part as the battle bound its moves (sfv::fighter::kResourceKey),
// never from the launch's picks, so it is what this copy's battle runs.

#include "core/MatchData.hxx"

#include <stdint.h>

namespace sfve::mod {

// Game thread, once the battle is bound. The version is the one this copy
// loaded: the key's, resolved against this copy's list as the game's
// resource lookup resolves it; the data, a hash of the move files the
// lookup gives for the key (GameContent's FighterMoveDataHash), read once
// here, a few milliseconds a fighter. A unit whose key cannot be read gives
// an empty code, an unknown version and no data (match_data::Compare then
// compares only what both copies know).
match_data::Fighter ReadFighterData(const uint8_t* actionPart, int side);

} // namespace sfve::mod
