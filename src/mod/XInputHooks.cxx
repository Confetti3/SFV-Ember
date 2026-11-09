#include "mod/XInputHooks.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "mod/PadLayer.hxx"
#include "mod/VirtualPads.hxx"

#include <windows.h>

#include <Xinput.h>

#include <stdio.h>
#include <wchar.h>

#include <atomic>
#include <string>

namespace sfve::mod {

namespace {

// --- The read record: each slot as the game last read it.

std::atomic<uint64_t> s_reads[kPadSlots];
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
// How far the left stick leans before it counts as a direction (of 32767).
constexpr SHORT kStickLean = 16000;

uint32_t Seen(const XINPUT_GAMEPAD& pad)
{
	const uint32_t stick = (pad.sThumbLY > kStickLean ? 0x1u : 0u) | (pad.sThumbLY < -kStickLean ? 0x2u : 0u) |
		(pad.sThumbLX < -kStickLean ? 0x4u : 0u) | (pad.sThumbLX > kStickLean ? 0x8u : 0u);
	return kSeenConnected | pad.wButtons | stick << kSeenStickShift |
		(pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? kSeenLeftTrigger : 0u) |
		(pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? kSeenRightTrigger : 0u);
}

std::atomic<PadFilter> s_padFilter;

// --- The hooks.

using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GetCapabilities_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using SetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

// A system XInput library, with the names its hooked calls have in a
// HookFailure ("xinput1_4!XInputGetState").
struct XInputModule {
	const wchar_t* file;
	const char* getState;
	const char* getStateEx;
	const char* getCapabilities;
	const char* setState;
};
#define SFVE_XINPUT_MODULE(stem) \
	{L"" #stem ".dll", #stem "!XInputGetState", #stem "!XInputGetStateEx", #stem "!XInputGetCapabilities", #stem "!XInputSetState"}
constexpr XInputModule kXInputModules[] = {
	SFVE_XINPUT_MODULE(xinput1_4),
	SFVE_XINPUT_MODULE(xinput1_3),
	SFVE_XINPUT_MODULE(xinput9_1_0),
};
#undef SFVE_XINPUT_MODULE
constexpr int kModules = sizeof(kXInputModules) / sizeof(kXInputModules[0]);
GetState_t o_getState[kModules];   // XInputGetState
GetState_t o_getStateEx[kModules]; // ordinal 100, XInputGetStateEx (adds the guide button)
GetCapabilities_t o_getCapabilities[kModules];
SetState_t o_setState[kModules];

// One XInput library may read through another: Windows 11's xinput1_3.dll
// (10.0.26200) is a shim whose XInputGetState and XInputGetStateEx call
// xinput1_4.dll's, and both are hooked. The game's one read then enters a
// hook twice on the same thread. Only the outermost call is the read: it
// answers from the slot's source, records the slot and runs the pad filter.
// A nested call goes straight to the library. Without this the outer call
// recorded the state the inner call's filter had already emptied, so while
// Ember's menu held the pads its record of every pad read "nothing pressed".
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

// The slot's answer from a virtual test pad, then from the controller layer;
// false when neither holds it, so XInput answers.
bool FillFromSources(DWORD user, XINPUT_STATE* state, bool ex)
{
	return FillVirtualPad((int)user, state) || layer::FillState((int)user, state, ex);
}

template <int M, bool Ex>
DWORD WINAPI HookedGetState(DWORD user, XINPUT_STATE* state)
{
	const GetState_t original = Ex ? o_getStateEx[M] : o_getState[M];
	if (t_passThrough) return original(user, state);
	if (t_hookDepth > 0) {
		s_nestedReads++;
		return original(user, state);
	}
	Depth depth;
	const DWORD result = FillFromSources(user, state, Ex) ? ERROR_SUCCESS : original(user, state);
	if (user < kPadSlots) {
		s_reads[user]++;
		const uint32_t seen = result == ERROR_SUCCESS && state ? Seen(state->Gamepad) : 0;
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
DWORD WINAPI HookedGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* capabilities)
{
	const GetCapabilities_t original = o_getCapabilities[M];
	if (t_passThrough || t_hookDepth > 0) return original(user, flags, capabilities);
	Depth depth;
	return layer::FillCapabilities((int)user, capabilities) ? ERROR_SUCCESS : original(user, flags, capabilities);
}

// The game sets the motors of every connected slot on each poll (zero when
// nothing shakes); a slot the controller layer holds keeps them for its
// thread, which turns its pad's motors.
template <int M>
DWORD WINAPI HookedSetState(DWORD user, XINPUT_VIBRATION* vibration)
{
	const SetState_t original = o_setState[M];
	if (t_passThrough || t_hookDepth > 0) return original(user, vibration);
	Depth depth;
	if (vibration && layer::SetMotors((int)user, *vibration)) return ERROR_SUCCESS;
	return original(user, vibration);
}

// Adds library M's calls to the set, by the library's full path in the
// system folder (so never the xinput1_3.dll proxy beside the game). A library
// that is not on this system, or that lacks one of the calls (not every XInput
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

std::optional<HookFailure> HookEveryXInputLibrary()
{
	wchar_t folder[MAX_PATH];
	const UINT n = GetSystemDirectoryW(folder, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		Log("virtual pads: no system folder to find XInput in (Win32 %lu); the pads are the game's", GetLastError());
		// Not a hook that failed: no MinHook status exists, and the status reads
		// "missing XInput (no system folder to find it in)".
		return HookFailure{"XInput (no system folder to find it in)", MH_UNKNOWN};
	}
	// Every library's calls or none: with only some hooked, a read would get
	// the virtual pads, the layer, the record and the pad filter through one
	// library and not another.
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

std::optional<HookFailure> InstallXInputHooks()
{
	static const std::optional<HookFailure> s_installed = HookEveryXInputLibrary();
	return s_installed;
}

void SetPadFilter(PadFilter filter)
{
	s_padFilter = filter;
}

PadSeen LastPadState(int slot)
{
	if (slot < 0 || slot >= kPadSlots) return {};
	const uint32_t seen = s_seen[slot];
	return {(seen & kSeenConnected) != 0, static_cast<uint16_t>(seen & 0xFFFF), static_cast<uint8_t>(seen >> kSeenStickShift & 0xF),
		(seen & kSeenLeftTrigger) != 0, (seen & kSeenRightTrigger) != 0};
}

uint64_t PadReads(int slot)
{
	return slot >= 0 && slot < kPadSlots ? s_reads[slot].load() : 0;
}

uint64_t NestedPadReads()
{
	return s_nestedReads;
}

int AnnounceDeviceChange()
{
	int posted = 0;
	EnumWindows(
		[](HWND hwnd, LPARAM count) -> BOOL {
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid != GetCurrentProcessId() || !PostMessageW(hwnd, WM_DEVICECHANGE, 0x0007 /* DBT_DEVNODES_CHANGED */, 0)) {
				return TRUE;
			}
			// Only the engine's own windows pass the change to its XInput
			// reader (FWindowsApplication's); the splash screen's
			// (SplashScreenClass) drops it.
			wchar_t name[32] = {};
			if (GetClassNameW(hwnd, name, 32) && wcscmp(name, L"UnrealWindow") == 0) ++*reinterpret_cast<int*>(count);
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
	XINPUT_STATE state{};
	const bool connected = read((DWORD)slot, &state) == ERROR_SUCCESS;
	t_passThrough = was;
	return connected;
}

void RegisterPadReadStatus()
{
	RegisterStatus("padReads", [] {
		std::string json = "[";
		for (int i = 0; i < kPadSlots; i++) json += (i ? "," : "") + std::to_string(s_reads[i].load());
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
}

} // namespace sfve::mod
