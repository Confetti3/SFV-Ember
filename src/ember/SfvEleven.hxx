#pragma once

// Eleven, SFV's own random fighter, on the fighter board (beta.8,
// docs/design/beta8/eleven.md). The pick is "Eleven": each game this player
// fights plays as another fighter, in the look the game's Eleven wears, with
// a V-Skill and a V-Trigger drawn for the game. It rides on Random's
// machinery (SfvRandomFighter): the runtime rolls the fighter once for each
// game (room::RandomFighterPick) and sends the rolled pick with every Ready,
// so the room, the opponent's copy, every watcher and the replay all get the
// same concrete fighter, costume, colour and loadout. Nothing about Eleven
// is random at launch, and nothing about it is in the pick space, the room
// host's data or the build id: a launch only ever sees a fighter of the
// catalog.
//
// What this file adds is the look: a pure rule from a fighter and a seed
// (the runtime's roll, mixed with this run's salt) to the pick that game
// plays. It reads the catalog only through SfvCatalog's public lists, so it
// never offers a look this copy's player cannot ready (FitLookWithin).

#include "common/SelectionPick.hxx"

#include <cstdint>
#include <string>

namespace sfve::adapter {

// The seed for the game whose roll the runtime numbered `roll` (RuntimeSnapshot::randomRoll,
// counting from 1): a well mixed 64-bit value from the roll and `salt`, the
// same for the same two. The salt is this run's own (ElevenSalt), so two
// runs' rolls differ in their loadouts as they do in their fighters.
std::uint64_t ElevenSeed(std::uint64_t salt, std::uint64_t roll);

// This run's salt: drawn once from the system's random source on first use,
// never zero. Any thread.
std::uint64_t ElevenSalt();
// Development and tests: the salt to use from now on.
void SetElevenSaltForTest(std::uint64_t salt);

// How the board and the summaries title Eleven's pick: the game's name for
// Eleven and the fighter it rolled, as Random's "Random . RYU" does
// (SfvCatalog's ElevenName).
std::string ElevenTitle(const std::string& fighter);

// The pick Eleven plays as `fighter` (an id of the catalog) for the game
// `seed` stands for: the look the game's Eleven wears (ElevenSpecOf's costume
// and colour) where the fighter has that costume in that colour and this
// player can ready it, else the nearest look they can (FitLookWithin: the
// fighter's first costume they can ready, and its first colour), a V-Skill
// and a V-Trigger each drawn from the choices the fighter has, normal size,
// no handicap. The same fighter and seed give the same pick on every
// call and every copy of the same game data. For an id the catalog does not
// have, a pick of that id, untouched.
sf4e::selection::Pick ElevenPick(int fighter, std::uint64_t seed);

} // namespace sfve::adapter
