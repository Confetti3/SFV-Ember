#pragma once

// The player's pick for the next Ready: the fighter, costume and colour, and
// the stage it offers when it plays P1, as ids of SFV's catalog
// (SfvCatalog). One pick, owned here: Ember's shell edits it on its pick
// screen and table page (SfvShell), and the pipe's test command `pick` sets it
// (RoomCommands); both Ready with it.
//
// Until anyone picks, it is the catalog's first fighter in costume 0 and
// colour 0, on its first stage; the game's own checks judge the costume and
// colour as the battle launches. Any thread.

#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <string>

namespace sfve::adapter {

struct PlayerPick {
	sf4e::selection::Pick pick;
	int stage = 0;
};

PlayerPick CurrentPick();
void SetPick(const PlayerPick& pick);

// The pick in a room command (Ready, the room's pick updates), as the wire
// carries it.
void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command);

// "RYU costume 0 colour 1 stage S25", by the game's codes, for the status
// and the logs.
std::string PickText(const PlayerPick& pick);

} // namespace sfve::adapter
