#pragma once

// Random on the fighter board: the player's pick is a fighter rolled for each
// game, in the look last used on that fighter. The runtime owns the roll
// (room::RandomFighterPick, in the library); this is SFV's side of it, modelled
// on SF4 Ember's FollowRandomFighter (the library's
// sf4e__OverlayRandomFighter.cxx): tell the runtime every frame whether the
// pick is Random, take each new roll it publishes once, and say when Training
// played the rolled fighter.
//
// "The look last used on a fighter" is SfvPick's per-fighter memory (session
// only) and its Random flag; these functions read and write the stored pick
// only through SfvPick's own functions, so the pick stays fitted as the rest
// of SFV Ember keeps it (SfvPick.hxx).
//
// The dummy's pick screen has no Random card: nothing here touches the dummy.
//
// Stream C fills SfvRandomFighter.cxx (and SfvPick's memory and flag); this
// header is final.

#include "sf4e/sf4e__NetplayFacade.hxx"

namespace sfve::adapter {

// Any thread, every frame the pick screen or the runtime's shell is up: tells
// the runtime whether the player's pick is Random
// (NetplayFacade::NoteRandomFighter, with the stored pick and the per-fighter
// remembered looks as the wire carries them), so it rolls the next game's
// fighter and sends it with every Ready.
void NoteRandomFighter(bool random);

// Game thread, every frame: when `snapshot` carries a roll this function has
// not taken (snapshot.randomRoll differs from the last taken, and
// snapshot.randomFighter is a fighter of the catalog), the stored pick
// becomes that fighter in its remembered look, fitted; one log line
// "Fighter select: Random rolled <CODE>". Does nothing while the pick is not
// Random.
void FollowRandomFighter(const sf4e::NetplayFacade::RuntimeSnapshot& snapshot);

// Training from Home played the rolled fighter: the next roll comes at once
// (NetplayFacade::NoteRandomFighterPlayed).
void RandomFighterPlayed();

} // namespace sfve::adapter
