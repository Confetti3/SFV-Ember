#include "mod/SdlPads.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameExit.hxx"
#include "mod/Log.hxx"
#include "mod/PadLayer.hxx"
#include "mod/VirtualPads.hxx"
#include "mod/XInputHooks.hxx"

#include "common/SdlPadSource.hxx"

#include <windows.h>

#include <nlohmann/json.hpp>

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace sfve::mod {

namespace {

namespace gamepads = sf4e::gamepads;
using gamepads::Pad;
using gamepads::Status;
using layer::Motors;
using layer::SlotState;

// The pipe's view of the layer, rebuilt by its thread whenever it changes.
std::mutex s_statusMutex;
std::string s_status = R"({"state":"not started"})";

void SetStatus(const nlohmann::json& status)
{
	std::string text = status.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::lock_guard lock(s_statusMutex);
	s_status = std::move(text);
}

// The layer thread's life: the game's exit asks it to stop, and waits until
// it has (StartSdlPads).
std::atomic<bool> s_stopRequested{false};
std::atomic<bool> s_running{false};

// What the layer does with a pad, for the log and the pipe's status.
struct Route {
	const char* name;
	const char* says;
};

Route RouteOf(const Pad& pad)
{
	switch (pad.status) {
	case Status::Slotted: return {"slotted", "the game reads it as XInput slot"};
	case Status::GameRead: return {"game reads it", "an XInput pad, which the game reads itself"};
	case Status::SteamTwin: return {"Steam twin", "Steam gives it to the game as its virtual pad; left to Steam"};
	case Status::Unmapped: return {"unmapped", "SDL has no layout for it; the game does not see it"};
	case Status::NotInUse:
		if (!pad.available) return {"not opened", "SDL could not open it; the game does not see it"};
		return {"waiting", "every XInput slot is taken; it waits for one"};
	}
	return {"?", "?"};
}

// Bit i when slot i is a virtual test pad's (mod/VirtualPads), which wins
// over the layer as it does over XInput.
unsigned VirtualSlots()
{
	unsigned mask = 0;
	for (int slot = 0; slot < kPadSlots; slot++) {
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
	for (int slot = 0; slot < kPadSlots; slot++) {
		if (!((which >> slot) & 1u)) continue;
		if (PhysicalPadConnected(slot)) known |= 1u << slot;
		else known &= ~(1u << slot);
	}
	return known;
}

class LayerThread {
public:
	// Until the game's exit asks it to stop.
	void Run()
	{
		// This thread's own XInput calls (PhysicalPadConnected) go straight to
		// XInput. SDL's threads are not covered: SdlBackends::NotXInput keeps
		// SDL off XInput, so none of them reads the layer's slots back.
		PassPadReadsThrough();
		if (const std::optional<std::string> failed = source_.Start(gamepads::SdlBackends::NotXInput)) {
			Log("pads: the controller layer could not start SDL (%s); the game reads the pads itself", failed->c_str());
			SetStatus({{"state", "failed"}, {"error", *failed}});
			return;
		}
		QueueOnlyPadChanges();
		version_ = gamepads::SdlVersion();
		Log("pads: the controller layer started SDL %s (HIDAPI and DirectInput; SDL's XInput off)", version_.c_str());
		timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
		Rebuild();
		ULONGLONG nextProbe = GetTickCount64() + kProbeMs;
		while (!s_stopRequested) {
			const bool padsChanged = source_.Pump();
			const ULONGLONG now = GetTickCount64();
			if (padsChanged) {
				Rebuild();
				nextProbe = now + kProbeMs;
			} else if (VirtualSlots() != virtual_) {
				Place(inventory_.pads, real_, "a virtual pad");
			} else if (now >= nextProbe) {
				// Every slot when a pad waits for one, else the layer's own.
				const unsigned real = ProbeRealSlots(Waiting() ? 0xFu : HeldSlots(), real_);
				if (real != real_) Place(inventory_.pads, real, "an XInput pad");
				nextProbe = now + kProbeMs;
			}
			ReadSlots();
			ApplyMotors(now);
			Announce(now);
			// A held slot is read every millisecond, so the game's read once
			// a frame finds the pad at most about a millisecond old; with no
			// pad held, only plugging one in is waited for.
			Wait(HeldSlots() ? 1 : 16);
		}
		Shutdown();
		Log("pads: the controller layer stopped SDL for the game's exit");
		SetStatus({{"state", "stopped"}});
	}

	// Lets every pad go: motors stopped, slots back to XInput, handles closed
	// and SDL quit, all on this thread.
	void Shutdown()
	{
		for (const Pad& pad : inventory_.pads) {
			if (SDL_Gamepad* gamepad = source_.Handle(pad.id)) SDL_RumbleGamepad(gamepad, 0, 0, 0);
		}
		layer::ClearSlots();
		source_.Stop();
		if (timer_) CloseHandle(timer_);
		timer_ = nullptr;
	}

private:
	static constexpr ULONGLONG kProbeMs = 1000;
	// The motors are sent for SDL's 250 ms and sent again every 100 ms while held.
	static constexpr Uint32 kRumbleMs = 250;
	static constexpr ULONGLONG kRumbleRefreshMs = 100;

	// The layer reads states, never SDL's per-press events: only pads coming
	// and going are queued.
	static void QueueOnlyPadChanges()
	{
		for (const SDL_EventType type : {SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
				 SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP, SDL_EVENT_JOYSTICK_BATTERY_UPDATED,
				 SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_DOWN, SDL_EVENT_GAMEPAD_BUTTON_UP,
				 SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
				 SDL_EVENT_GAMEPAD_SENSOR_UPDATE}) {
			SDL_SetEventEnabled(type, false);
		}
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
		std::optional<std::vector<Pad>> pads = source_.List();
		if (!pads) {
			Log("pads: SDL could not list the pads (%s)", SDL_GetError());
			return;
		}
		// Only the pads the layer may slot are opened: a pad the game reads
		// through XInput is never SDL's to hold.
		gamepads::Classify(*pads, {}, layer::GameReadsItself);
		for (const gamepads::OpenFailure& failure : source_.Hold(*pads)) {
			Log("pads: '%s' could not be opened (SDL: %s); left to the game", failure.name.c_str(), failure.error.c_str());
		}
		Place(std::move(*pads), ProbeRealSlots(0xFu, 0), nullptr);
	}

	// Which slot each pad takes, with `real` the slots XInput's own pads
	// hold; `why` names what moved the slots when SDL's pads did not.
	void Place(std::vector<Pad> pads, unsigned real, const char* why)
	{
		real_ = real;
		virtual_ = VirtualSlots();
		const std::vector<Pad> before = std::move(inventory_.pads);
		inventory_ = slots_.Update(std::move(pads), {}, {}, real_ | virtual_);
		if (slots_.Changed()) {
			announce_ = true;
			nextAnnounce_ = 0;
			if (why) Log("pads: %s took or left a slot; the layer's pads move", why);
		}
		LogChanges(before);
		UpdateStatus();
	}

	unsigned HeldSlots() const
	{
		unsigned mask = 0;
		for (const Pad& pad : inventory_.pads) {
			if (pad.slot >= 0) mask |= 1u << pad.slot;
		}
		return mask;
	}

	// A pad the layer opened waits for a free slot.
	bool Waiting() const
	{
		return std::any_of(inventory_.pads.begin(), inventory_.pads.end(),
			[](const Pad& pad) { return pad.status == Status::NotInUse && pad.available; });
	}

	void LogChanges(const std::vector<Pad>& before) const
	{
		const auto find = [](const std::vector<Pad>& pads, gamepads::Id id) {
			return std::find_if(pads.begin(), pads.end(), [&](const Pad& pad) { return pad.id == id; });
		};
		for (const Pad& old : before) {
			if (find(inventory_.pads, old.id) != inventory_.pads.end()) continue;
			Log("pads: '%s' removed%s", old.identity.name.c_str(), old.slot >= 0 ? (" from slot " + std::to_string(old.slot)).c_str() : "");
		}
		for (const Pad& pad : inventory_.pads) {
			const auto old = find(before, pad.id);
			const bool added = old == before.end();
			if (!added && old->status == pad.status && old->available == pad.available && old->slot == pad.slot) continue;
			Log("pads: '%s' %04X:%04X %s %s: %s%s", pad.identity.name.c_str(), pad.vid, pad.pid, gamepads::TypeName(pad.type),
				added ? "added" : "now", RouteOf(pad).says, pad.slot >= 0 ? (" " + std::to_string(pad.slot)).c_str() : "");
		}
	}

	void UpdateStatus() const
	{
		nlohmann::json pads = nlohmann::json::array();
		for (const Pad& pad : inventory_.pads) {
			char ids[16];
			snprintf(ids, sizeof(ids), "%04X:%04X", pad.vid, pad.pid);
			pads.push_back({{"name", pad.identity.name}, {"ids", ids}, {"type", gamepads::TypeName(pad.type)},
				{"route", RouteOf(pad).name}, {"slot", pad.slot}});
		}
		SetStatus({{"state", "running"}, {"sdl", version_}, {"held", HeldSlots()}, {"real", real_}, {"virtual", virtual_},
			{"pads", std::move(pads)}});
	}

	// Each slot the layer holds, as its pad is now.
	void ReadSlots()
	{
		std::array<const Pad*, kPadSlots> inSlot{};
		for (const Pad& pad : inventory_.pads) {
			if (pad.slot >= 0) inSlot[static_cast<size_t>(pad.slot)] = &pad;
		}
		for (int slot = 0; slot < kPadSlots; slot++) {
			const Pad* pad = inSlot[slot];
			SDL_Gamepad* gamepad = pad ? source_.Handle(pad->id) : nullptr;
			if (!gamepad || !SDL_GamepadConnected(gamepad)) {
				layer::Advance(published_[slot], 0, {}, false);
				continue;
			}
			const gamepads::State state = gamepads::ReadState(gamepad);
			layer::Advance(published_[slot], pad->id, gamepads::MapToXInput(state),
				state.buttons[static_cast<unsigned>(gamepads::Button::Guide)]);
		}
		layer::Publish(published_);
	}

	// The motors the game asked of each slot, on its pad. The game asks on
	// every poll, so they are sent when they change, and held motors again
	// before SDL's duration runs out (the library's rumble rule).
	void ApplyMotors(ULONGLONG now)
	{
		const std::array<Motors, kPadSlots> requested = layer::RequestedMotors();
		for (int slot = 0; slot < kPadSlots; slot++) {
			const Motors& want = requested[slot];
			Motors& sent = sent_[slot];
			if (!gamepads::ShouldApplyRumble(want != sent, want.left, want.right, now >= resend_[slot])) continue;
			sent = want;
			resend_[slot] = now + kRumbleRefreshMs;
			if (SDL_Gamepad* gamepad = want.id ? source_.Handle(want.id) : nullptr) {
				SDL_RumbleGamepad(gamepad, want.left, want.right, kRumbleMs);
			}
		}
	}

	// UE4 looks at a newly connected slot only after WM_DEVICECHANGE, so the
	// layer posts one whenever its slots change, and again every 250 ms until
	// one of the engine's own windows has taken it. Before the game's window
	// exists there is nothing to tell, and the game's first look at the pads
	// comes after it.
	void Announce(ULONGLONG now)
	{
		if (!announce_ || now < nextAnnounce_) return;
		if (AnnounceDeviceChange() > 0) announce_ = false;
		else nextAnnounce_ = now + 250;
	}

	gamepads::SdlPadSource source_;
	gamepads::Slots slots_{layer::GameReadsItself};
	gamepads::Inventory inventory_;
	std::array<SlotState, kPadSlots> published_{};
	std::array<Motors, kPadSlots> sent_{};
	std::array<ULONGLONG, kPadSlots> resend_{};
	unsigned real_ = 0, virtual_ = 0;
	bool announce_ = false;
	ULONGLONG nextAnnounce_ = 0;
	HANDLE timer_ = nullptr;
	std::string version_ = "?";
};

DWORD WINAPI LayerMain(void*)
{
	SetThreadDescription(GetCurrentThread(), L"SFV Ember controller layer");
	{
		// On this thread's stack: SDL starts, runs and ends here.
		LayerThread thread;
		try {
			thread.Run();
		} catch (const std::exception& e) {
			Log("pads: the controller layer stopped: %s", e.what());
			thread.Shutdown();
			SetStatus({{"state", "stopped"}, {"error", e.what()}});
		} catch (...) {
			Log("pads: the controller layer stopped");
			thread.Shutdown();
			SetStatus({{"state", "stopped"}});
		}
	}
	s_running = false;
	return 0;
}

} // namespace

void StartSdlPads(PadLayerStart start)
{
	RegisterStatus("sdlPads", [] {
		std::lock_guard lock(s_statusMutex);
		return s_status;
	});
	switch (start) {
	case PadLayerStart::NoHooks:
		Log("pads: no controller layer without the pad hooks; the game reads the pads itself");
		SetStatus({{"state", "no hooks"}});
		return;
	case PadLayerStart::Off:
		Log("pads: the controller layer is off (settings.json's controllerLayer); the game reads the pads itself");
		SetStatus({{"state", "off"}});
		return;
	case PadLayerStart::On: break;
	}
	SetStatus({{"state", "starting"}});
	s_running = true;
	HANDLE thread = CreateThread(nullptr, 0, &LayerMain, nullptr, 0, nullptr);
	if (!thread) {
		s_running = false;
		Log("pads: the controller layer's thread could not start (Win32 %lu); the game reads the pads itself", GetLastError());
		SetStatus({{"state", "failed"}});
		return;
	}
	CloseHandle(thread);
	// The game's exit (when the build check let the mod hook it) stops the
	// thread, which lets every pad go and quits SDL itself; a DualShock 4 or
	// DualSense would otherwise keep its last rumble and lights. This runs on
	// the game's main thread before its first frame, the thread the exit
	// work runs on later, so the work's list needs no lock.
	OnGameExit({[] { s_stopRequested = true; }, [] { return !s_running; }, nullptr});
}

} // namespace sfve::mod
