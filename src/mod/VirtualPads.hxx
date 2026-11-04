#pragma once

// Scripted XInput pads for test automation.
//
// The mod hooks XInputGetState in every system XInput library, so however
// the game reaches XInput (the exe imports xinput1_3, the engine also loads
// xinput1_4) a slot switched to virtual reports a connected pad whose buttons follow a
// queue of timed steps ("A for 100 ms, nothing for 200 ms"), whatever the
// physical pad does. Several game copies on one PC each drive their own
// virtual pad, so menus can be walked without anyone at the keyboard.

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

// Hooks XInput; until then virtual slots have no effect.
bool InstallVirtualPads();

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


// Button names: A B X Y UP DOWN LEFT RIGHT START BACK LB RB LS RS LT RT.
bool ParseButtons(const std::string& text, PadStep* out);

// Control pipe commands:
//   virtual <slot> on|off     pad <slot> <steps>     padclear <slot>
void RegisterPadCommands();

} // namespace sfve::mod
