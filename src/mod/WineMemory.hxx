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
// code), every allocation made at no fixed address is asked for top down
// under an 8 TB limit (kTopDownZeroBits), which Wine places just below that
// limit: above 1 TB and below 8 TB, as on Windows. What was made before that
// stays low, and its reservations (the
// heaps then made and the loaded images) become the start ranges, taken as
// addresses by every rule. On Windows nothing is hooked and the start ranges
// stay empty.

#include "mod/AddressRanges.hxx"

#include <stdint.h>

namespace sfve::mod {

// Whether this process runs under Wine (ntdll exports wine_get_version).
bool RunningUnderWine();

// Whether an allocation asked of NtAllocateVirtualMemory should go top down:
// one for this process at no fixed address and with no zero-bits limit
// (an allocation that must lie low says so with zero bits). Pure.
bool TopDownFits(bool thisProcess, bool fixedAddress, uint64_t zeroBits, uint32_t allocationType);

// Windows 7 era x64 code (XAudio2_7.dll, 2010) cuts pointers to 43 bits, so a
// pointer at or above 8 TB comes back wrong and its owner faults. A game's
// heap on Windows lies above 1 TB and far below 8 TB; top down alone puts it
// at 128 TB. So a top down allocation also passes this zero-bits mask, which
// Wine reads as an upper limit: the result lies in [1 TB, 8 TB).
constexpr uint64_t kWineUserSpace = 1ull << 43;
constexpr uint64_t kTopDownZeroBits = kWineUserSpace - 1;

// Whether an address a top down allocation returned lies where it should.
constexpr bool InTopDownRange(uint64_t address) { return address >= kHighAddresses && address < kWineUserSpace; }

// Under Wine: hooks NtAllocateVirtualMemory to ask top down (TopDownFits),
// sets the start ranges, and logs both ("wine memory:"). Nothing elsewhere.
// From SfveStart, before the game's code runs; false when the hook could not
// go in (the low-heap rule then still serves).
bool InstallWineMemory();

} // namespace sfve::mod
