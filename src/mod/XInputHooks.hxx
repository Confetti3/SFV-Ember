#pragma once

// The mod's XInput hooks: XInputGetState, XInputGetStateEx,
// XInputGetCapabilities and XInputSetState in every system XInput library,
// however the game reaches XInput (the exe imports xinput1_3, the engine also
// loads xinput1_4). A slot answers from, in order, a virtual test pad
// (mod/VirtualPads), the controller layer's pad in a slot no real XInput pad
// holds (mod/PadLayer; EMBER_LIBRARY.md section 49), then XInput itself.
// Every read the game makes is recorded (LastPadState) and passes the pad
// filter (the overlay's) before the game sees it.

#include "mod/GameHooks.hxx"

#include <stdint.h>

#include <optional>

namespace sfve::mod {

// XInput's slots.
constexpr int kPadSlots = 4;

// Hooks every system XInput library's calls as one set (CommitHookSet).
// Empty when the set is in, including on a PC with no XInput library, where
// the set has no hooks; otherwise the hook that failed, and none is. Once per
// process: a second call answers as the first (the tests that read through
// the hooks each ask for them).
std::optional<HookFailure> InstallXInputHooks();

// Sees every pad state the game reads (a slot's XINPUT_STATE, whoever
// answered it) before the game does, and may change it (the overlay keeps a
// pad's buttons from the game while it is open). On the thread polling the
// pads; null removes it.
using PadFilter = void (*)(unsigned long user, void* xinputState);
void SetPadFilter(PadFilter filter);

// A slot as the game last read it through XInput, before any pad filter:
// whether a pad answered, its XINPUT_GAMEPAD_* buttons, and the directions
// its left stick leans to (the D-pad's bits: 1 up, 2 down, 4 left, 8 right).
// Not connected until the game has read the slot. Any thread.
struct PadSeen {
	bool connected = false;
	uint16_t buttons = 0;
	uint8_t stick = 0;
	// The triggers held past XInput's own threshold (30 of 255), which the
	// buttons do not carry (a replay's RT step and LT).
	bool leftTrigger = false, rightTrigger = false;
};
PadSeen LastPadState(int slot);

// The game's reads of a slot, one per read however many XInput libraries it
// passes through, and the calls one hooked library made into another (passed
// straight through, never counted as reads).
uint64_t PadReads(int slot);
uint64_t NestedPadReads();

// UE4 looks for newly connected pads only when Windows reports a device
// change (the game reads a slot that was not connected only after
// WM_DEVICECHANGE, rva 0x11D7C20), so a pad that appears later, virtual or
// the controller layer's, has to say so. Posts it to every window of this
// process; the number of the engine's own (class UnrealWindow) among them,
// 0 before the game's window exists (the splash screen's ignores it).
int AnnounceDeviceChange();

// The calling thread's XInput calls go straight to the library from now on:
// no virtual pad, no layer slot, no record and no filter. For the controller
// layer's thread (mod/SdlPads), whose own XInput calls are never the game's.
void PassPadReadsThrough();

// Whether XInput itself has a pad in a slot, read beneath the hooks: a real
// XInput pad, never a virtual one or the controller layer's. False before the
// hooks are in. Any thread; reads the hardware, so not on every frame.
bool PhysicalPadConnected(int slot);

// The pipe's padReads, padSeen and padNested statuses.
void RegisterPadReadStatus();

} // namespace sfve::mod
