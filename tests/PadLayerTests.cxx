// The controller layer without SDL (mod/PadLayer; EMBER_LIBRARY.md section
// 48): which pads it leaves to the game, which slot each of its pads takes,
// and the slots as the game reads them through the XInput hooks.

#include <doctest/doctest.h>

#include "mod/PadLayer.hxx"
#include "mod/VirtualPads.hxx"

#include <MinHook.h>

#include <windows.h>

#include <Xinput.h>

#include <thread>

using namespace sfve::mod::layer;

namespace {

// SDL GUIDs in its standard form: bus, crc, vendor, 0, product, 0, version, driver data.
constexpr const char* kSteamVirtualGuid = "03000000de280000ff11000001000000";
constexpr const char* kDualShock4Guid = "030000004c050000cc09000000006800";
constexpr int kTypePs4 = 4; // any SDL_GamepadType; only equality matters

Pad MakePad(PadId id, const char* name, const char* path, const char* guid = kDualShock4Guid, int type = kTypePs4)
{
	Pad pad;
	pad.id = id;
	pad.identity = {guid, "", path, name};
	pad.sdlType = type;
	pad.gamepad = true;
	return pad;
}

const Pad& Find(const std::vector<Pad>& pads, PadId id)
{
	for (const Pad& pad : pads) {
		if (pad.id == id) return pad;
	}
	FAIL("no pad " << id);
	return pads.front();
}

} // namespace

TEST_CASE("an XInput device is known by its path")
{
	CHECK(XInputPath("\\\\?\\HID#VID_045E&PID_028E&IG_00#7&2d3e4f&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"));
	CHECK(XInputPath("\\\\?\\hid#vid_0f0d&pid_00c1&ig_01#8&1b2c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"));
	CHECK(XInputPath("XInput#2"));
	CHECK_FALSE(XInputPath("\\\\?\\HID#VID_054C&PID_09CC&MI_03#8&3a4b&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}"));
	CHECK_FALSE(XInputPath(""));
	CHECK_FALSE(XInputPath("XInput"));
}

TEST_CASE("the layer leaves XInput pads, Steam's pad and its twin to the game")
{
	Pad xbox = MakePad(1, "Xbox Controller", "\\\\?\\HID#VID_045E&PID_02FF&IG_00#1");
	Pad stick = MakePad(2, "Fight Stick (XInput mode)", "\\\\?\\HID#VID_0F0D&PID_00C1&IG_00#2");
	stick.xboxType = false;
	Pad typed = MakePad(3, "Xbox One pad", "\\\\?\\HID#VID_045E&PID_0B12#3");
	typed.xboxType = true;
	Pad ds4 = MakePad(4, "PS4 Controller", "\\\\?\\HID#VID_054C&PID_09CC&MI_03#4");
	Pad unmapped = MakePad(5, "Mystery Stick", "\\\\?\\HID#VID_1234&PID_5678#5");
	unmapped.gamepad = false;
	std::vector<Pad> pads = {xbox, stick, typed, ds4, unmapped};
	Classify(pads);
	CHECK(Find(pads, 1).route == Route::GameReads);
	CHECK(Find(pads, 2).route == Route::GameReads);
	CHECK(Find(pads, 3).route == Route::GameReads);
	CHECK(Find(pads, 4).route == Route::Waiting);
	CHECK(Find(pads, 5).route == Route::Unmapped);

	SUBCASE("Steam's virtual pad pairs with one physical pad of its type")
	{
		Pad steam = MakePad(6, "Steam Virtual Gamepad", "\\\\?\\HID#VID_28DE&PID_11FF&IG_00#6", kSteamVirtualGuid, kTypePs4);
		Pad second = MakePad(7, "PS4 Controller", "\\\\?\\HID#VID_054C&PID_09CC&MI_03#7");
		std::vector<Pad> withSteam = {ds4, steam, second};
		Classify(withSteam);
		CHECK(Find(withSteam, 6).route == Route::GameReads);
		CHECK(Find(withSteam, 4).route == Route::SteamTwin);
		CHECK(Find(withSteam, 7).route == Route::Waiting); // one Steam pad, one twin
	}
}

TEST_CASE("the layer's pads take slots no XInput pad holds")
{
	Plan plan;
	std::vector<Pad> pads = {MakePad(10, "PS4 Controller", "a"), MakePad(11, "Pro Controller", "b")};
	Classify(pads);
	// A real Xbox pad in slot 0.
	CHECK(plan.Update(pads, 0x1u));
	CHECK(Find(pads, 10).slot == 1);
	CHECK(Find(pads, 11).slot == 2);
	CHECK(Find(pads, 10).route == Route::Slotted);
	CHECK(plan.Held() == 0x6u);
	CHECK(plan.InSlot(1) == 10);

	SUBCASE("a pad keeps its slot when slot 0 frees")
	{
		Classify(pads); // the routes again, as the layer thread sets them before each plan
		CHECK_FALSE(plan.Update(pads, 0x0u));
		CHECK(Find(pads, 10).slot == 1);
		CHECK(Find(pads, 11).slot == 2);
	}
	SUBCASE("an XInput pad that takes a layer slot moves the layer's pad")
	{
		Classify(pads); // the routes again, as the layer thread sets them before each plan
		CHECK(plan.Update(pads, 0x3u)); // XInput gave a new Xbox pad slot 1
		CHECK(Find(pads, 10).slot == 3);
		CHECK(Find(pads, 11).slot == 2);
		CHECK(plan.Held() == 0xCu);
	}
	SUBCASE("a virtual test pad's slot is taken like a real one's")
	{
		Classify(pads); // the routes again, as the layer thread sets them before each plan
		CHECK(plan.Update(pads, 0x1u | 0x4u));
		CHECK(Find(pads, 10).slot == 1);
		CHECK(Find(pads, 11).slot == 3);
	}
	SUBCASE("with every slot taken a pad waits")
	{
		Classify(pads); // the routes again, as the layer thread sets them before each plan
		plan.Update(pads, 0xDu); // only slot 1 free
		CHECK(Find(pads, 10).slot == 1);
		CHECK(Find(pads, 11).route == Route::Waiting);
		CHECK(Find(pads, 11).slot == -1);
	}
}

TEST_CASE("a pad plugged in again goes back to its slot")
{
	Plan plan;
	std::vector<Pad> pads = {MakePad(1, "PS4 Controller", "first"), MakePad(2, "PS5 Controller", "second")};
	Classify(pads);
	plan.Update(pads, 0);
	REQUIRE(Find(pads, 1).slot == 0);
	REQUIRE(Find(pads, 2).slot == 1);

	// The second is unplugged: the first keeps slot 0.
	pads = {MakePad(1, "PS4 Controller", "first")};
	Classify(pads);
	CHECK(plan.Update(pads, 0));
	CHECK(Find(pads, 1).slot == 0);
	CHECK(plan.InSlot(1) == 0);

	// A new pad prefers a slot no pad last held over the one the second held.
	pads = {MakePad(1, "PS4 Controller", "first"), MakePad(3, "Pro Controller", "third")};
	Classify(pads);
	plan.Update(pads, 0);
	CHECK(Find(pads, 3).slot == 2);

	// The second comes back with a new SDL id and takes its slot again.
	pads = {MakePad(1, "PS4 Controller", "first"), MakePad(3, "Pro Controller", "third"), MakePad(7, "PS5 Controller", "second")};
	Classify(pads);
	plan.Update(pads, 0);
	CHECK(Find(pads, 7).slot == 1);
	CHECK(Find(pads, 1).slot == 0);
	CHECK(Find(pads, 3).slot == 2);
}

TEST_CASE("a slot's packet number changes with its state")
{
	SlotState slot;
	sf4e::gamepads::XInputFields fields;
	fields.buttons = 0x1000;
	Advance(slot, 5, fields, false);
	CHECK(slot.packet == 1);
	Advance(slot, 5, fields, false);
	CHECK(slot.packet == 1);
	fields.leftX = 1200;
	Advance(slot, 5, fields, false);
	CHECK(slot.packet == 2);
	Advance(slot, 5, fields, true);
	CHECK(slot.packet == 3);
	Advance(slot, 0, fields, true);
	CHECK(slot.packet == 4);
	CHECK(slot.id == 0);
	CHECK(slot.fields == sf4e::gamepads::XInputFields{});
	CHECK_FALSE(slot.guide);
}

TEST_CASE("SDL's sticks and triggers become XInput's")
{
	sf4e::gamepads::State state;
	state.buttons[static_cast<unsigned>(sf4e::gamepads::Button::South)] = true;
	state.buttons[static_cast<unsigned>(sf4e::gamepads::Button::Left)] = true;
	state.leftY = -32768;      // SDL: up is negative
	state.rightTrigger = 32767;
	const sf4e::gamepads::XInputFields fields = sf4e::gamepads::MapToXInput(state);
	CHECK(fields.buttons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_LEFT));
	CHECK(fields.leftY == 32767); // XInput: up is positive
	CHECK(fields.rightTrigger == 255);
	CHECK(fields.leftTrigger == 0);
}

TEST_CASE("the published slots answer the XInput hooks")
{
	std::array<SlotState, kSlots> slots{};
	sf4e::gamepads::XInputFields fields;
	fields.buttons = XINPUT_GAMEPAD_START;
	fields.leftTrigger = 200;
	fields.rightX = -4000;
	Advance(slots[3], 42, fields, true);
	Publish(slots);
	CHECK(HeldSlots() == 0x8u);

	XINPUT_STATE state{};
	CHECK_FALSE(FillState(2, &state, false));
	REQUIRE(FillState(3, &state, false));
	CHECK(state.dwPacketNumber == 1);
	CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_START);
	CHECK(state.Gamepad.bLeftTrigger == 200);
	CHECK(state.Gamepad.sThumbRX == -4000);
	REQUIRE(FillState(3, &state, true));
	CHECK(state.Gamepad.wButtons == (XINPUT_GAMEPAD_START | 0x0400)); // Ex adds the guide button

	XINPUT_CAPABILITIES capabilities{};
	CHECK_FALSE(FillCapabilities(0, &capabilities));
	REQUIRE(FillCapabilities(3, &capabilities));
	CHECK(capabilities.Type == XINPUT_DEVTYPE_GAMEPAD);
	CHECK(capabilities.SubType == XINPUT_DEVSUBTYPE_GAMEPAD);

	CHECK_FALSE(SetMotors(1, 100, 100));
	CHECK(SetMotors(3, 30000, 1000));
	CHECK(RequestedMotors()[3].id == 42);
	CHECK(RequestedMotors()[3].left == 30000);
	// Another pad in the slot: the motors asked of the last are dropped.
	Advance(slots[3], 43, fields, false);
	Publish(slots);
	CHECK(RequestedMotors()[3].id == 0);
	ClearSlots();
	CHECK(HeldSlots() == 0);
}

TEST_CASE("the game's XInput calls reach a layer slot through the hooks")
{
	const MH_STATUS init = MH_Initialize();
	REQUIRE((init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED));
	REQUIRE_FALSE(sfve::mod::InstallVirtualPads());

	wchar_t path[MAX_PATH];
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	REQUIRE(n > 0);
	wcscat_s(path, L"\\xinput1_3.dll"); // the library SFV's exe imports
	const HMODULE module = LoadLibraryW(path);
	if (!module) {
		MESSAGE("no xinput1_3.dll on this PC: nothing to read through");
		return;
	}
	using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
	using GetCapabilities_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
	using SetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
	const auto getState = reinterpret_cast<GetState_t>(GetProcAddress(module, "XInputGetState"));
	const auto getCapabilities = reinterpret_cast<GetCapabilities_t>(GetProcAddress(module, "XInputGetCapabilities"));
	const auto setState = reinterpret_cast<SetState_t>(GetProcAddress(module, "XInputSetState"));
	REQUIRE(getState);
	REQUIRE(getCapabilities);
	REQUIRE(setState);

	// Slot 3, with a packet number no real pad starts at.
	std::array<SlotState, kSlots> slots{};
	slots[3].packet = 0x5EED0000;
	sf4e::gamepads::XInputFields fields;
	fields.buttons = XINPUT_GAMEPAD_B;
	Advance(slots[3], 9, fields, false);
	Publish(slots);

	XINPUT_STATE state{};
	REQUIRE(getState(3, &state) == ERROR_SUCCESS);
	CHECK(state.dwPacketNumber == 0x5EED0001);
	CHECK(state.Gamepad.wButtons == XINPUT_GAMEPAD_B);
	CHECK(sfve::mod::LastPadState(3).connected);
	CHECK(sfve::mod::LastPadState(3).buttons == XINPUT_GAMEPAD_B);

	XINPUT_CAPABILITIES capabilities{};
	REQUIRE(getCapabilities(3, XINPUT_FLAG_GAMEPAD, &capabilities) == ERROR_SUCCESS);
	CHECK(capabilities.SubType == XINPUT_DEVSUBTYPE_GAMEPAD);

	XINPUT_VIBRATION vibration{12000, 34000};
	CHECK(setState(3, &vibration) == ERROR_SUCCESS);
	CHECK(RequestedMotors()[3].left == 12000);
	CHECK(RequestedMotors()[3].right == 34000);

	// The layer's own thread reads XInput itself: the layer's slot is not
	// there, whatever XInput has in slot 3.
	DWORD passedResult = ERROR_SUCCESS;
	XINPUT_STATE passed{};
	std::thread([&] {
		sfve::mod::PassPadReadsThrough();
		passedResult = getState(3, &passed);
	}).join();
	CHECK((passedResult != ERROR_SUCCESS || passed.dwPacketNumber != 0x5EED0001));
	// Beneath the hooks too.
	if (sfve::mod::PhysicalPadConnected(3)) MESSAGE("a real XInput pad sits in slot 3 on this PC");

	ClearSlots();
	CHECK((getState(3, &state) != ERROR_SUCCESS || state.dwPacketNumber != 0x5EED0001));
}
