#pragma once

// Under Wine and Proton, memory the way Windows gives it (mod/AddressRanges.hxx).
//
// Windows x64 randomizes where memory goes, and a game's heap lands above
// 1 TB; Wine places it bottom up from the first free address, so the same
// game's heap lies at a few hundred MB. A state hash zeroes the words that
// are addresses, and decides that by value: above 1 TB is an address, below
// is a number. Two Proton copies parted on their heap pointers (a false
// desync at the first checkpoint, 2026-11-16), and a Proton copy against a
// Windows one would too.
//
// So under Wine, from the exe's entry point on (before any of the game's
// code), every allocation made at no fixed address is asked for top down,
// which Wine places just below the end of user space: above 1 TB, as on
// Windows. What was made before that stays low, and its reservations (the
// heaps then made and the loaded images) become the start ranges, taken as
// addresses by every rule. On Windows nothing is hooked and the start ranges
// stay empty.

#include <stdint.h>

namespace sfve::mod {

// Whether this process runs under Wine (ntdll exports wine_get_version).
bool RunningUnderWine();

// Whether an allocation asked of NtAllocateVirtualMemory should go top down:
// one for this process at no fixed address and with no zero-bits limit
// (an allocation that must lie low says so with zero bits). Pure.
bool TopDownFits(bool thisProcess, bool fixedAddress, uint64_t zeroBits, uint32_t allocationType);

// Under Wine: hooks NtAllocateVirtualMemory to ask top down (TopDownFits),
// sets the start ranges, and logs both ("wine memory:"). Nothing elsewhere.
// From SfveStart, before the game's code runs; false when the hook could not
// go in (the low-heap rule then still serves).
bool InstallWineMemory();

} // namespace sfve::mod
