#pragma once

// Local control channel for test automation: \\.\pipe\sfve-<instance>.
//
// A named pipe, not a socket: nothing here touches the network or the guard.
// One request line in, one response line out ("ok ..." or "error ..."). The
// pipe owns only ping, status (every registered field as one JSON object) and
// quit; each module registers its own commands and fields. Handlers run on the pipe
// thread; anything that must touch game state posts to the game thread.
//
// A player build (mod/BuildKind.hxx) answers ping and status only: nothing on
// the PC can drive its pads, read or change its memory, write files or trust
// a bridge through it. The pipe is this user's alone (the process token's
// default DACL) and this PC's (no remote clients), and a name another process
// already holds is reported, not shared.

#include "mod/BuildKind.hxx"

#include <functional>
#include <string>
#include <utility>

namespace sfve::mod {

using CommandHandler = std::function<std::string(const std::string& args)>;
using StatusField = std::function<std::string()>; // returns a JSON value

// Adds a command to the pipe's table, in any build kind (Answer judges which
// kind of build may run it).
void AddCommand(const std::string& name, CommandHandler handler);
// What the modules call to register a command: a player build answers none, so
// there it registers nothing and, being a template, leaves the handler's code
// (a lambda of its own) out of the binary with it.
template <typename Handler>
void RegisterCommand(const std::string& name, Handler&& handler)
{
	if constexpr (kBuildKind == BuildKind::Development) AddCommand(name, CommandHandler(std::forward<Handler>(handler)));
}
void RegisterStatus(const std::string& key, StatusField field);
void StartControlPipe(int instance);
// The pipe's answer to one request line, as this kind of build gives it.
std::string Answer(const std::string& line, BuildKind kind = kBuildKind);

std::string JsonString(const std::string& s);

} // namespace sfve::mod
