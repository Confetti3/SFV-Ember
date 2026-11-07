#include "mod/ModConfig.hxx"

#include <stdlib.h>
#include <wchar.h>

#include <string>

namespace sfve::mod {

namespace {

ModConfig s_config;
const char* s_ignored = nullptr;
std::string s_unownedProblem;

// The value of `-name=value` in the command line, or nullptr.
const wchar_t* Find(const wchar_t* line, const wchar_t* name, wchar_t* out, size_t outLen)
{
	const size_t n = wcslen(name);
	for (const wchar_t* p = line; (p = wcsstr(p, name)) != nullptr; p += n) {
		if (p > line && p[-1] != L'-' && p[-1] != L'/') continue;
		if (p[n] != L'=') continue;
		const wchar_t* v = p + n + 1;
		size_t i = 0;
		while (v[i] && v[i] != L' ' && v[i] != L'"' && i + 1 < outLen) {
			out[i] = v[i];
			i++;
		}
		out[i] = 0;
		return out;
	}
	return nullptr;
}

long Number(const wchar_t* line, const wchar_t* name, long fallback)
{
	wchar_t buf[32];
	return Find(line, name, buf, 32) ? wcstol(buf, nullptr, 10) : fallback;
}

// Command-line text for the log, ASCII as it was typed (anything else as '?').
std::string Narrow(const wchar_t* text)
{
	std::string out;
	for (; *text; text++) out += *text < 0x80 ? (char)*text : '?';
	return out;
}

// A costume or colour number of an -sfve_unowned entry: 0 to 255, as a room
// carries them.
bool Variant(const std::wstring& text, int* value)
{
	if (text.empty() || text.size() > 3 || text.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
	*value = (int)wcstol(text.c_str(), nullptr, 10);
	return *value <= 0xFF;
}

// "CODE[:costume[:colour]]": a code of 1 to 8 capital letters or digits.
bool ParseUnowned(const std::wstring& entry, UnownedContent* out)
{
	std::vector<std::wstring> parts(1);
	for (wchar_t c : entry) {
		if (c == L':') parts.emplace_back();
		else parts.back() += c;
	}
	const std::wstring& code = parts[0];
	if (parts.size() > 3 || code.empty() || code.size() > 8) return false;
	for (wchar_t c : code) {
		if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) return false;
	}
	out->fighter.clear();
	for (wchar_t c : code) out->fighter += (char)c; // capitals and digits only
	return (parts.size() < 2 || Variant(parts[1], &out->costume)) && (parts.size() < 3 || Variant(parts[2], &out->colour));
}

} // namespace

const ModConfig& Config()
{
	return s_config;
}

void LoadConfigFromCommandLine(const wchar_t* line)
{
	ModConfig c;
	if (!line) line = L"";
	c.instance = (int)Number(line, L"sfve_instance", 0);
	wchar_t mode[32];
	if (Find(line, L"sfve_mode", mode, 32)) {
		if (!_wcsicmp(mode, L"inert")) c.mode = ModMode::Inert;
		else if (!_wcsicmp(mode, L"synctest")) c.mode = ModMode::SyncTest;
		else if (!_wcsicmp(mode, L"netplay")) c.mode = ModMode::Netplay;
	}
	c.player = (int)Number(line, L"sfve_player", c.instance == 0 ? 0 : 1);
	c.port = (uint16_t)Number(line, L"sfve_port", 7000 + 2 * c.instance);
	c.peerPort = (uint16_t)Number(line, L"sfve_peer_port", c.instance == 0 ? 7002 : 7000);
	c.delay = (int)Number(line, L"sfve_delay", 0);
	c.mashSeed = (uint32_t)Number(line, L"sfve_mash", 0);
	c.mashFrom = (int)Number(line, L"sfve_mash_from", 0);
	c.mashPad = Number(line, L"sfve_mash_pad", 0) != 0;
	c.distance = (int)Number(line, L"sfve_distance", 7);
	c.backgroundPlay = Number(line, L"sfve_background", 0) != 0;
	c.learn = Number(line, L"sfve_learn", 0) != 0;
	c.perturb = Number(line, L"sfve_perturb", 0) != 0;
	c.scheduleSeed = (uint32_t)Number(line, L"sfve_schedule", 0);
	c.lagMs = (int)Number(line, L"sfve_lag", 0);
	c.hashEvery = (int)Number(line, L"sfve_hash_every", 0);
	c.room = Number(line, L"sfve_room", 0) != 0;
	c.offlineDialogs = (int)Number(line, L"sfve_dialogs", 2);
	c.ownershipGate = Number(line, L"sfve_ownership", 1) != 0;
	c.gameOwnershipCheck = Number(line, L"sfve_game_ownership", 0) != 0;
	s_unownedProblem.clear();
	wchar_t unowned[512];
	if (Find(line, L"sfve_unowned", unowned, 512)) {
		std::wstring entry;
		for (const wchar_t* p = unowned;; p++) {
			if (*p && *p != L',') {
				entry += *p;
				continue;
			}
			UnownedContent mark;
			if (ParseUnowned(entry, &mark)) c.unowned.push_back(mark);
			else s_unownedProblem = Narrow(unowned);
			entry.clear();
			if (!*p) break;
		}
	}
	wchar_t pads[32];
	if (Find(line, L"sfve_pads", pads, 32)) {
		for (const wchar_t* p = pads; *p; p++) {
			if (*p >= L'0' && *p <= L'3') c.virtualPadMask |= 1u << (*p - L'0');
		}
	}
	s_ignored = nullptr;
	if (c.room && c.mode != ModMode::Netplay) {
		c.room = false;
		s_ignored = "-sfve_room=1 needs -sfve_mode=netplay; Ember's runtime is not started";
	}
	s_config = c;
}

const char* IgnoredSetting()
{
	return s_ignored;
}

const std::string& UnownedProblem()
{
	return s_unownedProblem;
}

const char* ModeName(ModMode mode)
{
	switch (mode) {
	case ModMode::Inert: return "inert";
	case ModMode::Probe: return "probe";
	case ModMode::SyncTest: return "synctest";
	case ModMode::Netplay: return "netplay";
	}
	return "?";
}

} // namespace sfve::mod
