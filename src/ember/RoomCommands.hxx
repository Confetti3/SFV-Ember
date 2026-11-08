#pragma once

// Test commands for Ember's rooms on the control pipe, beside Ember's shell
// (SfvShell): tools/runroom.ps1 plays rooms through them. Each submits the
// runtime's own command, as the shell's buttons do
// (NetplayFacade::SubmitRuntimeCommand), tagged with the session it was read
// from; the runtime judges it exactly as it judges a press. `pick` sets the
// one pick the shell edits too (SfvPick).
//
//   room [rounds] [seconds]   host a private room; its tables play that many
//                             rounds (1, 3, 5 or 7) of that many seconds (30,
//                             60, 99 or 300), and each game goes back to the
//                             room (SFV has no rematch prompt yet)
//   invite                    the room's invitation, to join with
//   join <invitation>         join a room
//   sit [table] [seat]        take a seat (0 P1, 1 P2) at a table, or with
//                             seat -1 its queue (default: table 0's queue)
//   pick CODE[:costume[:colour]] [STAGE]   this player's pick for the next
//                             Ready, by the game's codes; the stage counts
//                             for the host (P1)
//   ready                     Ready with the pick
//   delay N                   the input delay this player readies with
//   rules <rounds> <seconds>  the table rules saved in Settings, which a room
//                             this player opens plays (an organised match's
//                             room too); at the main menu only
//   leave                     leave the room
//
// Status field "room": the room and match phase, this player's seat, the
// members, whether Ready is open, the pick and the latest notice.

namespace sfve::adapter {

void RegisterRoomCommands();

} // namespace sfve::adapter
