// The controller layer (mod/PadLayer, mod/SdlPads; EMBER_LIBRARY.md section
// 49): which pads SFV's rule leaves to the game, the library's slots with the
// game's own as occupied, the slots as the game reads them through the
// XInput hooks, and SDL started with SFV's backends. The library's tests
// (gamepad_slots_test) hold the slot model itself.

#include <doctest/doctest.h>

#include "mod/PadLayer.hxx"
#include "mod/XInputHooks.hxx"

#include "common/SdlPadSource.hxx"

#include <MinHook.h>

#include <windows.h>

#include <Xinput.h>

#include <thread>
#include <vector>

using namespace sfve::mod::layer;
using sf4e::gamepads::Pad;
using sf4e::gamepads::Status;
using sf4e::gamepads::Type;

namespace {

// SDL GUIDs in its standard form: bus, crc, vendor, 0, product, 0, version, driver data.
constexpr const char* kSteamVirtualGuid = "03000000de280000ff11000001000000";
constexpr const char* kDualShock4Guid = "030000004c050000cc09000000006800";

Pad MakePad(PadId id, const char* name, const char* path, const char* guid = kDualShock4Guid, Type type = Type::PS4)
{
	Pad pad;
	pad.id = id;
	pad.identity = {guid, "", path, name};
	pad.type = type;
	pad.mapped = true;
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

std::vector<Pad> Classified(std::vector<Pad> pads)
{
	sf4e::gamepads::Classify(pads, {}, GameReadsItself);
	return pads;
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
	const Pad xbox = MakePad(1, "Xbox Controller", "\\\\?\\HID#VID_045E&PID_02FF&IG_00#1", kDualShock4Guid, Type::Unknown);
	const Pad stick = MakePad(2, "Fight Stick (XInput mode)", "\\\\?\\HID#VID_0F0D&PID_00C1&IG_00#2", kDualShock4Guid, Type::Standard);
	const Pad typed = MakePad(3, "Xbox One pad", "\\\\?\\HID#VID_045E&PID_0B12#3", kDualShock4Guid, Type::XboxOne);
	const Pad ds4 = MakePad(4, "PS4 Controller", "\\\\?\\HID#VID_054C&PID_09CC&MI_03#4");
	Pad unmapped = MakePad(5, "Mystery Stick", "\\\\?\\HID#VID_1234&PID_5678#5", kDualShock4Guid, Type::Unknown);
	unmapped.mapped = false;
	const std::vector<Pad> pads = Classified({xbox, stick, typed, ds4, unmapped});
	CHECK(Find(pads, 1).status == Status::GameRead);
	CHECK(Find(pads, 2).status == Status::GameRead);
	CHECK(Find(pads, 3).status == Status::GameRead);
	CHECK(Find(pads, 4).status == Status::NotInUse);
	CHECK(Find(pads, 5).status == Status::Unmapped);

	SUBCASE("Steam's virtual pad pairs with one physical pad of its type")
	{
		const Pad steam = MakePad(6, "Steam Virtual Gamepad", "\\\\?\\HID#VID_28DE&PID_11FF&IG_00#6", kSteamVirtualGuid);
		const Pad second = MakePad(7, "PS4 Controller", "\\\\?\\HID#VID_054C&PID_09CC&MI_03#7");
		const std::vector<Pad> withSteam = Classified({ds4, steam, second});
		CHECK(Find(withSteam, 6).status == Status::GameRead);
		CHECK(Find(withSteam, 4).status == Status::SteamTwin);
		CHECK(Find(withSteam, 7).status == Status::NotInUse); // one Steam pad, one twin
	}
}

TEST_CASE("the layer's pads take slots no XInput pad holds")
{
	sf4e::gamepads::Slots slots{GameReadsItself};
	const Pad xbox = MakePad(9, "Xbox Controller", "\\\\?\\HID#VID_045E&PID_02FF&IG_00#9", kDualShock4Guid, Type::XboxOne);
	// The Xbox pad holds XInput's slot 0, so slot 0 is occupied.
	const sf4e::gamepads::Inventory first =
		slots.Update({xbox, MakePad(10, "PS4 Controller", "a"), MakePad(11, "Pro Controller", "b")}, {}, {}, 0x1u);
	CHECK(slots.Changed());
	CHECK(Find(first.pads, 9).slot == -1);
	CHECK(Find(first.pads, 10).slot == 1);
	CHECK(Find(first.pads, 11).slot == 2);

	// The Xbox pad is unplugged: SFV's slot policy keeps each layer pad where it is.
	const sf4e::gamepads::Inventory freed = slots.Update({first.pads[1], first.pads[2]}, {}, {}, 0x0u);
	CHECK_FALSE(slots.Changed());
	CHECK(Find(freed.pads, 10).slot == 1);
	CHECK(Find(freed.pads, 11).slot == 2);
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

TEST_CASE("the published slots answer the XInput hooks")
{
	std::array<SlotState, sfve::mod::kPadSlots> slots{};
	sf4e::gamepads::XInputFields fields;
	fields.buttons = XINPUT_GAMEPAD_START;
	fields.leftTrigger = 200;
	fields.rightX = -4000;
	Advance(slots[3], 42, fields, true);
	Publish(slots);

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

	CHECK_FALSE(SetMotors(1, XINPUT_VIBRATION{100, 100}));
	CHECK(SetMotors(3, XINPUT_VIBRATION{30000, 1000}));
	CHECK(RequestedMotors()[3].id == 42);
	CHECK(RequestedMotors()[3].left == 30000);
	// Another pad in the slot: the motors asked of the last are dropped.
	Advance(slots[3], 43, fields, false);
	Publish(slots);
	CHECK(RequestedMotors()[3].id == 0);
	ClearSlots();
	CHECK_FALSE(FillState(3, &state, false));
}

TEST_CASE("the game's XInput calls reach a layer slot through the hooks")
{
	const MH_STATUS init = MH_Initialize();
	REQUIRE((init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED));
	REQUIRE_FALSE(sfve::mod::InstallXInputHooks());

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
	std::array<SlotState, sfve::mod::kPadSlots> slots{};
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

TEST_CASE("SDL starts for the layer with its XInput backends off, and stops")
{
	sf4e::gamepads::SdlPadSource source;
	const std::optional<std::string> failed = source.Start(sf4e::gamepads::SdlBackends::NotXInput);
	REQUIRE_MESSAGE(!failed, (failed ? *failed : std::string()));
	// The hints the pass-through relies on: SDL never reads XInput.
	CHECK_FALSE(SDL_GetHintBoolean(SDL_HINT_XINPUT_ENABLED, true));
	CHECK_FALSE(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_XBOX, true));
	CHECK(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_PS5, false));
	source.Pump();
	std::optional<std::vector<Pad>> pads = source.List();
	REQUIRE(pads);
	MESSAGE(pads->size() << " pad(s) on this PC");
	sf4e::gamepads::Classify(*pads, {}, GameReadsItself);
	for (const sf4e::gamepads::OpenFailure& failure : source.Hold(*pads)) MESSAGE("'" << failure.name << "' not opened: " << failure.error);
	for (const Pad& pad : *pads) {
		CHECK((pad.status != Status::NotInUse || !pad.available || source.Handle(pad.id)));
	}
	source.Stop();
	CHECK(SDL_WasInit(SDL_INIT_GAMEPAD) == 0);
}
