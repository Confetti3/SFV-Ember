#include "ember/RoomCommands.hxx"

#include "ember/EmberHost.hxx"
#include "ember/PublicCommands.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/Log.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/GameCatalog.hxx"
#include "common/NativeMenuLock.hxx"
#include "ember/RoomBattle.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "session/RoomQueries.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
using sf4e::netplay::CommandKind;

// A command for the runtime, tagged with the session the latest snapshot
// shows, as the shell tags a press with the screen it was drawn from.
facade::RuntimeCommand CommandFor(CommandKind kind)
{
	facade::RuntimeCommand command;
	command.command.kind = kind;
	command.command.generation = facade::GetRuntimeSnapshotShared()->session.generation;
	return command;
}

std::string Submit(facade::RuntimeCommand command)
{
	if (!EmberRuntimeRunning()) return "error Ember's runtime is not running (see the status's \"ember\")";
	if (!facade::SubmitRuntimeCommand(std::move(command))) return "error the runtime took no command (queue full)";
	return "ok submitted (the room's answer shows in the status's \"room\")";
}

std::string Host(const std::string& args)
{
	int rounds = 3, seconds = 99, rematch = 0, firstTo = 0;
	std::istringstream in(args);
	const char* usage = "error usage: room [rounds] [seconds] [instant rematch 0|1] [first to N, 0 unlimited]";
	if (!args.empty() && !(in >> rounds >> seconds)) return usage;
	if (!in.eof() && !(in >> rematch)) return usage;
	if (!in.eof() && !(in >> firstTo)) return usage;
	if (rematch < 0 || rematch > 1) return usage;
	if (!sf4e::room::ValidSetFormat(firstTo)) return "error the set is first to 1-10 games, or 0 for unlimited";
	// A table rule Ember offers and SFV plays (the catalog's).
	if (!ember::PlayableRoundCount(rounds)) return "error SFV plays 1, 3, 5 or 7 rounds";
	if (!ember::PlayableRoundTime(seconds)) return "error SFV rooms play rounds of 30, 60, 99 or 300 s";
	facade::RuntimeCommand command = CommandFor(CommandKind::HostRoom);
	command.preferences = facade::GetRuntimeSnapshotShared()->preferences;
	sf4e::room::Rules& rules = command.preferences.tableRules;
	rules.roundCount = (std::uint8_t)rounds;
	rules.roundTime = (std::uint16_t)seconds;
	rules.editionSelect = false; // SFV has no editions
	// A game goes on in place over its WIN only with Instant rematch on
	// (RoomBattle; off by default here, so a test asks for it).
	rules.instantRematch = rematch != 0;
	rules.format = static_cast<sf4e::room::SetFormat>(firstTo);
	command.preferences.lobby.roundCount = rounds;
	command.preferences.lobby.roundTime = seconds;
	command.preferences.lobby.editionSelect = false;
	if (!command.preferences.Valid()) return "error the room's settings are not valid";
	return Submit(std::move(command));
}

std::string Join(const std::string& invitation)
{
	if (invitation.empty() || invitation.find(' ') != std::string::npos) return "error usage: join <invitation>";
	facade::RuntimeCommand command = CommandFor(CommandKind::JoinInvite);
	command.command.invitation = invitation;
	return Submit(std::move(command));
}

// A seat at a table, as the room board's "Take P1's seat" sends it: Queue,
// fenced by the room and table revisions this PC last saw.
std::string Sit(const std::string& args)
{
	int table = 0, seat = -1;
	std::istringstream in(args);
	if (!args.empty() && !(in >> table >> seat)) return "error usage: sit [table] [seat: 0 P1, 1 P2, -1 the queue]";
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const sf4e::room::Snapshot& room = snapshot->Room();
	if (!room.roomEpoch || !room.localMember) return "error not in a room";
	if (table < 0 || table >= (int)room.tables.size() || seat < -1 || seat > 1) return "error no such table or seat";
	facade::RuntimeCommand command = CommandFor(CommandKind::RoomAction);
	sf4e::room::Action& action = command.roomAction;
	action.kind = sf4e::room::ActionKind::Queue;
	action.table = (std::uint8_t)table;
	action.seat = (std::int8_t)seat;
	action.protocolVersion = sf4e::room::ProtocolVersion;
	action.roomEpoch = room.roomEpoch;
	action.revision = room.revision;
	action.tableRevision = room.tables[(size_t)table].revision;
	return Submit(std::move(command));
}

// Watching a table, as the room board's Watch sends it: the table's games
// from the next one this member can join (a game in play is watched from the
// next; EMBER_LIBRARY.md section 25). Fenced as Sit is.
std::string WatchTable(const std::string& args)
{
	int table = 0;
	std::istringstream in(args);
	if (!args.empty() && !(in >> table)) return "error usage: watchtable [table]";
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const sf4e::room::Snapshot& room = snapshot->Room();
	if (!room.roomEpoch || !room.localMember) return "error not in a room";
	if (table < 0 || table >= (int)room.tables.size()) return "error no such table";
	facade::RuntimeCommand command = CommandFor(CommandKind::RoomAction);
	sf4e::room::Action& action = command.roomAction;
	action.kind = sf4e::room::ActionKind::Watch;
	action.table = (std::uint8_t)table;
	action.seat = -1;
	action.protocolVersion = sf4e::room::ProtocolVersion;
	action.roomEpoch = room.roomEpoch;
	action.revision = room.revision;
	action.tableRevision = room.tables[(size_t)table].revision;
	return Submit(std::move(command));
}

// The shell's Stop watching (ApplicationShell::LeaveWatchedGame): leaves the
// game this copy watches, as the runtime names it (the snapshot's
// watchedGame); the runtime decides what leaving it takes when it runs.
std::string StopWatching()
{
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	if (!snapshot->watchedGame.Active()) return "error no game is being watched";
	facade::RuntimeCommand command = CommandFor(CommandKind::RoomAction);
	command.leaveWatchedGame = snapshot->watchedGame;
	return Submit(std::move(command));
}

std::string Pick(const std::string& args)
{
	if (!EmberRuntimeRunning()) return "error Ember's runtime is not running, so the catalog is not filled";
	mod::FighterPick fighter;
	mod::MatchRules rules;
	if (!mod::ParseSetup(args, &fighter, &rules) || fighter.fighter.empty()) {
		return "error usage: pick CODE[:costume[:colour]] [STAGE]";
	}
	const std::optional<int> id = FighterId(fighter.fighter);
	if (!id) return "error that fighter is not in this game's list";
	// The one pick the shell edits too (SfvPick); a stage left out stays.
	PlayerPick pick = CurrentPick();
	if (!rules.stage.empty()) {
		const std::optional<int> stage = StageId(rules.stage);
		if (!stage) return "error that stage is not in this game's list";
		pick.stage = *stage;
	}
	pick.pick.fighter = *id;
	pick.pick.costume = fighter.costume < 0 ? 0 : fighter.costume;
	pick.pick.color = fighter.colour < 0 ? 0 : fighter.colour;
	// The store fits the pick to the catalog, as for the shell's picks.
	const PlayerPick asked = pick;
	pick = SetPick(pick);
	const bool fitted = pick.pick.costume != asked.pick.costume || pick.pick.color != asked.pick.color;
	if (fitted) mod::Log("room: pick %s fitted to %s", PickText(asked).c_str(), PickText(pick).c_str());
	return "ok " + PickText(pick) + (fitted ? " (fitted)" : "");
}

std::string Ready()
{
	facade::RuntimeCommand command = CommandFor(CommandKind::Ready);
	AddPickTo(command);
	return Submit(std::move(command));
}

// The post-match prompt's answers over the WIN (RoomBattle): "yes" is its
// Rematch, the room's own Ready, which the runtime parks as the choice the
// held frames carry; "no" is its Back to room.
std::string Rematch(const std::string& args)
{
	if (args == "no") {
		return DeclineRematch() ? "ok declined" : "error no WIN is waiting for an answer";
	}
	if (args != "yes") return "error usage: rematch yes|no";
	facade::RuntimeCommand command = CommandFor(CommandKind::Rematch);
	AddPickTo(command);
	return Submit(std::move(command));
}

std::string Delay(const std::string& args)
{
	char* end = nullptr;
	const long frames = std::strtol(args.c_str(), &end, 10);
	if (args.empty() || *end) return "error usage: delay <frames>";
	facade::RuntimeCommand command = CommandFor(CommandKind::ApplyDelay);
	command.selectedDelay = (int)frames;
	return Submit(std::move(command));
}

std::string Status()
{
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const auto presentation = facade::GetPresentationSnapshotShared();
	const auto& session = snapshot->session;
	nlohmann::json status = {
		{"room", sf4e::input::RoomStateName(session.room)},
		{"match", sf4e::input::MatchStateName(session.match)},
		{"host", session.isHost},
		{"slot", snapshot->localSlot},
		{"members", snapshot->Members().size()},
		{"helperReady", snapshot->helperReady},
		{"atMainMenu", snapshot->atMainMenu},
		{"controllerReady", snapshot->controllerReady},
		{"canReady", snapshot->canReady},
		{"invitation", !snapshot->invitation.empty()},
		{"notice", presentation->netplay.lastError},
		{"readyFailure", snapshot->readyFailure},
	};
	status["pick"] = EmberRuntimeRunning() ? PickText(CurrentPick()) : std::string();
	// The seat's table as the room keeps it: the set's score, its match and
	// the rule a game on it goes on in place by.
	const sf4e::room::Snapshot& room = snapshot->Room();
	// Watching (not a place: Queue and Watch both end it): the table this
	// member watches, whether from its next game, and the game it is in.
	for (std::size_t t = 0; t < room.tables.size(); t++) {
		const sf4e::room::Table& table = room.tables[t];
		const auto listed = [&](const auto& members) {
			return std::find(members.begin(), members.end(), room.localMember) != members.end();
		};
		if (!room.localMember || (!listed(table.spectators) && !listed(table.watchingNext))) continue;
		status["watching"] = {{"table", t}, {"next", listed(table.watchingNext)},
			{"game", snapshot->watchedGame.generation}, {"score", {table.score[0], table.score[1]}},
			{"generation", table.matchGeneration}, {"phase", (int)table.phase}};
	}
	for (std::size_t t = 0; t < room.tables.size(); t++) {
		// Everyone at a table sees how many watch it (the HUD's count).
		if (!room.tables[t].spectators.empty()) status["spectators"][std::to_string(t)] = room.tables[t].spectators.size();
	}
	// This member's sets won and lost in the room, as the Members card shows
	// them (EMBER_LIBRARY.md section 28).
	for (const auto& member : room.members) {
		if (room.localMember && member.id == room.localMember) status["sets"] = {{"won", member.setsWon}, {"lost", member.setsLost}};
	}
	const auto place = sf4e::room::PlaceOf(room, room.localMember);
	if (place.table >= 0 && place.table < (int)room.tables.size()) {
		const sf4e::room::Table& table = room.tables[(size_t)place.table];
		status["table"] = {{"score", {table.score[0], table.score[1]}}, {"generation", table.matchGeneration},
			{"phase", (int)table.phase}, {"instantRematch", table.rules.instantRematch},
			{"format", (int)table.rules.format}};
	}
	return status.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace

void RegisterRoomCommands()
{
	mod::RegisterCommand("room", Host);
	mod::RegisterCommand("invite", [](const std::string&) {
		const std::string invitation = facade::GetRuntimeSnapshotShared()->invitation;
		return invitation.empty() ? std::string("error no invitation (host a room first)") : "ok " + invitation;
	});
	mod::RegisterCommand("join", Join);
	mod::RegisterCommand("sit", Sit);
	mod::RegisterCommand("watchtable", WatchTable);
	mod::RegisterCommand("stopwatching", [](const std::string&) { return StopWatching(); });
	mod::RegisterCommand("pick", Pick);
	mod::RegisterCommand("ready", [](const std::string&) { return Ready(); });
	mod::RegisterCommand("rematch", Rematch);
	mod::RegisterCommand("delay", Delay);
	mod::RegisterCommand("leave", [](const std::string&) { return Submit(CommandFor(CommandKind::LeaveRoom)); });
	mod::RegisterStatus("room", Status);
	RegisterPublicCommands();
}

} // namespace sfve::adapter
