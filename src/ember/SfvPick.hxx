#pragma once

// The player's pick for the next Ready: the fighter, costume and colour, and
// the stage it offers when it plays P1, as ids of SFV's catalog
// (SfvCatalog). One pick, owned here: Ember's shell edits it on its pick
// screen and table page (SfvShell), and the pipe's test command `pick` sets it
// (RoomCommands); both Ready with it.
//
// It is always a pick the catalog takes: what is stored and read is fitted
// to the catalog's rules (its NormalizePick, the same on every copy), so a
// costume a fighter lacks becomes its first costume and a colour the costume
// lacks its first colour. It is this copy's player's own pick, so it is also
// fitted to what they can ready (SfvCatalog's FitOwnPick: a fighter,
// costume or colour the game does not let them pick, or this copy has not
// installed, becomes their first one they can). Until anyone picks, it is
// the first fighter they can play in their first costume and colour, on the
// first stage. Any thread.

#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <string>

namespace sfve::adapter {

struct PlayerPick {
	sf4e::selection::Pick pick;
	int stage = 0;
};

PlayerPick CurrentPick();
// Stores the pick, fitted; returns what was stored.
PlayerPick SetPick(const PlayerPick& pick);

// Training's other side (Training select's Dummy row, EMBER_LIBRARY.md
// section 27): a pick of its own, fitted the same way, as SF4 Ember keeps its
// Training dummy apart from the player's pick. Its stage is unused (Training
// loads on the player's). Until the player picks a dummy it is their own
// fighter, costume and colour. Neither is saved between runs: the player's
// pick is not saved either.
PlayerPick CurrentDummy();
PlayerPick SetDummy(const PlayerPick& dummy);

// The pick in a room command (Ready, the room's pick updates), as the wire
// carries it.
void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command);

// "RYU costume 0 colour 1 stage S25", by the game's codes, for the status
// and the logs.
std::string PickText(const PlayerPick& pick);

} // namespace sfve::adapter
