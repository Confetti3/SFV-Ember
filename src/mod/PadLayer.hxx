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
// The layer reads those pads with SDL (mod/SdlPads, on a thread of its own)
// and answers the game's XInput reads of a slot no real XInput pad holds with
// the pad's state. A real XInput pad still reads straight from XInput, so the
// layer leaves an Xbox pad or a stick in its XInput mode exactly as it was.
// A virtual test pad (mod/VirtualPads) wins over both.
//
// This header is the layer's model without SDL: which pad SDL lists goes to
// which slot (Classify, Plan) and the slots' states as the game reads them
// (SlotState). PadLayer.cxx keeps the states the layer thread publishes for
// the XInput hooks to answer from; it links no SDL, so the tests link it.

#include "common/GamepadIdentity.hxx"
#include "common/XInputMapping.hxx"

#include <stdint.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod::layer {

constexpr int kSlots = 4;

// SDL's joystick instance id: never 0, and a pad plugged in again gets a new one.
using PadId = uint32_t;

// What the layer does with a pad SDL lists.
enum class Route {
	Slotted,   // the layer gives it an XInput slot
	GameReads, // an XInput device: the game reads it through XInput itself
	SteamTwin, // the physical pad behind Steam's virtual XInput pad, which the game reads
	Unmapped,  // SDL has no gamepad mapping for it, so its buttons have no names
	Waiting,   // every slot is taken
};

const char* RouteName(Route route);

// A pad as SDL lists it, and what the layer made of it.
struct Pad {
	PadId id = 0;
	sf4e::gamepads::Identity identity;
	uint16_t vid = 0, pid = 0;
	// SDL_GamepadType, as SDL names the pad's type; Steam's virtual pad
	// reports the type of the pad behind it.
	int sdlType = 0;
	// SDL calls it an Xbox 360 or Xbox One pad.
	bool xboxType = false;
	// SDL maps its buttons to a gamepad's (SDL_IsGamepad).
	bool gamepad = false;
	Route route = Route::Waiting;
	int slot = -1;
};

// Windows names an XInput device's interface with "IG_" (Microsoft's
// "XInput and DirectInput"), and SDL's own XInput backend names its devices
// "XInput#N". Case does not matter in a device path.
inline bool XInputPath(std::string_view path)
{
	if (path.size() >= 7 && path.substr(0, 7) == "XInput#") return true;
	for (size_t i = 0; i + 3 <= path.size(); i++) {
		if (std::toupper((unsigned char)path[i]) == 'I' && std::toupper((unsigned char)path[i + 1]) == 'G' && path[i + 2] == '_') {
			return true;
		}
	}
	return false;
}

// Steam's virtual pad, by the vendor and product its GUID keeps (SDL may
// report the physical pad's for the device itself).
inline bool SteamVirtual(const Pad& pad)
{
	const sf4e::gamepads::GuidIds ids = sf4e::gamepads::GuidVendorProduct(pad.identity.guid);
	return ids.vendor == 0x28DE && ids.product == 0x11FF;
}

// A pad the game already reads through XInput: an XInput device (an Xbox
// pad, a stick in its XInput mode) or Steam's virtual pad. The layer never
// opens one, so no pad reaches the game twice.
inline bool GameReadsItself(const Pad& pad)
{
	return XInputPath(pad.identity.path) || pad.xboxType || SteamVirtual(pad);
}

// Sets each pad's route, all but Slotted and Waiting final; a pad the layer
// may slot is Waiting until a Plan places it. Each Steam virtual pad pairs
// with one physical pad of its type, which Steam already gives the game as
// that virtual pad: the layer leaves the twin alone (SF4 Ember's rule,
// GamepadSlots.hxx).
inline void Classify(std::vector<Pad>& pads)
{
	for (Pad& pad : pads) {
		pad.slot = -1;
		pad.route = GameReadsItself(pad) ? Route::GameReads : pad.gamepad ? Route::Waiting : Route::Unmapped;
	}
	for (const Pad& steam : pads) {
		if (!SteamVirtual(steam)) continue;
		for (Pad& physical : pads) {
			if (physical.route != Route::Waiting && physical.route != Route::Unmapped) continue;
			if (physical.sdlType != steam.sdlType) continue;
			physical.route = Route::SteamTwin;
			break;
		}
	}
}

// Which Waiting pad holds which slot. A slot the game reads without the
// layer (a real XInput pad's, or a virtual test pad's) is occupied and never
// given; a pad keeps its slot while the slot stays free, and a pad plugged in
// again goes back to the slot it last held when that is free, as XInput's own
// slots do. The others take the lowest free slot no other pad last held, then
// any free slot, in the order SDL listed them (their ids rise).
class Plan {
public:
	// occupied: bit i when slot i answers without the layer. Places the
	// Waiting pads (route Slotted and their slot; Waiting stays for those
	// without one) and says whether any slot's pad changed.
	bool Update(std::vector<Pad>& pads, unsigned occupied)
	{
		std::array<PadId, kSlots> next{};
		const auto open = [&](int slot) { return !next[slot] && !(occupied >> slot & 1u); };
		const auto place = [&](Pad& pad, int slot) {
			next[slot] = pad.id;
			pad.slot = slot;
			pad.route = Route::Slotted;
			for (auto& held : held_) {
				if (held && sf4e::gamepads::Same(*held, pad.identity)) held.reset();
			}
			held_[slot] = pad.identity;
		};
		for (int slot = 0; slot < kSlots; slot++) {
			if (!current_[slot] || !open(slot)) continue;
			for (Pad& pad : pads) {
				if (pad.route == Route::Waiting && pad.id == current_[slot]) {
					place(pad, slot);
					break;
				}
			}
		}
		std::vector<Pad*> waiting;
		for (Pad& pad : pads) {
			if (pad.route == Route::Waiting) waiting.push_back(&pad);
		}
		std::stable_sort(waiting.begin(), waiting.end(), [](const Pad* a, const Pad* b) { return a->id < b->id; });
		for (Pad* pad : waiting) {
			int slot = -1;
			for (int s = 0; s < kSlots && slot < 0; s++) {
				if (open(s) && held_[s] && sf4e::gamepads::Same(*held_[s], pad->identity)) slot = s;
			}
			for (int s = 0; s < kSlots && slot < 0; s++) {
				if (open(s) && !held_[s]) slot = s;
			}
			for (int s = 0; s < kSlots && slot < 0; s++) {
				if (open(s)) slot = s;
			}
			if (slot >= 0) place(*pad, slot);
		}
		const bool changed = next != current_;
		current_ = next;
		return changed;
	}

	// The pad in a slot; 0 for none.
	PadId InSlot(int slot) const { return slot >= 0 && slot < kSlots ? current_[slot] : 0; }
	// Bit i when the layer holds slot i.
	unsigned Held() const
	{
		unsigned mask = 0;
		for (int slot = 0; slot < kSlots; slot++) {
			if (current_[slot]) mask |= 1u << slot;
		}
		return mask;
	}

private:
	std::array<PadId, kSlots> current_{};
	// The pad each slot last held, for its return.
	std::array<std::optional<sf4e::gamepads::Identity>, kSlots> held_;
};

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
void Publish(const std::array<SlotState, kSlots>& slots);
// Bit i when the layer holds slot i.
unsigned HeldSlots();

// The XInput hooks (mod/VirtualPads), on whichever thread reads pads: false
// when the layer does not hold the slot, so the read goes on to XInput.
// `state` is an XINPUT_STATE; `ex` is XInputGetStateEx's read, which adds the
// guide button.
bool FillState(int slot, void* state, bool ex);
// `capabilities` is an XINPUT_CAPABILITIES: a wired gamepad with every
// button, both triggers, both sticks and both motors.
bool FillCapabilities(int slot, void* capabilities);
// The game's XInputSetState for a slot the layer holds: the motors are kept
// for the layer thread to apply (Motors).
bool SetMotors(int slot, uint16_t left, uint16_t right);

// The motors the game last asked of a slot, for the pad the slot held then.
struct Motors {
	PadId id = 0;
	uint16_t left = 0, right = 0;
};
std::array<Motors, kSlots> RequestedMotors();

// Forgets every slot (the layer stops holding any).
void ClearSlots();

} // namespace sfve::mod::layer
