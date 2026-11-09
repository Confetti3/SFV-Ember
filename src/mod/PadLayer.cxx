#include "mod/PadLayer.hxx"

#include <windows.h>

#include <Xinput.h>

#include <mutex>

namespace sfve::mod::layer {

namespace {

// The game reads pads on its own thread and the overlay's hooks on theirs;
// the layer thread publishes about once a millisecond. A read copies a slot
// under the lock and nothing waits on anything else while it is held.
std::mutex s_mutex;
std::array<SlotState, kSlots> s_slots;
std::array<Motors, kSlots> s_motors;

constexpr WORD kGuide = 0x0400; // XInputGetStateEx's guide button

} // namespace

const char* RouteName(Route route)
{
	switch (route) {
	case Route::Slotted: return "slotted";
	case Route::GameReads: return "game reads it";
	case Route::SteamTwin: return "Steam twin";
	case Route::Unmapped: return "unmapped";
	case Route::Waiting: return "waiting";
	}
	return "?";
}

void Publish(const std::array<SlotState, kSlots>& slots)
{
	std::lock_guard lock(s_mutex);
	s_slots = slots;
	// A slot that changed pads keeps no motors asked of the pad before.
	for (int slot = 0; slot < kSlots; slot++) {
		if (s_motors[slot].id != slots[slot].id) s_motors[slot] = {};
	}
}

unsigned HeldSlots()
{
	std::lock_guard lock(s_mutex);
	unsigned mask = 0;
	for (int slot = 0; slot < kSlots; slot++) {
		if (s_slots[slot].id) mask |= 1u << slot;
	}
	return mask;
}

bool FillState(int slot, void* state, bool ex)
{
	if (slot < 0 || slot >= kSlots || !state) return false;
	SlotState held;
	{
		std::lock_guard lock(s_mutex);
		held = s_slots[slot];
	}
	if (!held.id) return false;
	auto* out = static_cast<XINPUT_STATE*>(state);
	const sf4e::gamepads::XInputFields& f = held.fields;
	*out = {};
	out->dwPacketNumber = held.packet;
	out->Gamepad.wButtons = static_cast<WORD>(f.buttons | (ex && held.guide ? kGuide : 0));
	out->Gamepad.bLeftTrigger = f.leftTrigger;
	out->Gamepad.bRightTrigger = f.rightTrigger;
	out->Gamepad.sThumbLX = f.leftX;
	out->Gamepad.sThumbLY = f.leftY;
	out->Gamepad.sThumbRX = f.rightX;
	out->Gamepad.sThumbRY = f.rightY;
	return true;
}

bool FillCapabilities(int slot, void* capabilities)
{
	if (slot < 0 || slot >= kSlots || !capabilities) return false;
	{
		std::lock_guard lock(s_mutex);
		if (!s_slots[slot].id) return false;
	}
	auto* out = static_cast<XINPUT_CAPABILITIES*>(capabilities);
	*out = {};
	out->Type = XINPUT_DEVTYPE_GAMEPAD;
	out->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
	out->Gamepad = {0xF3FF, 255, 255, 32767, 32767, 32767, 32767};
	out->Vibration = {65535, 65535};
	return true;
}

bool SetMotors(int slot, uint16_t left, uint16_t right)
{
	if (slot < 0 || slot >= kSlots) return false;
	std::lock_guard lock(s_mutex);
	if (!s_slots[slot].id) return false;
	s_motors[slot] = {s_slots[slot].id, left, right};
	return true;
}

std::array<Motors, kSlots> RequestedMotors()
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
