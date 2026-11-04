#pragma once

// Test only: holds back this process's outgoing UDP datagrams by a fixed
// delay, so two copies on one PC see a real connection's latency (and GGPO
// has to predict and roll back). Off unless -sfve_lag=<ms> is given.

#include <winsock2.h>

namespace sfve::mod {

using SendToFn = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);

void StartLagSim(int delayMs, SendToFn send);
// Queues the datagram and returns true when lag is on; the caller then
// reports it as sent.
bool LagSimQueue(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen);

} // namespace sfve::mod
