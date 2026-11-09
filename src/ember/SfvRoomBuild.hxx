#pragma once

// The build a room's members must share (EMBER_LIBRARY.md sections 24 and
// 41): the pick build (picks::BuildId: the game build, the mod's match
// version and the pick space's fingerprint), "/layout-", and the state
// layout id (mod::LayoutRulesId), as separate parts so each can move on its
// own. Two
// copies whose rollback rules differ then never meet in a room, where their
// first checkpoint would desync.
//
// SFV's room host has no game code and cannot work out the layout id: it
// takes it from the build the game exported with its pick space
// (data/pick-space.json), as it takes the pick space itself.
//
// No game code: the room host builds it on Linux with g++ alone (C++17).

#include <string>

namespace sfve::room {

std::string RoomBuildId(const std::string& pickBuild, const std::string& layoutId);

// The layout id a RoomBuildId ends with: the 16 lowercase hex digits after
// its "/layout-", else empty (a build from before the layout id).
std::string LayoutIdOf(const std::string& roomBuild);

} // namespace sfve::room
