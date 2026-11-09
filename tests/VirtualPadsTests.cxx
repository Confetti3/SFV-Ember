// One pad read is one read, however many hooked XInput libraries it passes
// through (mod/VirtualPads). Windows 11's xinput1_3.dll (10.0.26200) calls
// xinput1_4.dll's XInputGetState, so the game's read entered the hooks twice;
// the outer call recorded the state the inner call's pad filter had emptied,
// and Ember's menu never saw a press. On a PC whose xinput1_3 reads on its
// own the nesting count stays 0 and the read count still has to be one.

#include <doctest/doctest.h>

#include "mod/VirtualPads.hxx"

#include <MinHook.h>

#include <windows.h>

using sfve::mod::InstallVirtualPads;
using sfve::mod::NestedPadReads;
using sfve::mod::PadReads;

namespace {

using GetState_t = DWORD(WINAPI*)(DWORD, void*);

struct XState {
	DWORD packet;
	WORD buttons;
	BYTE leftTrigger, rightTrigger;
	SHORT lx, ly, rx, ry;
};

int s_filterCalls = 0;
void CountingFilter(unsigned long, void*) { s_filterCalls++; }

} // namespace

TEST_CASE("a read through a hooked XInput library that calls another is one read")
{
	const MH_STATUS init = MH_Initialize();
	REQUIRE((init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED));
	REQUIRE_FALSE(InstallVirtualPads()); // empty: every XInput hook is in
	sfve::mod::SetPadFilter(&CountingFilter);

	wchar_t path[MAX_PATH];
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	REQUIRE(n > 0);
	wcscat_s(path, L"\\xinput1_3.dll"); // the library SFV's exe imports
	const HMODULE module = LoadLibraryW(path);
	if (!module) {
		MESSAGE("no xinput1_3.dll on this PC: nothing to read through");
		sfve::mod::SetPadFilter(nullptr);
		return;
	}
	const auto getState = reinterpret_cast<GetState_t>(GetProcAddress(module, "XInputGetState"));
	const auto getStateEx = reinterpret_cast<GetState_t>(GetProcAddress(module, MAKEINTRESOURCEA(100)));
	REQUIRE(getState);

	for (const GetState_t read : {getState, getStateEx}) {
		if (!read) continue;
		for (DWORD slot = 0; slot < sfve::mod::kPadSlots; slot++) {
			const uint64_t before = PadReads((int)slot);
			s_filterCalls = 0;
			XState state = {};
			const DWORD result = read(slot, &state);
			CHECK(PadReads((int)slot) == before + 1);
			// A pad that answered passed the filter once; an empty slot not at all.
			CHECK(s_filterCalls == (result == ERROR_SUCCESS ? 1 : 0));
		}
	}
	MESSAGE("nested calls passed through: " << NestedPadReads());
	sfve::mod::SetPadFilter(nullptr);
}
