#pragma once

// The controller layer (docs/design/EMBER_LIBRARY.md section 49): pads the
// game cannot read through XInput (a DualShock 4 or DualSense, a Switch Pro
// pad, an arcade stick in its PlayStation or DirectInput mode) given to the
// game as XInput pads.
//
// SFV reads pads through XInput alone: XInputGetState and XInputSetState
// imported from xinput1_3, four slots, no DirectInput, no HID, and raw input
// for the mouse only. A player who starts SFV Ember outside Steam has no
// Steam Input to make such a pad an XInput one, so the game never sees it.
// The layer reads those pads with SDL (mod/SdlPads, on a thread of its own,
// through the library's common/SdlPadSource) and answers the game's XInput
// reads of a slot no real XInput pad holds with the pad's state. A real
// XInput pad still reads straight from XInput, so the layer leaves an Xbox
// pad or a stick in its XInput mode exactly as it was. A virtual test pad
// (mod/VirtualPads) wins over both.
//
// Which pad takes which slot is the library's model (common/GamepadSlots:
// Classify and Slots, with the slots the game reads without the layer as
// occupied). This header is SFV's rule for the pads the game reads itself,
// and the slots' states as the game reads them; PadLayer.cxx keeps the states
// the layer thread publishes for the XInput hooks (mod/XInputHooks) to answer
// from. It links no SDL, so the tests link it.

#include "mod/XInputHooks.hxx"

#include "common/GamepadSlots.hxx"
#include "common/XInputMapping.hxx"

#include <windows.h>

#include <Xinput.h>

#include <stdint.h>

#include <array>
#include <cctype>
#include <string_view>

namespace sfve::mod::layer {

// SDL's joystick instance id: never 0, and a pad plugged in again gets a new one.
using PadId = sf4e::gamepads::Id;

// Windows names an XInput device's interface with "IG_" (Microsoft's
// "XInput and DirectInput"), and SDL's own XInput backend names its devices
// "XInput#N". Case does not matter in a device path.
inline bool XInputPath(std::string_view path)
{
	if (path.starts_with("XInput#")) return true;
	for (size_t i = 0; i + 3 <= path.size(); i++) {
		if (std::toupper((unsigned char)path[i]) == 'I' && std::toupper((unsigned char)path[i + 1]) == 'G' && path[i + 2] == '_') {
			return true;
		}
	}
	return false;
}

// SFV's rule for a pad it reads itself (the library's GameReadsPad): an
// XInput device (an Xbox pad, a stick in its XInput mode) or Steam's virtual
// pad. The layer never opens one, so no pad reaches the game twice, and the
// library pairs each Steam virtual pad with the physical pad behind it.
inline bool GameReadsItself(const sf4e::gamepads::Pad& pad)
{
	using sf4e::gamepads::Type;
	return XInputPath(pad.identity.path) || pad.type == Type::Xbox360 || pad.type == Type::XboxOne ||
		sf4e::gamepads::SteamVirtual(pad);
}

// A slot as the game reads it from the layer: its pad (0: the layer does not
// hold it), XINPUT_STATE's packet number and gamepad, and the guide button,
// which only XInputGetStateEx reports (XInput's 0x400).
struct SlotState {
	PadId id = 0;
	uint32_t packet = 0;
	sf4e::gamepads::XInputFields fields;
	bool guide = false;
};

// XInput's packet number changes whenever the state does: a new pad in the
// slot, or any button, trigger or stick.
inline void Advance(SlotState& slot, PadId id, const sf4e::gamepads::XInputFields& fields, bool guide)
{
	if (id != slot.id || fields != slot.fields || guide != slot.guide) slot.packet++;
	slot.id = id;
	slot.fields = id ? fields : sf4e::gamepads::XInputFields{};
	slot.guide = id && guide;
}

// --- The states the layer thread publishes and the XInput hooks read (PadLayer.cxx).

// The layer thread: the slots' states, after each read of its pads.
void Publish(const std::array<SlotState, kPadSlots>& slots);

// The XInput hooks, on whichever thread reads pads: false when the layer
// does not hold the slot, so the call goes on to XInput. `ex` is
// XInputGetStateEx's read, which adds the guide button.
bool FillState(int slot, XINPUT_STATE* state, bool ex);
// A wired gamepad with every button, both triggers, both sticks and both motors.
bool FillCapabilities(int slot, XINPUT_CAPABILITIES* capabilities);
// The game's XInputSetState: the motors are kept for the layer thread to
// apply (RequestedMotors).
bool SetMotors(int slot, const XINPUT_VIBRATION& vibration);

// The motors the game last asked of a slot, for the pad the slot held then.
struct Motors {
	PadId id = 0;
	uint16_t left = 0, right = 0;
	bool operator==(const Motors&) const = default;
};
std::array<Motors, kPadSlots> RequestedMotors();

// Forgets every slot (the layer stops holding any).
void ClearSlots();

} // namespace sfve::mod::layer
