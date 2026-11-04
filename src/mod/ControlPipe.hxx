#pragma once

// Local control channel for test automation: \\.\pipe\sfve-<instance>.
//
// A named pipe, not a socket: nothing here touches the network or the guard.
// One request line in, one response line out ("ok ..." or "error ..."). The
// pipe owns only ping, status (every registered field as one JSON object) and
// quit; each module registers its own commands and fields. Handlers run on the pipe
// thread; anything that must touch game state posts to the game thread.

#include <functional>
#include <string>

namespace sfve::mod {

using CommandHandler = std::function<std::string(const std::string& args)>;
using StatusField = std::function<std::string()>; // returns a JSON value

void RegisterCommand(const std::string& name, CommandHandler handler);
void RegisterStatus(const std::string& key, StatusField field);
void StartControlPipe(int instance);

std::string JsonString(const std::string& s);

} // namespace sfve::mod
