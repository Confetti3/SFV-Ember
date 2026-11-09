#pragma once

// Steam's record of its running client, checked before the game starts.
//
// steam_api finds the client through HKCU\Software\Valve\Steam\ActiveProcess:
// pid (the running steam.exe) and ActiveUser (the signed-in account). After
// some Steam restarts the record is left at 0 or at a process that is gone
// while Steam runs signed in; SFV's SteamAPI_Init then fails and the game
// crashes in its online code (StreetFighterV.exe+0x742A34). A clean restart,
// where the old client has fully exited before the new one starts, writes
// the record again (EMBER_LIBRARY.md section 36).

#include <functional>

namespace sfve::launcher {

enum class SteamSession {
	Ready,      // the record names the running steam.exe and a signed-in account
	Restarted,  // it did not; Steam was restarted (or started) and now it does
	Declined,   // it did not, and the player chose not to restart Steam
	Failed,     // Steam could not be brought to a usable record in time
};

// Asked before a running Steam is restarted, which interrupts its downloads,
// a game it runs and its voice chat: true lets the restart go ahead.
using RestartApproval = std::function<bool()>;

// Checks the record; when it is wrong and Steam runs, asks mayRestart, then
// shuts Steam down (steam.exe -shutdown), waits for every steam.exe to exit,
// starts Steam and waits for the record to name it and a signed-in account.
// A Steam that is not running is started without asking. A record naming
// the running client while nobody is signed in yet waits for the sign-in
// without a restart. Blocks for up to about two minutes. Logs each step.
SteamSession EnsureSteamSession(const RestartApproval& mayRestart);

} // namespace sfve::launcher
