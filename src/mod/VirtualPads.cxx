#include "mod/VirtualPads.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/XInputHooks.hxx"

#include <ctype.h>
#include <stdlib.h>

#include <deque>
#include <mutex>
#include <sstream>

namespace sfve::mod {

namespace {

struct Slot {
	bool enabled = false;
	std::deque<PadStep> steps;
	ULONGLONG stepStartedMs = 0;
	DWORD packet = 0;
	uint32_t lastButtons = 0xFFFFFFFF;
};

std::mutex s_mutex;
Slot s_slots[kPadSlots];

struct Name {
	const char* text;
	uint16_t bit;
};
constexpr Name kNames[] = {
	{"UP", 0x0001}, {"DOWN", 0x0002}, {"LEFT", 0x0004}, {"RIGHT", 0x0008}, {"START", 0x0010}, {"BACK", 0x0020},
	{"LS", 0x0040}, {"RS", 0x0080}, {"LB", 0x0100}, {"RB", 0x0200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000},
	{"Y", 0x8000},
};

} // namespace

bool FillVirtualPad(int slot, XINPUT_STATE* state)
{
	if (slot < 0 || slot >= kPadSlots || !state) return false;
	std::lock_guard lock(s_mutex);
	Slot& s = s_slots[slot];
	if (!s.enabled) return false;

	const ULONGLONG now = GetTickCount64();
	while (!s.steps.empty() && now - s.stepStartedMs >= s.steps.front().durationMs) {
		s.stepStartedMs += s.steps.front().durationMs;
		s.steps.pop_front();
	}
	if (s.steps.empty()) s.stepStartedMs = now;
	const PadStep held = s.steps.empty() ? PadStep{} : s.steps.front();

	*state = {};
	const uint32_t key = held.buttons | (uint32_t)held.leftTrigger << 16 | (uint32_t)held.rightTrigger << 24;
	if (key != s.lastButtons) {
		s.lastButtons = key;
		s.packet++;
	}
	state->dwPacketNumber = s.packet;
	state->Gamepad.wButtons = held.buttons;
	state->Gamepad.bLeftTrigger = held.leftTrigger;
	state->Gamepad.bRightTrigger = held.rightTrigger;
	return true;
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
