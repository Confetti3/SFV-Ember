#include "mod/HudText.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameThread.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <windows.h>

#include <wchar.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace sfve::mod {

namespace {

// It returns what the movie player's invoke leaves in rax; the callers seen ignore it.
using HudInvokeFn = uint64_t (*)(uint8_t* movie, const GameString* method, float number, const GameString* text);

HudInvokeFn o_hudInvoke;

std::mutex s_mutex;
PlateName s_source;
// What the "hudText" status reports: each side's plate and marker as the game
// asked for them and as shown (UTF-8), and how many were replaced.
struct Shown {
	std::string asked, shown;
};
// The two calls that show a side's label: the plate on its life bar, and the
// marker over its fighter at a round's start.
struct Label {
	const wchar_t* method;
	const char* what;
};
constexpr Label kLabels[] = {{L"SetPilotName", "plate"}, {L"SetCursorText", "marker"}};
Shown s_shown[2][2];
uint64_t s_replaced;

// The photo booth's HUD switch (InstallHudHide): every HUD part the
// PostUpdate hook has run for in this battle, and whether their movies are
// to be hidden. A part list past kMaxHudParts is not grown, and a part with
// more movies than kMaxPartMovies is read as having none (the layout comes
// from reading the exe, sfv::kHudPartPostUpdate).
constexpr size_t kMaxHudParts = 64;
constexpr size_t kMaxPartMovies = 16;
using HudPartFn = void (*)(uint8_t* part);
using MovieSetVisibleFn = void (*)(uint8_t* player, bool visible);
HudPartFn o_hudPartPostUpdate;
MovieSetVisibleFn s_movieSetVisible;
std::mutex s_partsMutex;
std::vector<uint8_t*> s_hudParts;
std::atomic<bool> s_hudHidden;
std::atomic<bool> s_hudHideInstalled;

// Gives each of `part`'s movies `visible` (the game's own call). False when
// the part's movie list cannot be read.
bool SetPartMovies(uint8_t* part, bool visible)
{
	namespace hp = sfv::hudpart;
	uint8_t** begin = nullptr;
	uint8_t** end = nullptr;
	if (!TryRead(part + hp::kMoviesBegin, &begin) || !TryRead(part + hp::kMoviesEnd, &end)) return false;
	if (end < begin || size_t(end - begin) > kMaxPartMovies) return false;
	for (uint8_t** at = begin; at < end; ++at) {
		uint8_t* movie = nullptr;
		uint8_t* player = nullptr;
		if (!TryRead(at, &movie) || !movie || !TryRead(movie + hp::kMoviePlayer, &player) || !player) continue;
		s_movieSetVisible(player, visible);
	}
	return true;
}

// What the part last showed (the game's own answer, +0x10C).
bool PartShown(uint8_t* part)
{
	int32_t shown = 0;
	return TryRead(part + sfv::hudpart::kShown, &shown) && shown == 1;
}

void h_HudPartPostUpdate(uint8_t* part)
{
	o_hudPartPostUpdate(part);
	if (!part) return;
	{
		std::lock_guard lock(s_partsMutex);
		if (std::find(s_hudParts.begin(), s_hudParts.end(), part) == s_hudParts.end() && s_hudParts.size() < kMaxHudParts) {
			s_hudParts.push_back(part);
		}
	}
	// After the game's update, which shows its movies only when its answer
	// changes: hidden they stay, whatever it answered.
	if (s_hudHidden.load()) SetPartMovies(part, false);
}

// The photo booth's probe (HudCall, SetHudCallLog): the movie that last named
// a label (SetPilotName or SetCursorText, the HUD's own calls; the marker is
// named again at each round's start), and whether every invoke is logged.
std::atomic<uint8_t*> s_movie;
std::atomic<bool> s_logCalls;

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

uint64_t h_HudInvoke(uint8_t* movie, const GameString* method, float number, const GameString* text)
{
	if (s_logCalls.load()) {
		Log("hud: invoke %s(%g, \"%s\") on movie %p", Utf8(method).c_str(), (double)number, Utf8(text).c_str(), (void*)movie);
	}
	for (const Label& candidate : kLabels) {
		if (movie && Is(method, candidate.method)) s_movie.store(movie);
	}
	const int side = number == 0.f ? 0 : number == 1.f ? 1 : -1;
	int label = -1;
	for (int i = 0; side >= 0 && i < 2; ++i)
		if (Is(method, kLabels[i].method)) label = i;
	if (label < 0) {
		return o_hudInvoke(movie, method, number, text);
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
		Shown& seen = s_shown[label][side];
		const std::string asked = Utf8(text), now = Utf8(shown);
		if (replace) ++s_replaced;
		// Once per change: the marker is named again each round.
		if (asked != seen.asked || now != seen.shown) {
			Log("hud: side %d's %s \"%s\"%s%s%s", side + 1, kLabels[label].what, asked.c_str(), replace ? " shows \"" : "",
				replace ? now.c_str() : "", replace ? "\"" : "");
		}
		seen.asked = asked;
		seen.shown = now;
	}
	return o_hudInvoke(movie, method, number, shown);
}

std::string StatusJson()
{
	std::lock_guard lock(s_mutex);
	std::string out = "{\"replaced\":" + std::to_string(s_replaced);
	for (int label = 0; label < 2; ++label) {
		out += std::string(",\"") + kLabels[label].what + "s\":[";
		for (int side = 0; side < 2; ++side) {
			const Shown& seen = s_shown[label][side];
			out += std::string(side ? "," : "") + "{\"asked\":" + JsonString(seen.asked) + ",\"shown\":" + JsonString(seen.shown) + "}";
		}
		out += "]";
	}
	return out + "}";
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

bool HudCall(const std::wstring& method, float number, const std::wstring& text)
{
	uint8_t* movie = s_movie.load();
	if (!o_hudInvoke || !movie || method.empty()) return false;
	if (!GameThreadId() || GetCurrentThreadId() != GameThreadId()) {
		Log("hud: a HUD call was asked off the game thread; refused");
		return false;
	}
	// Strings the mod owns, as the label hook's own: they live until the
	// call returns (the player copies them). No text is UE4's empty FString.
	const GameString name{method.c_str(), (int32_t)method.size() + 1, (int32_t)method.size() + 1};
	const GameString words{text.empty() ? nullptr : text.c_str(), text.empty() ? 0 : (int32_t)text.size() + 1,
		text.empty() ? 0 : (int32_t)text.size() + 1};
	Log("hud: booth call %s(%g, \"%s\") on movie %p", Utf8(&name).c_str(), (double)number, Utf8(&words).c_str(), (void*)movie);
	o_hudInvoke(movie, &name, number, &words);
	return true;
}

void ForgetHudMovie()
{
	s_movie.store(nullptr);
}

bool HudMovieKnown()
{
	return s_movie.load() != nullptr;
}

bool InstallHudHide(uintptr_t imageBase)
{
	if (s_hudHideInstalled.load()) return true;
	s_movieSetVisible = reinterpret_cast<MovieSetVisibleFn>(imageBase + sfv::kMovieSetVisible.rva);
	const bool ok = HookCodeSite(imageBase, sfv::kHudPartPostUpdate, (void*)h_HudPartPostUpdate, (void**)&o_hudPartPostUpdate);
	s_hudHideInstalled.store(ok);
	Log("hud: %s", ok ? "the HUD parts' updates hooked (booth hudhide)" : "the HUD parts' updates NOT hooked; booth hudhide does nothing");
	return ok;
}

int SetHudHidden(bool hidden, bool now)
{
	if (!s_hudHideInstalled.load()) return -1;
	if (!GameThreadId() || GetCurrentThreadId() != GameThreadId()) {
		Log("hud: the HUD switch was asked off the game thread; refused");
		return -1;
	}
	s_hudHidden.store(hidden);
	std::vector<uint8_t*> parts;
	if (now) {
		std::lock_guard lock(s_partsMutex);
		parts = s_hudParts;
	}
	int reached = 0;
	for (uint8_t* part : parts) {
		if (SetPartMovies(part, hidden ? false : PartShown(part))) reached++;
	}
	Log("hud: the HUD %s (%d part(s))", hidden ? "hidden" : "shown as the game has it", reached);
	return reached;
}

void ForgetHudParts()
{
	std::lock_guard lock(s_partsMutex);
	s_hudParts.clear();
}

int HudPartsKnown()
{
	std::lock_guard lock(s_partsMutex);
	return (int)s_hudParts.size();
}

bool HudHidden()
{
	return s_hudHidden.load();
}

void SetHudCallLog(bool on)
{
	s_logCalls.store(on);
	Log("hud: every invoke %s", on ? "logged" : "no longer logged");
}

} // namespace sfve::mod
