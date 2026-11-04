#pragma once

// Keeps Street Fighter V off the network except for loopback and the peers a
// session allows (core/NetPolicy.hxx).
//
// Installed before any game code runs (the proxy starts the mod from the
// exe's entry point). Every route a Windows program has out of the process is
// covered: Winsock connect/send/lookup (including ConnectEx handed out by
// WSAIoctl), the DNS client API, WinHTTP and WinINet. A refused call fails the
// way an unreachable network does, so the game takes its own offline paths;
// every refusal is logged.
//
// If the guard cannot be installed the mod ends the process before the game
// starts: the game never runs unguarded.

#include "core/NetPolicy.hxx"

#include <stdint.h>

namespace sfve::mod {

net::NetPolicy& GuardPolicy();

// Loads the network modules and hooks them. False if any hook on a loaded
// module failed; the caller must then not let the game run.
bool InstallNetGuard();

uint64_t NetGuardRefusals();

// Test only: holds back allowed outgoing datagrams (LagSim.hxx).
void StartGuardedLagSim(int delayMs);

} // namespace sfve::mod
