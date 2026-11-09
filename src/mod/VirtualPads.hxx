#pragma once

// Scripted XInput pads for test automation.
//
// The mod hooks XInputGetState in every system XInput library, so however
// the game reaches XInput (the exe imports xinput1_3, the engine also loads
// xinput1_4) a slot switched to virtual reports a connected pad whose buttons follow a
// queue of timed steps ("A for 100 ms, nothing for 200 ms"), whatever the
// physical pad does. Several game copies on one PC each drive their own
// virtual pad, so menus can be walked without anyone at the keyboard.

#include "mod/GameHooks.hxx"

#include <stdint.h>

#include <string>

namespace sfve::mod {

constexpr int kPadSlots = 4;

struct PadStep {
	uint16_t buttons = 0;     // XINPUT_GAMEPAD_* bits
	uint8_t leftTrigger = 0;
	uint8_t rightTrigger = 0;
	uint32_t durationMs = 0;
};

// Hooks XInput, every system library's reads as one set (CommitHookSet);
// until then virtual slots have no effect. Empty when the set is in,
// including on a PC with no XInput library, where the set has no hooks;
// otherwise the hook that failed, and none is.
std::optional<HookFailure> InstallVirtualPads();

// Sees every pad state the game reads (a slot's XINPUT_STATE, physical or
// virtual) before the game does, and may change it (the overlay keeps a
// pad's buttons from the game while it is open). On the thread polling the
// pads; null removes it.
using PadFilter = void (*)(unsigned long user, void* xinputState);
void SetPadFilter(PadFilter filter);
void SetVirtualPad(int slot, bool enabled);
bool IsVirtualPad(int slot);
// Appends steps; a step list is "A+START:100 -:200 DOWN:80" (names below,
// '-' = nothing held). False on a parse error (nothing queued).
bool QueuePadSteps(int slot, const std::string& steps, std::string* error);
void ClearPadSteps(int slot);
size_t PendingPadSteps(int slot);


// A slot as the game last read it through XInput (physical or virtual),
// before any pad filter: whether a pad answered, its XINPUT_GAMEPAD_*
// buttons, and the directions its left stick leans to (the D-pad's bits:
// 1 up, 2 down, 4 left, 8 right). Not connected until the game has read the
// slot. Any thread.
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

// Button names: A B X Y UP DOWN LEFT RIGHT START BACK LB RB LS RS LT RT.
bool ParseButtons(const std::string& text, PadStep* out);

// Control pipe commands:
//   virtual <slot> on|off     pad <slot> <steps>     padclear <slot>
void RegisterPadCommands();

} // namespace sfve::mod
