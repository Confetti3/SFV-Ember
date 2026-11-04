#include "mod/VirtualPads.hxx"
#include "mod/GameThread.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"

#include <MinHook.h>

#include <windows.h>

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

struct Name {
	const char* text;
	uint16_t bit;
};
constexpr Name kNames[] = {
	{"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008}, {"START", 0x0010}, {"BACK", 0x0020},
	{"LS", 0x0040}, {"RS", 0x0080}, {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000},
	{"Y", 0x8000},
};

// UE4 looks for newly connected pads only when Windows reports a device
// change, so plugging a virtual pad in later has to say so.
void AnnounceDeviceChange()
{
	EnumWindows(
		[](HWND hwnd, LPARAM) -> BOOL {
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid == GetCurrentProcessId()) PostMessageW(hwnd, WM_DEVICECHANGE, 0x0007 /* DBT_DEVNODES_CHANGED */, 0);
			return TRUE;
		},
		0);
}

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
constexpr const wchar_t* kXInputModules[] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
constexpr int kModules = sizeof(kXInputModules) / sizeof(kXInputModules[0]);
GetState_t o_getState[kModules];   // XInputGetState
GetState_t o_getStateEx[kModules]; // ordinal 100, XInputGetStateEx (adds the guide button)

template <int M, bool Ex>
DWORD WINAPI HookedGetState(DWORD user, void* state)
{
	if (user == 0) PumpGameThread(); // once per poll of the pads
	if (FillVirtualXInput((int)user, state)) return ERROR_SUCCESS;
	return (Ex ? o_getStateEx[M] : o_getState[M])(user, state);
}

template <int M>
bool HookModule()
{
	wchar_t path[MAX_PATH];
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (n == 0 || n + 20 >= MAX_PATH) return false;
	wcscat_s(path, L"\\");
	wcscat_s(path, kXInputModules[M]);
	HMODULE module = LoadLibraryW(path);
	if (!module) return true; // not on this system: nothing reads through it
	bool ok = true;
	const auto hook = [&](void* target, void* detour, void** original) {
		if (!target) return;
		MH_STATUS st = MH_CreateHook(target, detour, original);
		if (st == MH_OK) st = MH_EnableHook(target);
		ok = ok && st == MH_OK;
	};
	hook((void*)GetProcAddress(module, "XInputGetState"), (void*)HookedGetState<M, false>, (void**)&o_getState[M]);
	hook((void*)GetProcAddress(module, MAKEINTRESOURCEA(100)), (void*)HookedGetState<M, true>,
		(void**)&o_getStateEx[M]);
	return ok;
}

} // namespace

bool InstallVirtualPads()
{
	const bool ok = HookModule<0>() && HookModule<1>() && HookModule<2>();
	Log("virtual pads: %s", ok ? "XInput hooked" : "could not hook XInput");
	return ok;
}

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
