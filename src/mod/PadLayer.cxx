#include "mod/PadLayer.hxx"

#include <mutex>

namespace sfve::mod::layer {

namespace {

// The game reads pads on its own thread and the overlay's hooks on theirs;
// the layer thread publishes about once a millisecond. A read copies a slot
// under the lock and nothing waits on anything else while it is held.
std::mutex s_mutex;
std::array<SlotState, kPadSlots> s_slots;
std::array<Motors, kPadSlots> s_motors;

constexpr WORD kGuide = 0x0400; // XInputGetStateEx's guide button

bool InRange(int slot)
{
	return slot >= 0 && slot < kPadSlots;
}

} // namespace

void Publish(const std::array<SlotState, kPadSlots>& slots)
{
	std::lock_guard lock(s_mutex);
	s_slots = slots;
	// A slot that changed pads keeps no motors asked of the pad before.
	for (int slot = 0; slot < kPadSlots; slot++) {
		if (s_motors[slot].id != slots[slot].id) s_motors[slot] = {};
	}
}

bool FillState(int slot, XINPUT_STATE* state, bool ex)
{
	if (!InRange(slot) || !state) return false;
	SlotState held;
	{
		std::lock_guard lock(s_mutex);
		held = s_slots[slot];
	}
	if (!held.id) return false;
	const sf4e::gamepads::XInputFields& f = held.fields;
	*state = {};
	state->dwPacketNumber = held.packet;
	state->Gamepad.wButtons = static_cast<WORD>(f.buttons | (ex && held.guide ? kGuide : 0));
	state->Gamepad.bLeftTrigger = f.leftTrigger;
	state->Gamepad.bRightTrigger = f.rightTrigger;
	state->Gamepad.sThumbLX = f.leftX;
	state->Gamepad.sThumbLY = f.leftY;
	state->Gamepad.sThumbRX = f.rightX;
	state->Gamepad.sThumbRY = f.rightY;
	return true;
}

bool FillCapabilities(int slot, XINPUT_CAPABILITIES* capabilities)
{
	if (!InRange(slot) || !capabilities) return false;
	{
		std::lock_guard lock(s_mutex);
		if (!s_slots[slot].id) return false;
	}
	*capabilities = {};
	capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
	capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
	capabilities->Gamepad = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
	capabilities->Vibration = {65535, 65535};
	return true;
}

bool SetMotors(int slot, const XINPUT_VIBRATION& vibration)
{
	if (!InRange(slot)) return false;
	std::lock_guard lock(s_mutex);
	if (!s_slots[slot].id) return false;
	s_motors[slot] = {s_slots[slot].id, vibration.wLeftMotorSpeed, vibration.wRightMotorSpeed};
	return true;
}

std::array<Motors, kPadSlots> RequestedMotors()
{
	std::lock_guard lock(s_mutex);
	return s_motors;
}

void ClearSlots()
{
	std::lock_guard lock(s_mutex);
	s_slots = {};
	s_motors = {};
}

} // namespace sfve::mod::layer
