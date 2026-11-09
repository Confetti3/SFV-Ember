#include "mod/ModConfig.hxx"

#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include <string>

namespace sfve::mod {

namespace {

ModConfig s_config;
const char* s_ignored = nullptr;
std::string s_unownedProblem;
std::string s_uninstalledProblem;
std::string s_playerBuildIgnored;

// The value of `-name=value` in the command line, or nullptr.
const wchar_t* FindAny(const wchar_t* line, const wchar_t* name, wchar_t* out, size_t outLen)
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

// The options a player build reads: those its launcher passes the game
// (SfvLaunch's StartGame), with the one value it passes where it passes one.
// The rest are the tests' (ModConfig.hxx).
struct PlayerOption {
	const wchar_t* name;
	const wchar_t* value; // null: any value
};
constexpr PlayerOption kPlayerOptions[] = {{L"sfve_mode", L"netplay"}, {L"sfve_room", nullptr}, {L"sfve_dump_channel", nullptr}};

// The command line's options as this build reads them: a player build reads
// only kPlayerOptions and reads every other option, or another value, as
// absent, noting the ones it was given for the log.
class Options {
public:
	Options(const wchar_t* line, BuildKind kind, std::string& ignored) : line_(line), kind_(kind), ignored_(ignored)
	{
		ignored_.clear();
	}

	const wchar_t* Find(const wchar_t* name, wchar_t* out, size_t outLen)
	{
		const wchar_t* value = FindAny(line_, name, out, outLen);
		if (!value || Reads(name, value)) return value;
		std::string narrow;
		for (const wchar_t* c = name; *c; c++) narrow += (char)*c; // option names are ASCII
		ignored_ += (ignored_.empty() ? "-" : ", -") + narrow;
		return nullptr;
	}

	long Number(const wchar_t* name, long fallback)
	{
		wchar_t buf[32];
		return Find(name, buf, 32) ? wcstol(buf, nullptr, 10) : fallback;
	}

private:
	bool Reads(const wchar_t* name, const wchar_t* value) const
	{
		if (kind_ == BuildKind::Development) return true;
		for (const PlayerOption& option : kPlayerOptions) {
			if (!wcscmp(option.name, name)) return !option.value || !_wcsicmp(option.value, value);
		}
		return false;
	}

	const wchar_t* line_;
	BuildKind kind_;
	std::string& ignored_;
};

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

// "R:D:M", three nonzero hex handle values, digits only (no sign, no 0x);
// all zero (no channel) when the text is anything else.
DumpChannelHandles ParseDumpChannel(const wchar_t* text)
{
	uint64_t values[3] = {};
	const wchar_t* p = text;
	for (int i = 0; i < 3; i++) {
		if (!iswxdigit(p[0]) || (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))) return {};
		wchar_t* end = nullptr;
		values[i] = wcstoull(p, &end, 16);
		if (end == p || !values[i] || *end != (i < 2 ? L':' : L'\0')) return {};
		p = end + 1;
	}
	return {values[0], values[1], values[2]};
}

// A game code: 1 to 8 capital letters or digits.
bool ParseCode(const std::wstring& text, std::string* out)
{
	if (text.empty() || text.size() > 8) return false;
	for (wchar_t c : text) {
		if (!((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) return false;
	}
	out->clear();
	for (wchar_t c : text) *out += (char)c; // capitals and digits only
	return true;
}

// "CODE[:costume[:colour]]".
bool ParseUnowned(const std::wstring& entry, UnownedContent* out)
{
	std::vector<std::wstring> parts(1);
	for (wchar_t c : entry) {
		if (c == L':') parts.emplace_back();
		else parts.back() += c;
	}
	return parts.size() <= 3 && ParseCode(parts[0], &out->fighter) &&
		(parts.size() < 2 || Variant(parts[1], &out->costume)) && (parts.size() < 3 || Variant(parts[2], &out->colour));
}

// The comma-separated entries of `-name=...`, each parsed into `out`. An
// entry that does not parse is left out, and `problem` is then the whole
// value as the command line gave it (for the log); otherwise empty.
template <typename Entry, typename Parse>
void ReadEntries(Options& options, const wchar_t* name, Parse parse, std::vector<Entry>& out, std::string& problem)
{
	problem.clear();
	wchar_t value[512];
	if (!options.Find(name, value, 512)) return;
	std::wstring entry;
	for (const wchar_t* p = value;; p++) {
		if (*p && *p != L',') {
			entry += *p;
			continue;
		}
		Entry parsed;
		if (parse(entry, &parsed)) out.push_back(parsed);
		else problem = Narrow(value);
		entry.clear();
		if (!*p) break;
	}
}

} // namespace

const ModConfig& Config()
{
	return s_config;
}

void LoadConfigFromCommandLine(const wchar_t* line, BuildKind kind)
{
	ModConfig c;
	if (!line) line = L"";
	Options options(line, kind, s_playerBuildIgnored);
	c.instance = (int)options.Number(L"sfve_instance", 0);
	wchar_t mode[32];
	if (options.Find(L"sfve_mode", mode, 32)) {
		if (!_wcsicmp(mode, L"inert")) c.mode = ModMode::Inert;
		else if (!_wcsicmp(mode, L"synctest")) c.mode = ModMode::SyncTest;
		else if (!_wcsicmp(mode, L"netplay")) c.mode = ModMode::Netplay;
	}
	c.player = (int)options.Number(L"sfve_player", c.instance == 0 ? 0 : 1);
	c.port = (uint16_t)options.Number(L"sfve_port", 7000 + 2 * c.instance);
	c.peerPort = (uint16_t)options.Number(L"sfve_peer_port", c.instance == 0 ? 7002 : 7000);
	c.delay = (int)options.Number(L"sfve_delay", 0);
	c.script.seed = (uint32_t)options.Number(L"sfve_mash", 0);
	c.script.from = (int)options.Number(L"sfve_mash_from", 0);
	c.script.padBefore = options.Number(L"sfve_mash_pad", 0) != 0;
	c.script.script = options.Number(L"sfve_moves", 0) != 0 ? input::Script::Moves : input::Script::Buttons;
	c.practiceGauges = options.Number(L"sfve_practice", 0) != 0;
	c.distance = (int)options.Number(L"sfve_distance", 7);
	c.backgroundPlay = options.Number(L"sfve_background", 0) != 0;
	c.learn = options.Number(L"sfve_learn", 0) != 0;
	c.perturb = options.Number(L"sfve_perturb", 0) != 0;
	c.scheduleSeed = (uint32_t)options.Number(L"sfve_schedule", 0);
	c.lagMs = (int)options.Number(L"sfve_lag", 0);
	c.hashEvery = (int)options.Number(L"sfve_hash_every", 0);
	c.replayRegionsFrom = (int)options.Number(L"sfve_replay_regions", 0);
	c.room = options.Number(L"sfve_room", 0) != 0;
	c.offlineDialogs = (int)options.Number(L"sfve_dialogs", 2);
	c.ownershipGate = options.Number(L"sfve_ownership", 1) != 0;
	c.gameOwnershipCheck = options.Number(L"sfve_game_ownership", 0) != 0;
	c.crashCommand = options.Number(L"sfve_crash_command", 0) != 0;
	wchar_t channel[64];
	if (options.Find(L"sfve_dump_channel", channel, 64)) c.dumpChannel = ParseDumpChannel(channel);
	ReadEntries(options, L"sfve_unowned", &ParseUnowned, c.unowned, s_unownedProblem);
	ReadEntries(options, L"sfve_uninstalled", &ParseCode, c.uninstalled, s_uninstalledProblem);
	wchar_t pads[32];
	if (options.Find(L"sfve_pads", pads, 32)) {
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

const std::string& UninstalledProblem()
{
	return s_uninstalledProblem;
}

const std::string& PlayerBuildIgnored()
{
	return s_playerBuildIgnored;
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
