#pragma once

// The end of a linked match. After the last round SFV's versus battle shows
// a results menu (rematch, character select, exit...) that each player
// answers on their own copy; two linked copies could take different ways
// out. While a linked session runs, the mod keeps that menu from opening
// and decides for both copies at the same session frame: the match ends
// inside the battle flow's lockstep stretch (outside the fight frames only
// run once both players' inputs are confirmed), so both copies reach it on
// the same frame and nothing before it is ever rolled back.
//
// For now the decision is always a rematch: the game's own restart (what
// "Continue" does), with the same fighters and stage, in the same battle
// state.

#include <stdint.h>

namespace sfve::mod {

bool InstallMatchEnd(uintptr_t imageBase);

// While on, a versus battle's results menu does not open; the match end is
// kept for EndedVersusBattle. Any thread.
void SetLinkedMatchEnd(bool on);

// Game thread: the versus battle whose match ended (its results skipped)
// and is waiting for a decision, or null. Taking it clears it.
uint8_t* TakeEndedVersusBattle();

// Game thread, between two session frames: restarts that battle with the
// same fighters and stage.
void Rematch(uint8_t* versusBattleState);

} // namespace sfve::mod
