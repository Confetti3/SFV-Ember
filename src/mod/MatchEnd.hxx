#pragma once

// The end of a linked match. After the last round SFV's versus battle shows
// a results menu (rematch, character select, exit...) that each player
// answers on their own copy; two linked copies could take different ways
// out. While a linked session runs, the mod keeps that menu from opening
// and decides for both copies at the same session frame: the session holds
// a pending end until the frame it was seen on is confirmed (NetplayMode),
// so a guessed end that a rollback takes back is never acted on, and both
// copies act on the same frame.
//
// The way out is the caller's, the same on both copies: a rematch (the
// game's own restart, what "Continue" does: same fighters and stage, in the
// same battle state) or back to the main menu (what "Exit" does), where the
// next match is launched from. A room's battle always goes back (NetplayMode). The match's result
// is read as the end is taken, on that confirmed frame, and kept.

#include "mod/MatchResult.hxx"

#include <stdint.h>

#include <optional>

namespace sfve::mod {

bool InstallMatchEnd(uintptr_t imageBase);

// While on, a versus battle's results menu does not open; the match end is
// kept for TakeEndedVersusBattle. Turning it off drops a pending one. Game
// thread.
void SetLinkedMatchEnd(bool on);

// Game thread: a match ended (its results skipped) and waits for a decision.
bool EndedVersusBattlePending();

// Game thread, on a confirmed frame: the versus battle whose match ended, or
// null (also when that battle is no longer the current scene). Taking it
// clears it and, when it returns the battle, reads the match's result from
// `battleData` (SfvAdapter::BattleData) as DecidedMatchResult.
uint8_t* TakeEndedVersusBattle(const uint8_t* battleData);

// Game thread: the result read when the last match end was taken; empty
// before. It outlasts the battle (the room hears it after the battle has
// left) until the next battle begins.
std::optional<MatchResult> DecidedMatchResult();

// Game thread: a new battle: drops a pending match end and the decided result.
void ForgetEndedVersusBattle();

// Game thread, between two session frames: restarts that battle with the
// same fighters and stage.
void Rematch(uint8_t* versusBattleState);

// Game thread: leaves the battle for the main menu, as the results menu's
// "Exit" does (a scene change the manager carries out on its next tick).
// False when the scene manager is not known yet.
bool LeaveBattle();

} // namespace sfve::mod
