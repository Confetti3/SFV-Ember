#include "mod/HudText.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <wchar.h>

#include <mutex>

namespace sfve::mod {

namespace {

using HudInvokeFn = void (*)(uint8_t* movie, const GameString* method, float number, const GameString* text);

HudInvokeFn o_hudInvoke;

std::mutex s_mutex;
PlateName s_source;
// What the "hudText" status reports: each side's label as the game asked for
// it and as shown (UTF-8), and how many were replaced.
struct Shown {
	std::string asked, shown;
};
Shown s_shown[2];
uint64_t s_replaced;

bool Is(const GameString* s, const wchar_t* text)
{
	return s && s->data && s->count > 0 && wcsncmp(s->data, text, (size_t)s->count) == 0;
}

std::string Utf8(const wchar_t* text, size_t length)
{
	const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, (int)length, nullptr, 0, nullptr, nullptr);
	std::string out((size_t)(bytes > 0 ? bytes : 0), '\0');
	if (bytes > 0) WideCharToMultiByte(CP_UTF8, 0, text, (int)length, out.data(), bytes, nullptr, nullptr);
	return out;
}

std::string Utf8(const GameString* s)
{
	if (!s || !s->data || s->count <= 0) return {};
	return Utf8(s->data, wcsnlen(s->data, (size_t)s->count));
}

void h_HudInvoke(uint8_t* movie, const GameString* method, float number, const GameString* text)
{
	const int side = number == 0.f ? 0 : number == 1.f ? 1 : -1;
	if (side < 0 || !Is(method, L"SetPilotName")) {
		o_hudInvoke(movie, method, number, text);
		return;
	}
	std::wstring name;
	bool replace = false;
	{
		std::lock_guard lock(s_mutex);
		replace = s_source && s_source(side, name) && !name.empty();
	}
	// The game's own FString is never written: the call takes the mod's,
	// which lives until it returns (the player copies it).
	const GameString own{name.c_str(), (int32_t)name.size() + 1, (int32_t)name.size() + 1};
	const GameString* shown = replace ? &own : text;
	{
		std::lock_guard lock(s_mutex);
		Shown& seen = s_shown[side];
		seen.asked = Utf8(text);
		seen.shown = Utf8(shown);
		if (replace) ++s_replaced;
		Log("hud: side %d's plate \"%s\"%s%s%s", side + 1, seen.asked.c_str(), replace ? " shows \"" : "",
			replace ? seen.shown.c_str() : "", replace ? "\"" : "");
	}
	o_hudInvoke(movie, method, number, shown);
}

std::string StatusJson()
{
	std::lock_guard lock(s_mutex);
	std::string out = "{\"replaced\":" + std::to_string(s_replaced) + ",\"plates\":[";
	for (int side = 0; side < 2; ++side) {
		if (side) out += ",";
		out += "{\"asked\":" + JsonString(s_shown[side].asked) + ",\"shown\":" + JsonString(s_shown[side].shown) + "}";
	}
	return out + "]}";
}

} // namespace

bool InstallHudText(uintptr_t imageBase)
{
	const bool ok = HookCodeSite(imageBase, sfv::kHudInvoke, (void*)h_HudInvoke, (void**)&o_hudInvoke);
	Log("hud: %s", ok ? "the HUD's plate labels hooked" : "the HUD's plate labels NOT hooked");
	if (ok) RegisterStatus("hudText", StatusJson);
	return ok;
}

void SetPlateNames(PlateName source)
{
	std::lock_guard lock(s_mutex);
	s_source = std::move(source);
}

} // namespace sfve::mod
