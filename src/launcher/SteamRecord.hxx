#pragma once

// Steam's record of its running client, put right before the game starts.
//
// steam_api finds the client through HKCU\Software\Valve\Steam\ActiveProcess:
// pid (the running steam.exe) and ActiveUser (the signed-in account's id).
// Sometimes the record says 0 while Steam runs signed in (a Steam started
// where the registry it writes is not the player's, such as from a sandboxed
// process). SFV's SteamAPI_Init then fails, the game gets no online subsystem
// and stops in its LoggedIn (StreetFighterV.exe+0x742A34), a null it never
// tests for (EMBER_LIBRARY.md section 47).
//
// The launcher writes the record back: the running steam.exe's pid, and,
// when the record names no account, the account Steam signed in last
// (config\loginusers.vdf, MostRecent). With the record right, the game's own
// Steam start works as usual. Nothing is asked and Steam is never restarted;
// a record that is already right, or a Steam that is not running (the game's
// own start opens Steam), is left alone.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::launcher {

enum class SteamRecord {
	Current,     // the record names the running steam.exe and an account
	Repaired,    // it did not; the launcher wrote the right values
	NotRunning,  // no steam.exe runs: nothing to name
	Unrepaired,  // it is wrong and could not be put right (logged)
};

SteamRecord RepairSteamRecord();

// A running process, as the snapshot of them lists it.
struct ProcessEntry {
	uint32_t pid = 0;
	uint32_t session = 0; // its Windows session (another user's is another)
	std::wstring exe;     // the file name, as Windows lists it
};

// The pids of the Steam clients (steam.exe; steamwebhelper.exe and
// SteamService.exe do not count) among `processes` that run in `session`: the
// record is this user's, and another user's Steam (fast user switching keeps
// theirs running) is not the one the game talks to.
std::vector<uint32_t> SteamClientsInSession(const std::vector<ProcessEntry>& processes, uint32_t session);

// Steam's account id (the low 32 bits of the SteamID64) of the user
// loginusers.vdf marks MostRecent, or of its only user; nullopt when it names
// none, or several with none marked. Pure: the file's text in, the id out.
std::optional<uint32_t> MostRecentAccount(std::string_view loginUsersVdf);

} // namespace sfve::launcher
