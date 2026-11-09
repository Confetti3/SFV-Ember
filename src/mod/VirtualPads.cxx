#include "mod/VirtualPads.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "mod/PadLayer.hxx"

#include <windows.h>

#include <stdio.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <sstream>

namespace sfve::mod {

namespace {

struct XState {
	DWORD packet;
	WORD buttons;
	BYTE leftTrigger;
	BYTE rightTrigger;
	SHORT lx, ly, rx, ry;
};

struct Slot {
	bool enabled = false;
	std::deque<PadStep> steps;
	ULONGLONG stepStartedMs = 0;
	DWORD packet = 0;
	uint32_t lastButtons = 0xFFFFFFFF;
};

std::mutex s_mutex;
Slot s_slots[kPadSlots];
uint64_t s_reads[kPadSlots]; // XInput reads seen per slot, for the status report
// Each slot's last read: bit 16 connected, bits 0-15 the buttons, bits 20-23
// the left stick's directions (PadSeen::stick), bits 24 and 25 the left and
// right triggers held.
std::atomic<uint32_t> s_seen[kPadSlots];
// How often each slot's record changed from one read to the next: a pad two
// readers see differently changes on almost every read.
std::atomic<uint32_t> s_seenChanges[kPadSlots];
constexpr uint32_t kSeenConnected = 0x10000;
constexpr int kSeenStickShift = 20;
constexpr uint32_t kSeenLeftTrigger = 1u << 24, kSeenRightTrigger = 1u << 25;
constexpr BYTE kTriggerHeld = 30; // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
// How far the left stick leans before it counts as a direction (of 32767).
constexpr SHORT kStickLean = 16000;

uint32_t Seen(const XState& state)
{
	const uint32_t stick = (state.ly > kStickLean ? 0x1u : 0u) | (state.ly < -kStickLean ? 0x2u : 0u) |
		(state.lx < -kStickLean ? 0x4u : 0u) | (state.lx > kStickLean ? 0x8u : 0u);
	return kSeenConnected | state.buttons | stick << kSeenStickShift |
		(state.leftTrigger > kTriggerHeld ? kSeenLeftTrigger : 0u) | (state.rightTrigger > kTriggerHeld ? kSeenRightTrigger : 0u);
}

struct Name {
	const char* text;
	uint16_t bit;
};
constexpr Name kNames[] = {
	{"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008}, {"START", 0x0010}, {"BACK", 0x0020},
	{"LS", 0x0040}, {"RS", 0x0080}, {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000},
	{"Y", 0x8000},
};

// Fills an XINPUT_STATE for a virtual slot. False for a physical slot.
bool FillVirtualXInput(int slot, void* xinputState)
{
	if (slot < 0 || slot >= kPadSlots || !xinputState) return false;
	std::lock_guard lock(s_mutex);
	s_reads[slot]++;
	Slot& s = s_slots[slot];
	if (!s.enabled) return false;

	const ULONGLONG now = GetTickCount64();
	while (!s.steps.empty() && now - s.stepStartedMs >= s.steps.front().durationMs) {
		s.stepStartedMs += s.steps.front().durationMs;
		s.steps.pop_front();
	}
	if (s.steps.empty()) s.stepStartedMs = now;
	const PadStep held = s.steps.empty() ? PadStep{} : s.steps.front();

	auto* out = static_cast<XState*>(xinputState);
	*out = {};
	const uint32_t key = held.buttons | (uint32_t)held.leftTrigger << 16 | (uint32_t)held.rightTrigger << 24;
	if (key != s.lastButtons) {
		s.lastButtons = key;
		s.packet++;
	}
	out->packet = s.packet;
	out->buttons = held.buttons;
	out->leftTrigger = held.leftTrigger;
	out->rightTrigger = held.rightTrigger;
	return true;
}

// --- XInput hooks: every system XInput library the game may read pads through.

using GetState_t = DWORD(WINAPI*)(DWORD, void*);
using GetCapabilities_t = DWORD(WINAPI*)(DWORD, DWORD, void*);
using SetState_t = DWORD(WINAPI*)(DWORD, void*);
// A system XInput library, with the names its hooked calls have in a HookFailure.
struct XInputModule {
	const wchar_t* file;
	const char* getState;
	const char* getStateEx;
	const char* getCapabilities;
	const char* setState;
};
constexpr XInputModule kXInputModules[] = {
	{L"xinput1_4.dll", "xinput1_4!XInputGetState", "xinput1_4!XInputGetStateEx", "xinput1_4!XInputGetCapabilities",
		"xinput1_4!XInputSetState"},
	{L"xinput1_3.dll", "xinput1_3!XInputGetState", "xinput1_3!XInputGetStateEx", "xinput1_3!XInputGetCapabilities",
		"xinput1_3!XInputSetState"},
	{L"xinput9_1_0.dll", "xinput9_1_0!XInputGetState", "xinput9_1_0!XInputGetStateEx", "xinput9_1_0!XInputGetCapabilities",
		"xinput9_1_0!XInputSetState"},
};
constexpr int kModules = sizeof(kXInputModules) / sizeof(kXInputModules[0]);
GetState_t o_getState[kModules];   // XInputGetState
GetState_t o_getStateEx[kModules]; // ordinal 100, XInputGetStateEx (adds the guide button)
GetCapabilities_t o_getCapabilities[kModules];
SetState_t o_setState[kModules];

std::atomic<PadFilter> s_padFilter;

// One XInput library may read through another: Windows 11's xinput1_3.dll
// (10.0.26200) is a shim whose XInputGetState and XInputGetStateEx call
// xinput1_4.dll's, and both are hooked. The game's one read then enters a
// hook twice on the same thread. Only the outermost call is the read: it
// fills a virtual slot, records the slot and runs the pad filter. A nested
// call goes straight to the library. Without this the outer call recorded
// the state the inner call's filter had already emptied, so while Ember's
// menu held the pads its record of every pad read "nothing pressed".
//
// The capabilities and motor calls nest the same way, and pass through the
// same way. A thread that reads XInput for itself (the controller layer's,
// PassPadReadsThrough) passes every call straight to the library: none of
// them is the game's.
thread_local int t_hookDepth = 0;
thread_local bool t_passThrough = false;
std::atomic<uint64_t> s_nestedReads; // nested calls passed straight through, for the status report

struct Depth {
	Depth() { t_hookDepth++; }
	~Depth() { t_hookDepth--; }
};

template <int M, bool Ex>
DWORD WINAPI HookedGetState(DWORD user, void* state)
{
	const GetState_t original = Ex ? o_getStateEx[M] : o_getState[M];
	if (t_passThrough) return original(user, state);
	if (t_hookDepth > 0) {
		s_nestedReads++;
		return original(user, state);
	}
	Depth depth;
	// A virtual test pad first, then the controller layer's pad in a slot no
	// real XInput pad holds (mod/PadLayer), then XInput itself.
	const bool answered = FillVirtualXInput((int)user, state) || layer::FillState((int)user, state, Ex);
	const DWORD result = answered ? ERROR_SUCCESS : original(user, state);
	if (user < kPadSlots) {
		const uint32_t seen = result == ERROR_SUCCESS && state ? Seen(*static_cast<const XState*>(state)) : 0;
		if (s_seen[user].exchange(seen) != seen) s_seenChanges[user]++;
	}
	if (result == ERROR_SUCCESS && state) {
		if (const PadFilter filter = s_padFilter) filter(user, state);
	}
	return result;
}

// A slot the controller layer holds answers as a connected gamepad; any other
// is XInput's to answer.
template <int M>
DWORD WINAPI HookedGetCapabilities(DWORD user, DWORD flags, void* capabilities)
{
	const GetCapabilities_t original = o_getCapabilities[M];
	if (t_passThrough || t_hookDepth > 0) return original(user, flags, capabilities);
	Depth depth;
	return layer::FillCapabilities((int)user, capabilities) ? ERROR_SUCCESS : original(user, flags, capabilities);
}

// The game sets the motors of every connected slot on each poll (zero when
// nothing shakes); a slot the controller layer holds keeps them for its
// thread, which turns its pad's motors. XINPUT_VIBRATION is two WORDs.
template <int M>
DWORD WINAPI HookedSetState(DWORD user, void* vibration)
{
	const SetState_t original = o_setState[M];
	if (t_passThrough || t_hookDepth > 0) return original(user, vibration);
	Depth depth;
	if (vibration) {
		const auto* motors = static_cast<const WORD*>(vibration);
		if (layer::SetMotors((int)user, motors[0], motors[1])) return ERROR_SUCCESS;
	}
	return original(user, vibration);
}

// Adds library M's reads to the pads' set, by the library's full path in the
// system folder (so never the xinput1_3.dll proxy beside the game). A library
// that is not on this system, or that lacks one of the reads (not every XInput
// library exports ordinal 100), adds nothing for it: nothing reads through it.
template <int M>
void AddXInputHooks(HookTransaction& hooks, const std::wstring& systemFolder)
{
	const XInputModule& library = kXInputModules[M];
	const HMODULE module = LoadLibraryW((systemFolder + L"\\" + library.file).c_str());
	if (!module) return;
	if (void* target = (void*)GetProcAddress(module, "XInputGetState")) {
		hooks.Add(library.getState, target, (void*)HookedGetState<M, false>, (void**)&o_getState[M]);
	}
	if (void* target = (void*)GetProcAddress(module, MAKEINTRESOURCEA(100))) {
		hooks.Add(library.getStateEx, target, (void*)HookedGetState<M, true>, (void**)&o_getStateEx[M]);
	}
	if (void* target = (void*)GetProcAddress(module, "XInputGetCapabilities")) {
		hooks.Add(library.getCapabilities, target, (void*)HookedGetCapabilities<M>, (void**)&o_getCapabilities[M]);
	}
	if (void* target = (void*)GetProcAddress(module, "XInputSetState")) {
		hooks.Add(library.setState, target, (void*)HookedSetState<M>, (void**)&o_setState[M]);
	}
}

// Hooks every system XInput library's calls as one set (InstallVirtualPads).
std::optional<HookFailure> InstallXInputHooks();

} // namespace

int AnnounceDeviceChange()
{
	int posted = 0;
	EnumWindows(
		[](HWND hwnd, LPARAM count) -> BOOL {
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == GetCurrentProcessId() && PostMessageW(hwnd, WM_DEVICECHANGE, 0x0007 /* DBT_DEVNODES_CHANGED */, 0)) {
				++*reinterpret_cast<int*>(count);
			}
			return TRUE;
		},
		reinterpret_cast<LPARAM>(&posted));
	return posted;
}

void PassPadReadsThrough()
{
	t_passThrough = true;
}

bool PhysicalPadConnected(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return false;
	// The first library hooked is the one underneath the others (xinput1_4
	// calls no other), so the read enters no hook; the pass-through covers a
	// PC where it does.
	GetState_t read = nullptr;
	for (int m = 0; m < kModules && !read; m++) read = o_getState[m];
	if (!read) return false;
	const bool was = t_passThrough;
	t_passThrough = true;
	XState state{};
	const bool connected = read((DWORD)slot, &state) == ERROR_SUCCESS;
	t_passThrough = was;
	return connected;
}

void SetPadFilter(PadFilter filter)
{
	s_padFilter = filter;
}

uint64_t PadReads(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return 0;
	std::lock_guard lock(s_mutex);
	return s_reads[slot];
}

uint64_t NestedPadReads()
{
	return s_nestedReads;
}

PadSeen LastPadState(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return {};
	const uint32_t seen = s_seen[slot];
	return {(seen & kSeenConnected) != 0, static_cast<uint16_t>(seen & 0xFFFF), static_cast<uint8_t>(seen >> kSeenStickShift & 0xF),
		(seen & kSeenLeftTrigger) != 0, (seen & kSeenRightTrigger) != 0};
}

std::optional<HookFailure> InstallVirtualPads()
{
	// Once per process: a second call answers as the first did (the tests
	// that read through the hooks each ask for them).
	static std::optional<std::optional<HookFailure>> s_installed;
	if (s_installed) return *s_installed;
	s_installed = InstallXInputHooks();
	return *s_installed;
}

namespace {

std::optional<HookFailure> InstallXInputHooks()
{
	wchar_t folder[MAX_PATH];
	const UINT n = GetSystemDirectoryW(folder, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		Log("virtual pads: no system folder to find XInput in (Win32 %lu); the pads are the game's", GetLastError());
		// Not a hook that failed: no MinHook status exists, and the status reads
		// "missing XInput (no system folder to find it in)".
		return HookFailure{"XInput (no system folder to find it in)", MH_UNKNOWN};
	}
	// Every library's reads or none: with only some hooked, a read would get
	// the virtual pads, the record and the pad filter through one library and
	// not another.
	const std::wstring systemFolder(folder, n);
	HookTransaction hooks;
	static_assert(kModules == 3, "one AddXInputHooks per library");
	AddXInputHooks<0>(hooks, systemFolder);
	AddXInputHooks<1>(hooks, systemFolder);
	AddXInputHooks<2>(hooks, systemFolder);
	const std::optional<HookFailure> missing = CommitHookSet(hooks, "virtual pads");
	Log("virtual pads: %s", missing ? "XInput not hooked; the pads are the game's" : "XInput hooked");
	return missing;
}

} // namespace

bool ParseButtons(const std::string& text, PadStep* out)
{
	*out = {};
	if (text == "-" || text.empty()) return true;
	std::stringstream ss(text);
	std::string part;
	while (std::getline(ss, part, '+')) {
		for (char& c : part) c = (char)toupper((unsigned char)c);
		if (part == "LT") {
			out->leftTrigger = 255;
			continue;
		}
		if (part == "RT") {
			out->rightTrigger = 255;
			continue;
		}
		bool found = false;
		for (const Name& n : kNames) {
			if (part == n.text) {
				out->buttons |= n.bit;
				found = true;
			}
		}
		if (!found) return false;
	}
	return true;
}

void SetVirtualPad(int slot, bool enabled)
{
	if (slot < 0 || slot >= kPadSlots) return;
	{
		std::lock_guard lock(s_mutex);
		s_slots[slot].enabled = enabled;
		if (!enabled) s_slots[slot].steps.clear();
	}
	AnnounceDeviceChange();
}

bool IsVirtualPad(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return false;
	std::lock_guard lock(s_mutex);
	return s_slots[slot].enabled;
}

bool QueuePadSteps(int slot, const std::string& steps, std::string* error)
{
	if (slot < 0 || slot >= kPadSlots) {
		*error = "slot out of range";
		return false;
	}
	std::deque<PadStep> parsed;
	std::stringstream ss(steps);
	std::string token;
	while (ss >> token) {
		const size_t colon = token.rfind(':');
		PadStep step;
		if (colon == std::string::npos || !ParseButtons(token.substr(0, colon), &step)) {
			*error = "bad step '" + token + "'";
			return false;
		}
		step.durationMs = (uint32_t)strtoul(token.c_str() + colon + 1, nullptr, 10);
		parsed.push_back(step);
	}
	std::lock_guard lock(s_mutex);
	Slot& s = s_slots[slot];
	if (s.steps.empty()) s.stepStartedMs = GetTickCount64();
	for (const PadStep& p : parsed) s.steps.push_back(p);
	return true;
}

void ClearPadSteps(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return;
	std::lock_guard lock(s_mutex);
	s_slots[slot].steps.clear();
}

size_t PendingPadSteps(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return 0;
	std::lock_guard lock(s_mutex);
	return s_slots[slot].steps.size();
}

void RegisterPadCommands()
{
	RegisterStatus("padReads", [] {
		std::lock_guard lock(s_mutex);
		std::string json = "[";
		for (int i = 0; i < kPadSlots; i++) json += (i ? "," : "") + std::to_string(s_reads[i]);
		return json + "]";
	});
	// Each slot's last read as LastPadState gives it, and how often it changed.
	RegisterStatus("padSeen", [] {
		std::string json = "[";
		for (int i = 0; i < kPadSlots; i++) {
			char item[48];
			snprintf(item, sizeof(item), "%s[\"%08X\",%u]", i ? "," : "", (unsigned)s_seen[i], (unsigned)s_seenChanges[i]);
			json += item;
		}
		return json + "]";
	});
	RegisterStatus("padNested", [] { return std::to_string(NestedPadReads()); });
	RegisterCommand("virtual", [](const std::string& args) {
		const int slot = atoi(args.c_str());
		const bool on = args.find(" on") != std::string::npos;
		SetVirtualPad(slot, on);
		return "ok virtual " + std::to_string(slot) + (on ? " on" : " off");
	});
	RegisterCommand("pad", [](const std::string& args) {
		const size_t space = args.find(' ');
		const int slot = atoi(args.c_str());
		std::string error;
		if (space == std::string::npos) return std::string("error usage: pad <slot> <steps>");
		if (!QueuePadSteps(slot, args.substr(space + 1), &error)) return "error " + error;
		return "ok " + std::to_string(PendingPadSteps(slot)) + " step(s) pending";
	});
	RegisterCommand("padclear", [](const std::string& args) {
		ClearPadSteps(atoi(args.c_str()));
		return std::string("ok");
	});
}

} // namespace sfve::mod
