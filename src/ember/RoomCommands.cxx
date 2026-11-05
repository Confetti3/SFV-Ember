#include "ember/RoomCommands.hxx"

#include "ember/EmberHost.hxx"
#include "ember/SfvCatalog.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/NativeMenuLock.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
using sf4e::netplay::CommandKind;

// The pick the next Ready sends: the game's ids, by the catalog.
struct RoomPick {
	sf4e::wire::ConfirmedCharaConditions character{};
	int stage = 0;
	std::string text; // as the command named it, for the status
};

std::mutex s_mutex; // the pipe thread writes the pick, status reads it
std::optional<RoomPick> s_pick;

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
	int rounds = 3, seconds = 99;
	std::istringstream in(args);
	if (!args.empty() && !(in >> rounds >> seconds)) return "error usage: room [rounds] [seconds]";
	// A table rule Ember offers and SFV plays.
	if (!mod::PlayableRounds(rounds) || !sf4e::room::ValidRoundCount(rounds)) return "error SFV plays 1, 3, 5 or 7 rounds";
	if (!mod::RoundClockFrames(seconds) || !sf4e::room::ValidRoundTime(seconds)) return "error SFV rooms play rounds of 30, 60, 99 or 300 s";
	facade::RuntimeCommand command = CommandFor(CommandKind::HostRoom);
	command.preferences = facade::GetRuntimeSnapshotShared()->preferences;
	sf4e::room::Rules& rules = command.preferences.tableRules;
	rules.roundCount = (std::uint8_t)rounds;
	rules.roundTime = (std::uint16_t)seconds;
	rules.editionSelect = false; // SFV has no editions
	// SFV shows no prompt over the WIN yet, so its games always go back to the room.
	rules.instantRematch = false;
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
	RoomPick pick;
	if (!rules.stage.empty()) {
		const std::optional<int> stage = StageId(rules.stage);
		if (!stage) return "error that stage is not in this game's list";
		pick.stage = *stage;
	}
	pick.character.charaID = (unsigned char)*id;
	pick.character.costume = (unsigned char)(fighter.costume < 0 ? 0 : fighter.costume);
	pick.character.color = (unsigned char)(fighter.colour < 0 ? 0 : fighter.colour);
	pick.text = args;
	std::lock_guard lock(s_mutex);
	s_pick = pick;
	return "ok " + FighterCode(*id) + " costume " + std::to_string(pick.character.costume) + " colour " +
		std::to_string(pick.character.color) + " stage " + StageCode(pick.stage);
}

std::string Ready()
{
	facade::RuntimeCommand command = CommandFor(CommandKind::Ready);
	{
		std::lock_guard lock(s_mutex);
		if (!s_pick) return "error pick first (pick CODE[:costume[:colour]] [STAGE])";
		command.character = s_pick->character;
		command.stage = s_pick->stage;
	}
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
	std::lock_guard lock(s_mutex);
	status["pick"] = s_pick ? s_pick->text : std::string();
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
	mod::RegisterCommand("pick", Pick);
	mod::RegisterCommand("ready", [](const std::string&) { return Ready(); });
	mod::RegisterCommand("delay", Delay);
	mod::RegisterCommand("leave", [](const std::string&) { return Submit(CommandFor(CommandKind::LeaveRoom)); });
	mod::RegisterStatus("room", Status);
}

} // namespace sfve::adapter
