#pragma once

// Test commands on the control pipe for Ember's public rooms (EMBER_LIBRARY.md
// section 24), standing in for the Ember ID, Linked accounts and Public rooms
// screens as the room commands stand in for the room's buttons:
//   ember-id                  make this copy's Ember ID, or read it
//   bridge <origin>           look a service up
//   trust                     trust the service looked up
//   public list               list its public rooms for this build and game
//   public create <rounds> <seconds> <name>
//                             open one (8 seats) and take its creator's ticket;
//                             its tables play those rounds, no set length, no
//                             Instant rematch
//   public join <room id>     take a ticket for a listed room
//   public enter              join with the last create's or ticket's admission
//   ranking standing          read this player's SFV standing (Rankings)
//   ranking board             read SFV's board of classes
//   ranking take-part <country|-> <name>
//                             take part, listed under that flag and name
//   ranking stop              stop taking part
//   catalog-export <path>     write the pick space SFV's room host judges by
// The status field `public` reports the identity, the service, the list and
// the last admission; `ranking` the standing, the board, the last choice, each
// room member's mark and this copy's online record (EMBER_LIBRARY.md section
// 28). Registered with the room commands (-sfve_room=1).

namespace sfve::adapter {

void RegisterPublicCommands();

} // namespace sfve::adapter
