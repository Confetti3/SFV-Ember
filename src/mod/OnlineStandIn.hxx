#pragma once

// SFV starts with Steam as its online subsystem. When steam_api cannot reach
// a signed-in Steam (Steam's HKCU\Software\Valve\Steam\ActiveProcess record
// names no process or no user, as after some Steam restarts), the subsystem
// never comes up and the game's GetOnlineSubsystem (rva 0x73BEC0) returns
// null. Its eight callers do not test for that: LoggedIn (rva 0x742A20)
// reads through it at +0x14 and the game stops with a fatal error.
//
// SFV Ember keeps the game offline, so a missing online subsystem changes
// nothing a player can do. With this hook, a null subsystem is replaced by a
// stand-in whose interface getters answer "none": the callers' own empty
// paths (offline, not logged in) run instead, and the game starts whatever
// Steam's record says. A working subsystem is passed through untouched.
//
// The callers use three of its vtable slots, each with a 16-byte result
// returned through a pointer (EMBER_LIBRARY.md section 47): +0x08 and +0x50,
// interface getters (a TSharedPtr: object and reference count), and +0xE8,
// an FString (data, count and capacity). The stand-in clears that result,
// which reads as a null pointer or an empty string. Every other slot returns
// 0 and writes nothing.

#include <cstdint>

namespace sfve::mod {

bool InstallOnlineStandIn(uintptr_t imageBase);

// How many times the game was handed the stand-in (0 with Steam reachable).
uint32_t OnlineStandInUses();

} // namespace sfve::mod
