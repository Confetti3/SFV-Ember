#include "SteamRecord.hxx"

#include <windows.h>
#include <tlhelp32.h>

#include <spdlog/spdlog.h>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sfve::launcher {

namespace {

constexpr wchar_t kActiveProcess[] = L"Software\\Valve\\Steam\\ActiveProcess";
// SteamID64 = this base + the account id, for an individual public account.
constexpr uint64_t kIndividualBase = 76561197960265728ull;

struct Record {
	DWORD pid = 0;
	DWORD user = 0;
};

Record ReadRecord()
{
	Record record;
	DWORD size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, kActiveProcess, L"pid", RRF_RT_REG_DWORD, nullptr, &record.pid, &size);
	size = sizeof(DWORD);
	RegGetValueW(HKEY_CURRENT_USER, kActiveProcess, L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &record.user, &size);
	return record;
}

bool WriteValue(const wchar_t* name, DWORD value)
{
	return RegSetKeyValueW(HKEY_CURRENT_USER, kActiveProcess, name, REG_DWORD, &value, sizeof(value)) == ERROR_SUCCESS;
}

// Every running steam.exe (the client; steamwebhelper.exe and
// SteamService.exe do not count).
std::vector<DWORD> SteamProcesses()
{
	std::vector<DWORD> found;
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return found;
	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
		if (lstrcmpiW(entry.szExeFile, L"steam.exe") == 0) found.push_back(entry.th32ProcessID);
	}
	CloseHandle(snapshot);
	return found;
}

// Steam's folder, as Steam records it (SteamPath, forward slashes).
std::wstring SteamFolder()
{
	wchar_t path[MAX_PATH] = {};
	DWORD size = sizeof(path);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, path, &size) != ERROR_SUCCESS) {
		return {};
	}
	std::wstring folder = path;
	for (wchar_t& c : folder) {
		if (c == L'/') c = L'\\';
	}
	return folder;
}

std::string ReadFile(const std::wstring& path)
{
	std::ifstream file(path, std::ios::binary);
	std::ostringstream text;
	text << file.rdbuf();
	return text.str();
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
	}
	return true;
}

// The quoted tokens of a VDF text and its braces, in order.
std::vector<std::string_view> Tokens(std::string_view text)
{
	std::vector<std::string_view> tokens;
	for (size_t i = 0; i < text.size();) {
		const char c = text[i];
		if (c == '{' || c == '}') {
			tokens.push_back(text.substr(i, 1));
			++i;
		} else if (c == '"') {
			const size_t end = text.find('"', i + 1);
			if (end == std::string_view::npos) break;
			tokens.push_back(text.substr(i + 1, end - i - 1));
			i = end + 1;
		} else {
			++i;
		}
	}
	return tokens;
}

} // namespace

std::optional<uint32_t> MostRecentAccount(std::string_view vdf)
{
	// "users" { "<SteamID64>" { "AccountName" "..." ... "MostRecent" "1" } ... }
	const std::vector<std::string_view> tokens = Tokens(vdf);
	std::vector<uint64_t> users;
	std::optional<uint64_t> mostRecent;
	for (size_t i = 0; i + 1 < tokens.size(); ++i) {
		const std::string_view id = tokens[i];
		if (tokens[i + 1] != "{" || id.size() != 17) continue;
		uint64_t value = 0;
		bool digits = true;
		for (const char c : id) {
			digits = digits && c >= '0' && c <= '9';
			value = value * 10 + static_cast<uint64_t>(c - '0');
		}
		if (!digits || value <= kIndividualBase || value - kIndividualBase > 0xFFFFFFFFull) continue;
		users.push_back(value);
		// This user's keys, up to its closing brace.
		for (size_t j = i + 2; j + 1 < tokens.size() && tokens[j] != "}"; j += 2) {
			if (EqualsIgnoringCase(tokens[j], "MostRecent") && tokens[j + 1] == "1") mostRecent = value;
		}
	}
	if (!mostRecent && users.size() == 1) mostRecent = users.front();
	if (!mostRecent) return std::nullopt;
	return static_cast<uint32_t>(*mostRecent - kIndividualBase);
}

SteamRecord RepairSteamRecord()
{
	const Record record = ReadRecord();
	const std::vector<DWORD> running = SteamProcesses();
	if (running.empty()) {
		spdlog::info("Steam record: no steam.exe runs (pid {}, user {}); the game's start opens Steam", record.pid, record.user);
		return SteamRecord::NotRunning;
	}
	bool named = false;
	for (const DWORD pid : running) named = named || pid == record.pid;
	if (named && record.user != 0) return SteamRecord::Current;
	if (running.size() != 1) {
		spdlog::warn("Steam record: pid {} user {}, but {} steam.exe processes run; left as it is", record.pid, record.user, running.size());
		return SteamRecord::Unrepaired;
	}
	DWORD user = record.user;
	if (user == 0) {
		const std::wstring folder = SteamFolder();
		const std::optional<uint32_t> account = folder.empty() ? std::nullopt : MostRecentAccount(ReadFile(folder + L"\\config\\loginusers.vdf"));
		if (!account) {
			spdlog::warn("Steam record: pid {} user 0 while Steam runs as {}, and loginusers.vdf names no account; left as it is", record.pid, running.front());
			return SteamRecord::Unrepaired;
		}
		user = *account;
	}
	if (!WriteValue(L"pid", running.front()) || !WriteValue(L"ActiveUser", user)) {
		spdlog::warn("Steam record: could not write pid {} user {}", running.front(), user);
		return SteamRecord::Unrepaired;
	}
	spdlog::info("Steam record: was pid {} user {}, Steam runs as {}; wrote pid {} user {}", record.pid, record.user, running.front(), running.front(), user);
	return SteamRecord::Repaired;
}

} // namespace sfve::launcher
