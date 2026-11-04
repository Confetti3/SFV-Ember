#pragma once

// Diagnostics: which game code writes a given state field.
//
// A hardware write breakpoint (debug register 0) on the game thread; every
// write is caught by a vectored exception handler that records the writing
// instruction's address (as an RVA) and lets the game go on. Used to find the
// code behind a region that the synctest reports, then read in IDA.

#include <stdint.h>

#include <string>

namespace sfve::mod {

// Arms a watch on `length` (1, 2, 4 or 8) bytes at `address` for the thread
// `gameThreadId`. Replaces any previous watch.
bool ArmWriteWatch(uint32_t gameThreadId, uintptr_t address, int length);
void DisarmWriteWatch(uint32_t gameThreadId);
// "rva:count rva:count ...", most frequent first.
std::string WriteWatchHits();

} // namespace sfve::mod
