#include "mod/ModConfig.hxx"

#include <stdlib.h>
#include <wchar.h>

namespace sfve::mod {

namespace {

ModConfig s_config;

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
	c.distance = (int)Number(line, L"sfve_distance", 7);
	c.backgroundPlay = Number(line, L"sfve_background", 0) != 0;
	c.learn = Number(line, L"sfve_learn", 0) != 0;
	c.perturb = Number(line, L"sfve_perturb", 0) != 0;
	wchar_t pads[32];
	if (Find(line, L"sfve_pads", pads, 32)) {
		for (const wchar_t* p = pads; *p; p++) {
			if (*p >= L'0' && *p <= L'3') c.virtualPadMask |= 1u << (*p - L'0');
		}
	}
	s_config = c;
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
