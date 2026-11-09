#include "mod/SdlPads.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "mod/PadLayer.hxx"
#include "mod/VirtualPads.hxx"

#include <windows.h>

#pragma warning(push, 3)
#include <SDL3/SDL.h>
#pragma warning(pop)

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace sfve::mod {

namespace {

using namespace layer;
using sf4e::gamepads::Button;

// The pipe's view of the layer, rebuilt by its thread whenever it changes.
std::mutex s_statusMutex;
std::string s_status = "{\"state\":\"not started\"}";
std::atomic<bool> s_started{false};

void SetStatus(std::string status)
{
	std::lock_guard lock(s_statusMutex);
	s_status = std::move(status);
}

std::string Text(const char* text)
{
	return text ? text : "";
}

const char* TypeName(int type)
{
	const char* name = SDL_GetGamepadStringForType(static_cast<SDL_GamepadType>(type));
	return name && *name ? name : "unknown";
}

// A pad's buttons, triggers and sticks as SDL's gamepad mapping names them,
// in the library's order (common/XInputMapping.hxx), which MapToXInput turns
// into an XInput pad's.
sf4e::gamepads::State ReadState(SDL_Gamepad* pad)
{
	static constexpr SDL_GamepadButton kButtons[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
		SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
		SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
		SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
		SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
	static_assert(sizeof(kButtons) / sizeof(kButtons[0]) == static_cast<unsigned>(Button::Count), "one SDL button per Button");
	sf4e::gamepads::State state;
	for (unsigned i = 0; i < state.buttons.size(); i++) state.buttons[i] = SDL_GetGamepadButton(pad, kButtons[i]);
	state.leftX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
	state.leftY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
	state.rightX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
	state.rightY = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
	state.leftTrigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
	state.rightTrigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
	return state;
}

// Bit i when slot i is a virtual test pad's (mod/VirtualPads), which wins
// over the layer as it does over XInput.
unsigned VirtualSlots()
{
	unsigned mask = 0;
	for (int slot = 0; slot < kSlots; slot++) {
		if (IsVirtualPad(slot)) mask |= 1u << slot;
	}
	return mask;
}

// Bit i when XInput itself has a pad in slot i, for each slot of `which`.
// XInput gives a pad plugged in later the lowest slot it sees free, and the
// layer's slots look free to it, so a new Xbox pad may take a layer slot:
// the layer moves its pad when it finds one there.
unsigned ProbeRealSlots(unsigned which, unsigned known)
{
	for (int slot = 0; slot < kSlots; slot++) {
		if (!(which >> slot & 1u)) continue;
		if (PhysicalPadConnected(slot)) known |= 1u << slot;
		else known &= ~(1u << slot);
	}
	return known;
}

class LayerThread {
public:
	void Run()
	{
		// Every XInput call SDL or this thread makes goes straight to XInput:
		// none is the game's, and the game's slots the layer answers must
		// never reach SDL as pads of their own.
		PassPadReadsThrough();
		if (!Start()) return;
		Rebuild();
		ULONGLONG nextProbe = GetTickCount64() + kProbeMs;
		for (;;) {
			bool rebuild = false;
			SDL_UpdateGamepads();
			SDL_PumpEvents();
			SDL_Event events[32];
			int count;
			while ((count = SDL_PeepEvents(events, 32, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST)) > 0) {
				for (int i = 0; i < count; i++) {
					switch (events[i].type) {
					case SDL_EVENT_JOYSTICK_ADDED:
					case SDL_EVENT_JOYSTICK_REMOVED:
					case SDL_EVENT_GAMEPAD_ADDED:
					case SDL_EVENT_GAMEPAD_REMOVED:
					case SDL_EVENT_GAMEPAD_REMAPPED: rebuild = true; break;
					default: break;
					}
				}
			}
			const ULONGLONG now = GetTickCount64();
			const unsigned virtualSlots = VirtualSlots();
			if (rebuild) {
				Rebuild();
				nextProbe = now + kProbeMs;
			} else if (virtualSlots != virtual_) {
				Replan(real_, "a virtual pad");
			} else if (now >= nextProbe) {
				// Every slot when a pad waits for one, else the layer's own.
				const bool waiting = std::any_of(pads_.begin(), pads_.end(), [](const Pad& pad) { return pad.route == Route::Waiting; });
				const unsigned real = ProbeRealSlots(waiting ? 0xFu : plan_.Held(), real_);
				if (real != real_) Replan(real, "an XInput pad");
				nextProbe = now + kProbeMs;
			}
			ReadSlots();
			ApplyMotors(now);
			if (announce_ && now >= nextAnnounce_) {
				// Before the game's window exists there is nothing to tell,
				// and the game's first look at the pads comes after it.
				if (AnnounceDeviceChange() > 0) announce_ = false;
				else nextAnnounce_ = now + 250;
			}
			// A held slot is read every millisecond, so the game's read once
			// a frame finds the pad at most about a millisecond old; with no
			// pad held, only plugging one in is waited for.
			Wait(plan_.Held() ? 1 : 16);
		}
	}

private:
	static constexpr ULONGLONG kProbeMs = 1000;

	bool Start()
	{
		// Only what the layer reads: SDL's HIDAPI drivers for PlayStation,
		// Switch and Steam pads, and DirectInput for the rest. SDL's XInput,
		// raw input, Windows.Gaming.Input and GameInput backends and its Xbox
		// HIDAPI driver are off: they read XInput devices, which the game
		// already reads, and the XInput backend would read the layer's own
		// slots back as pads. With them off SDL's DirectInput lists the
		// XInput devices too (by an IG_ path), and the layer leaves them be.
		for (const char* hint : {SDL_HINT_XINPUT_ENABLED, SDL_HINT_JOYSTICK_HIDAPI_XBOX, SDL_HINT_JOYSTICK_RAWINPUT,
				 SDL_HINT_JOYSTICK_WGI, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_JOYSTICK_GAMEINPUT_RAW}) {
			SDL_SetHint(hint, "0");
		}
		for (const char* hint : {SDL_HINT_JOYSTICK_THREAD, SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, SDL_HINT_JOYSTICK_HIDAPI,
				 SDL_HINT_JOYSTICK_HIDAPI_PS4, SDL_HINT_JOYSTICK_HIDAPI_PS5, SDL_HINT_JOYSTICK_HIDAPI_SWITCH,
				 SDL_HINT_JOYSTICK_HIDAPI_STEAM, SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS}) {
			SDL_SetHint(hint, "1");
		}
		if (!SDL_Init(SDL_INIT_GAMEPAD)) {
			const std::string error = Text(SDL_GetError());
			Log("pads: the controller layer could not start SDL (%s); the game reads the pads itself", error.c_str());
			SetStatus("{\"state\":\"failed\",\"error\":" + JsonString(error) + "}");
			SDL_Quit();
			return false;
		}
		// The layer reads states, never SDL's per-press events: only pads
		// coming and going are queued.
		for (const SDL_EventType type : {SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
				 SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_BATTERY_UPDATED,
				 SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_UP,
				 SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
				 SDL_EVENT_GAMEPAD_SENSOR_UPDATE}) {
			SDL_SetEventEnabled(type, false);
		}
		const int version = SDL_GetVersion();
		snprintf(version_, sizeof(version_), "%d.%d.%d", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
			SDL_VERSIONNUM_MICRO(version));
		Log("pads: the controller layer started SDL %s (HIDAPI and DirectInput; SDL's XInput off)", version_);
		timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		return true;
	}

	void Wait(int ms)
	{
		LARGE_INTEGER due;
		due.QuadPart = -10000LL * ms;
		if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer_, INFINITE);
		else Sleep((DWORD)ms);
	}

	// SDL's pads again, after one came or went: what each is, which the
	// layer opens, and which slot each takes.
	void Rebuild()
	{
		int count = 0;
		SDL_JoystickID* ids = SDL_GetJoysticks(&count);
		if (!ids) {
			Log("pads: SDL could not list the pads (%s)", SDL_GetError());
			return;
		}
		std::vector<Pad> pads;
		for (int i = 0; i < count; i++) {
			Pad pad;
			pad.id = ids[i];
			pad.vid = SDL_GetJoystickVendorForID(pad.id);
			pad.pid = SDL_GetJoystickProductForID(pad.id);
			pad.sdlType = SDL_GetGamepadTypeForID(pad.id);
			pad.xboxType = pad.sdlType == SDL_GAMEPAD_TYPE_XBOX360 || pad.sdlType == SDL_GAMEPAD_TYPE_XBOXONE;
			pad.gamepad = SDL_IsGamepad(pad.id);
			char guid[33];
			SDL_GUIDToString(SDL_GetJoystickGUIDForID(pad.id), guid, sizeof(guid));
			pad.identity = {guid, "", Text(SDL_GetJoystickPathForID(pad.id)), Text(SDL_GetJoystickNameForID(pad.id))};
			pads.push_back(std::move(pad));
		}
		SDL_free(ids);
		Classify(pads);
		// Only the pads the layer may slot are opened: a pad the game reads
		// through XInput is never SDL's to hold.
		for (auto it = handles_.begin(); it != handles_.end();) {
			const bool keep = std::any_of(pads.begin(), pads.end(), [&](const Pad& pad) { return pad.id == it->first && pad.route == Route::Waiting; });
			if (keep) {
				++it;
				continue;
			}
			SDL_CloseGamepad(it->second);
			it = handles_.erase(it);
		}
		for (Pad& pad : pads) {
			if (pad.route != Route::Waiting) continue;
			auto handle = handles_.find(pad.id);
			if (handle == handles_.end()) {
				SDL_Gamepad* opened = SDL_OpenGamepad(pad.id);
				if (!opened) {
					Log("pads: '%s' could not be opened (SDL: %s); left to the game", pad.identity.name.c_str(), SDL_GetError());
					pad.route = Route::Unmapped;
					continue;
				}
				handle = handles_.emplace(pad.id, opened).first;
			}
			pad.identity.serial = Text(SDL_GetGamepadSerial(handle->second));
		}
		const std::vector<Pad> before = std::move(pads_);
		pads_ = std::move(pads);
		Place(ProbeRealSlots(0xFu, 0), "");
		LogChanges(before);
	}

	// The slots again without listing the pads: a virtual pad or an XInput
	// pad took or left a slot.
	void Replan(unsigned real, const char* why)
	{
		const std::vector<Pad> before = pads_;
		for (Pad& pad : pads_) {
			if (pad.route == Route::Slotted || pad.route == Route::Waiting) {
				pad.route = Route::Waiting;
				pad.slot = -1;
			}
		}
		Place(real, why);
		LogChanges(before);
	}

	void Place(unsigned real, const char* why)
	{
		real_ = real;
		virtual_ = VirtualSlots();
		if (plan_.Update(pads_, real_ | virtual_)) {
			announce_ = true;
			nextAnnounce_ = 0;
			if (*why) Log("pads: %s took or left a slot; the layer's pads move", why);
		}
		UpdateStatus();
	}

	void LogChanges(const std::vector<Pad>& before)
	{
		for (const Pad& old : before) {
			if (std::none_of(pads_.begin(), pads_.end(), [&](const Pad& pad) { return pad.id == old.id; })) {
				Log("pads: '%s' removed%s", old.identity.name.c_str(),
					old.slot >= 0 ? (" from slot " + std::to_string(old.slot)).c_str() : "");
			}
		}
		for (const Pad& pad : pads_) {
			const auto old = std::find_if(before.begin(), before.end(), [&](const Pad& candidate) { return candidate.id == pad.id; });
			if (old != before.end() && old->route == pad.route && old->slot == pad.slot) continue;
			const char* verb = old == before.end() ? "added" : "now";
			switch (pad.route) {
			case Route::Slotted:
				Log("pads: '%s' %04X:%04X %s %s: the game reads it as XInput slot %d", pad.identity.name.c_str(), pad.vid, pad.pid,
					TypeName(pad.sdlType), verb, pad.slot);
				break;
			case Route::GameReads:
				Log("pads: '%s' %04X:%04X %s %s: an XInput pad, which the game reads itself", pad.identity.name.c_str(), pad.vid,
					pad.pid, TypeName(pad.sdlType), verb);
				break;
			case Route::SteamTwin:
				Log("pads: '%s' %04X:%04X %s: Steam gives it to the game as its virtual pad; left to Steam", pad.identity.name.c_str(),
					pad.vid, pad.pid, verb);
				break;
			case Route::Unmapped:
				Log("pads: '%s' %04X:%04X %s: SDL has no layout for it; the game does not see it", pad.identity.name.c_str(), pad.vid,
					pad.pid, verb);
				break;
			case Route::Waiting:
				Log("pads: '%s' %04X:%04X %s: every XInput slot is taken; it waits for one", pad.identity.name.c_str(), pad.vid, pad.pid,
					verb);
				break;
			}
		}
	}

	void UpdateStatus()
	{
		std::string pads = "[";
		for (const Pad& pad : pads_) {
			char ids[16];
			snprintf(ids, sizeof(ids), "%04X:%04X", pad.vid, pad.pid);
			pads += (pads.size() > 1 ? "," : "") + std::string("{\"name\":") + JsonString(pad.identity.name) + ",\"ids\":\"" + ids +
				"\",\"type\":" + JsonString(TypeName(pad.sdlType)) + ",\"route\":" + JsonString(RouteName(pad.route)) +
				",\"slot\":" + std::to_string(pad.slot) + "}";
		}
		SetStatus(std::string("{\"state\":\"running\",\"sdl\":\"") + version_ + "\",\"held\":" + std::to_string(plan_.Held()) +
			",\"real\":" + std::to_string(real_) + ",\"virtual\":" + std::to_string(virtual_) + ",\"pads\":" + pads + "]}");
	}

	// Each slot the layer holds, as its pad is now.
	void ReadSlots()
	{
		for (int slot = 0; slot < kSlots; slot++) {
			const PadId id = plan_.InSlot(slot);
			const auto handle = id ? handles_.find(id) : handles_.end();
			if (handle == handles_.end() || !SDL_GamepadConnected(handle->second)) {
				Advance(slots_[slot], 0, {}, false);
				continue;
			}
			const sf4e::gamepads::State state = ReadState(handle->second);
			Advance(slots_[slot], id, sf4e::gamepads::MapToXInput(state), state.buttons[static_cast<unsigned>(Button::Guide)]);
		}
		Publish(slots_);
	}

	// The motors the game asked of each slot, on its pad. The game asks on
	// every poll, so they are sent when they change, and held motors again
	// before SDL's 250 ms runs out.
	void ApplyMotors(ULONGLONG now)
	{
		const std::array<Motors, kSlots> requested = RequestedMotors();
		for (int slot = 0; slot < kSlots; slot++) {
			const Motors& want = requested[slot];
			Motors& sent = sent_[slot];
			const bool changed = want.id != sent.id || want.left != sent.left || want.right != sent.right;
			const bool held = want.left || want.right;
			if (!changed && !(held && now >= resend_[slot])) continue;
			sent = want;
			resend_[slot] = now + 100;
			const auto handle = want.id ? handles_.find(want.id) : handles_.end();
			if (handle != handles_.end()) SDL_RumbleGamepad(handle->second, want.left, want.right, 250);
		}
	}

	std::vector<Pad> pads_;
	std::map<PadId, SDL_Gamepad*> handles_;
	Plan plan_;
	std::array<SlotState, kSlots> slots_{};
	std::array<Motors, kSlots> sent_{};
	std::array<ULONGLONG, kSlots> resend_{};
	unsigned real_ = 0, virtual_ = 0;
	bool announce_ = false;
	ULONGLONG nextAnnounce_ = 0;
	HANDLE timer_ = nullptr;
	char version_[16] = "?";
};

DWORD WINAPI LayerMain(void* parameter)
{
	SetThreadDescription(GetCurrentThread(), L"SFV Ember controller layer");
	try {
		static_cast<LayerThread*>(parameter)->Run();
	} catch (const std::exception& e) {
		Log("pads: the controller layer stopped: %s", e.what());
		ClearSlots();
		SetStatus("{\"state\":\"stopped\",\"error\":" + JsonString(e.what()) + "}");
	} catch (...) {
		Log("pads: the controller layer stopped");
		ClearSlots();
		SetStatus("{\"state\":\"stopped\"}");
	}
	return 0;
}

} // namespace

void StartSdlPads(bool enabled)
{
	if (s_started.exchange(true)) return;
	if (!enabled) {
		Log("pads: the controller layer is off (settings.json's controllerLayer); the game reads the pads itself");
		SetStatus("{\"state\":\"off\"}");
		return;
	}
	SetStatus("{\"state\":\"starting\"}");
	// Never destroyed: the thread runs until the process ends, and a static's
	// destructor would race it at exit.
	auto* thread = new LayerThread();
	if (HANDLE handle = CreateThread(nullptr, 0, &LayerMain, thread, 0, nullptr)) {
		CloseHandle(handle);
	} else {
		Log("pads: the controller layer's thread could not start (Win32 %lu); the game reads the pads itself", GetLastError());
		SetStatus("{\"state\":\"failed\"}");
	}
}

void NoSdlPads(const char* why)
{
	if (s_started.exchange(true)) return;
	Log("%s", why);
	SetStatus("{\"state\":\"no hooks\"}");
}

void RegisterSdlPadStatus()
{
	RegisterStatus("sdlPads", [] {
		std::lock_guard lock(s_statusMutex);
		return s_status;
	});
}

} // namespace sfve::mod
