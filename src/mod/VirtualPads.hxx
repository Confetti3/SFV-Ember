#pragma once

// Scripted XInput pads for test automation.
//
// A slot switched to virtual reports a connected pad through the XInput
// hooks (mod/XInputHooks), ahead of the controller layer and XInput itself,
// whose buttons follow a queue of timed steps ("A for 100 ms, nothing for
// 200 ms"), whatever the physical pad does. Several game copies on one PC
// each drive their own virtual pad, so menus can be walked without anyone at
// the keyboard.

#include <windows.h>

#include <Xinput.h>

#include <stdint.h>

#include <string>

namespace sfve::mod {

struct PadStep {
	uint16_t buttons = 0;     // XINPUT_GAMEPAD_* bits
	uint8_t leftTrigger = 0;
	uint8_t rightTrigger = 0;
	uint32_t durationMs = 0;
};

// The XInput hooks' read of a virtual slot: its state now. False for a slot
// that is not virtual.
bool FillVirtualPad(int slot, XINPUT_STATE* state);

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
