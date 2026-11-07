#include "ember/PublicCommands.hxx"

#include "ember/EmberHost.hxx"
#include "ember/SfvCatalog.hxx"
#include "mod/ControlPipe.hxx"

#include "common/Country.hxx"
#include "common/GameCatalog.hxx"
#include "common/SkillBadge.hxx"
#include "netplay/Rankings.hxx"
#include "netplay/TableSetup.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "ui/NameMarks.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
using sf4e::netplay::IdentityOp;
using sf4e::netplay::IdentityRequest;
using sf4e::netplay::tournament::Command;

// The service the test commands use: the origin `bridge` looked up, and the
// ID it named. Pipe thread.
std::mutex s_mutex;
std::string s_origin, s_bridge;
// The tables the last create asked for, with its request, which `public
// enter` sets on the room it joins, as the Create public room page does.
std::uint64_t s_createRequest = 0;
std::optional<sf4e::netplay::TableSetup> s_createdRules;
// The commands' own request identities, far above the shell's (which counts
// from 1), so neither takes the other's answer for its own.
std::atomic<std::uint64_t> s_ticket{std::uint64_t(1) << 48};
std::atomic<std::uint64_t> s_request{std::uint64_t(1) << 48};

std::string Submit(facade::RuntimeCommand command)
{
	if (!EmberRuntimeRunning()) return "error Ember's runtime is not running (see the status's \"ember\")";
	if (!facade::SubmitRuntimeCommand(std::move(command))) return "error the runtime took no command (queue full)";
	return "ok submitted";
}

std::string Ask(IdentityOp op)
{
	IdentityRequest request;
	request.op = op;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		request.origin = s_origin;
		request.bridge = s_bridge;
	}
	request.ticket = ++s_ticket;
	facade::RuntimeCommand command;
	command.identity = std::move(request);
	const std::string answer = Submit(std::move(command));
	return answer.rfind("ok", 0) == 0 ? "ok " + std::to_string(s_ticket.load()) : answer;
}

// `ember-id`: makes this copy's Ember ID if it has none (the Ember ID screen's
// Create), else reads it.
std::string EmberId(const std::string&)
{
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	return Ask(snapshot->identity.state == "disabled" ? IdentityOp::Enable : IdentityOp::Status);
}

// `bridge <origin>`: looks a service up (Linked accounts' Look up).
std::string Bridge(const std::string& origin)
{
	if (origin.empty() || origin.find(' ') != std::string::npos) return "error usage: bridge <origin>";
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_origin = origin;
		s_bridge.clear();
	}
	return Ask(IdentityOp::BridgeInspect);
}

// `trust`: trusts the service `bridge` looked up (Linked accounts' Trust).
std::string Trust(const std::string&)
{
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		if (s_origin.empty() || snapshot->identity.inspected.origin != s_origin || snapshot->identity.inspected.id.empty())
			return "error look the service up with `bridge <origin>` first";
		s_bridge = snapshot->identity.inspected.id;
	}
	return Ask(IdentityOp::BridgeApprove);
}

std::string BridgeId()
{
	std::lock_guard<std::mutex> lock(s_mutex);
	return s_bridge;
}

// `public list`, `public create <rounds> <seconds> <name>`, `public join <room
// id>` (a ticket), as the Public rooms screen sends them; `public enter` joins
// with the last create's or ticket's admission, as that screen does once it is
// answered. A created room's tables play the rounds and time it names, first
// to no set length, without Instant rematch.
std::string Public(const std::string& args)
{
	const std::string bridge = BridgeId();
	if (bridge.empty()) return "error trust a service first (`bridge <origin>`, then `trust`)";
	const auto space = args.find(' ');
	const std::string verb = args.substr(0, space), rest = space == std::string::npos ? std::string() : args.substr(space + 1);
	facade::RuntimeCommand command;
	command.command.generation = facade::GetRuntimeSnapshotShared()->session.generation;
	Command& request = command.tournament;
	request.bridgeId = bridge;
	request.request = ++s_request;
	if (verb == "list" && rest.empty()) request.op = Command::Op::RoomList;
	else if (verb == "create") {
		std::istringstream in(rest);
		int rounds = 0, seconds = 0;
		std::string name;
		if (!(in >> rounds >> seconds) || !std::getline(in >> std::ws, name) || name.empty() || name.size() > 64)
			return "error usage: public create <rounds> <seconds> <name>";
		if (!ember::PlayableRoundCount(rounds) || !ember::PlayableRoundTime(seconds)) return "error SFV plays 1, 3, 5 or 7 rounds of 30, 60, 99 or 300 s";
		sf4e::room::Rules rules;
		rules.roundCount = (std::uint8_t)rounds;
		rules.roundTime = (std::uint16_t)seconds;
		rules.editionSelect = false;
		rules.instantRematch = false;
		rules.format = sf4e::room::SetFormat::Unlimited;
		request.op = Command::Op::RoomCreate;
		request.roomName = name;
		request.capacity = 8;
		std::lock_guard<std::mutex> lock(s_mutex);
		s_createRequest = request.request;
		s_createdRules = sf4e::netplay::TableSetup::Same(rules);
	} else if (verb == "join" && rest.size() == 32) {
		request.op = Command::Op::RoomTicket;
		request.roomId = rest;
	} else if (verb == "enter" && rest.empty()) {
		const auto snapshot = facade::GetRuntimeSnapshotShared();
		const auto& rooms = snapshot->publicRooms;
		if (!rooms.failure.empty() || rooms.admission.invitation.empty()) return "error no admission to join with";
		facade::RuntimeCommand join;
		join.command.kind = sf4e::netplay::CommandKind::JoinInvite;
		join.command.generation = snapshot->session.generation;
		join.command.invitation = rooms.admission.invitation;
		join.publicTicket = rooms.admission.ticket;
		{
			std::lock_guard<std::mutex> lock(s_mutex);
			if (rooms.answered == s_createRequest && s_createdRules) join.createdRules = s_createdRules;
		}
		return Submit(std::move(join));
	} else return "error usage: public list | public create <rounds> <seconds> <name> | public join <room id> | public enter";
	const std::string answer = Submit(std::move(command));
	return answer.rfind("ok", 0) == 0 ? "ok " + std::to_string(request.request) : answer;
}

// `ranking standing`, `ranking board`, `ranking take-part <country|-> <name>`
// and `ranking stop`, as the Rankings screen sends them (EMBER_LIBRARY.md
// section 28): the runtime asks the trusted service for SFV's standing and
// board, and takes part (listed under that name and flag) or stops.
std::string Ranking(const std::string& args)
{
	const std::string bridge = BridgeId();
	if (bridge.empty()) return "error trust a service first (`bridge <origin>`, then `trust`)";
	const auto space = args.find(' ');
	const std::string verb = args.substr(0, space), rest = space == std::string::npos ? std::string() : args.substr(space + 1);
	facade::RuntimeCommand command;
	command.command.generation = facade::GetRuntimeSnapshotShared()->session.generation;
	Command& request = command.tournament;
	request.bridgeId = bridge;
	if (verb == "standing" && rest.empty()) request.op = Command::Op::RankingStanding;
	else if (verb == "board" && rest.empty()) request.op = Command::Op::RankingBoard;
	else if (verb == "stop" && rest.empty()) {
		request.op = Command::Op::RankingChoose;
		request.ranked = false;
		request.request = ++s_request;
	} else if (verb == "take-part") {
		const auto gap = rest.find(' ');
		const std::string country = rest.substr(0, gap), name = gap == std::string::npos ? std::string() : rest.substr(gap + 1);
		if ((country != "-" && !sf4e::CountryCode::Parsed(country).Present()) || !sf4e::netplay::rankings::IsListingName(name))
			return "error usage: ranking take-part <country code or -> <name of 1 to 32 bytes>";
		request.op = Command::Op::RankingChoose;
		request.ranked = true;
		request.listingName = name;
		if (country != "-") request.listingCountry = country;
		request.request = ++s_request;
	} else return "error usage: ranking standing | ranking board | ranking take-part <country|-> <name> | ranking stop";
	const std::string answer = Submit(std::move(command));
	if (answer.rfind("ok", 0) != 0) return answer;
	return request.request ? "ok " + std::to_string(request.request) : answer;
}

nlohmann::json BadgeJson(const sf4e::SkillBadge& badge)
{
	static const char* const kinds[] = {"none", "placing", "class", "developer"};
	return {{"kind", kinds[(int)badge.kind]}, {"band", badge.band}, {"settled", badge.settled},
		{"name", badge.HasClass() ? sf4e::ui::ClassName(badge.band) : std::string()}};
}

// The status field `ranking`: the runtime's standing, board and last choice
// (netplay::rankings::Status), each member's mark in the room, and this
// copy's own online record (Profile's).
std::string RankingStatus()
{
	using Kind = sf4e::netplay::rankings::Standing::Kind;
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const auto& rankings = snapshot->rankings;
	const auto& standing = rankings.standing;
	static const char* const kinds[] = {"no_account", "opted_out", "ranked"};
	nlohmann::json board = nlohmann::json::array();
	if (rankings.board.value) {
		for (const auto& entry : *rankings.board.value)
			board.push_back({{"name", entry.listing.name}, {"country", entry.listing.country.ToString()}, {"band", entry.band},
				{"class", sf4e::ui::ClassName(entry.band)}, {"own", entry.own}});
	}
	nlohmann::json members = nlohmann::json::array();
	for (const auto& member : snapshot->Room().members) members.push_back({{"name", member.name}, {"badge", BadgeJson(member.badge)}});
	const auto& record = snapshot->preferences.record;
	nlohmann::json status = {
		{"bridge", rankings.bridge},
		{"emberId", rankings.emberId},
		{"standing", {
			{"loaded", standing.loaded}, {"loading", standing.loading}, {"error", standing.error}, {"revision", standing.revision},
			{"kind", kinds[(int)standing.value.kind]}, {"classesShown", standing.value.classesShown},
			{"badge", BadgeJson(standing.value.badge)}, {"listed", standing.value.listed},
			{"name", standing.value.listing.name}, {"country", standing.value.listing.country.ToString()},
			{"changeIn", standing.value.changeIn},
			{"ranked", standing.value.kind == Kind::Ranked},
		}},
		{"board", {{"loaded", rankings.board.loaded}, {"loading", rankings.board.loading}, {"error", rankings.board.error},
			{"revision", rankings.board.revision}, {"players", std::move(board)}}},
		{"choosing", rankings.choosing},
		{"chose", rankings.chose},
		{"choiceFailure", rankings.choiceFailure},
		{"members", std::move(members)},
		{"record", {{"available", record.available}, {"wins", record.wins}, {"losses", record.losses}, {"recent", record.recent.size()}}},
	};
	return status.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// `catalog-export <path>`: writes the catalog's pick space and build as SFV's
// room host takes it (EMBER_LIBRARY.md section 24).
std::string CatalogExport(const std::string& path)
{
	if (path.empty()) return "error usage: catalog-export <path>";
	const std::string text = PickSpaceJson();
	if (text.empty()) return "error the catalog is not filled yet";
	FILE* file = nullptr;
	if (fopen_s(&file, path.c_str(), "wb") != 0 || !file) return "error cannot write " + path;
	const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
	const bool closed = std::fclose(file) == 0;
	return written && closed ? "ok " + MatchBuildId() : "error the write failed";
}

std::string Status()
{
	const auto snapshot = facade::GetRuntimeSnapshotShared();
	const auto& identity = snapshot->identity;
	const auto& rooms = snapshot->publicRooms;
	nlohmann::json list = nlohmann::json::array();
	for (const auto& room : rooms.rooms) list.push_back({{"id", room.id}, {"name", room.name}, {"members", room.members}});
	nlohmann::json status = {
		{"identity", identity.state},
		{"emberId", identity.emberId},
		{"identityTicket", snapshot->identityTicket},
		{"identityOk", identity.ok},
		{"identityFailure", !snapshot->identityRefusal.empty() ? snapshot->identityRefusal : identity.failure},
		{"inspected", identity.inspected.id},
		{"bridge", BridgeId()},
		{"rooms", std::move(list)},
		{"listed", rooms.listed},
		{"loading", rooms.loading},
		{"error", rooms.error},
		{"admitting", rooms.admitting},
		{"answered", rooms.answered},
		{"failure", rooms.failure},
		{"admitted", rooms.admission.room.id},
	};
	return status.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace

void RegisterPublicCommands()
{
	mod::RegisterCommand("ember-id", EmberId);
	mod::RegisterCommand("bridge", Bridge);
	mod::RegisterCommand("trust", Trust);
	mod::RegisterCommand("public", Public);
	mod::RegisterCommand("ranking", Ranking);
	mod::RegisterCommand("catalog-export", CatalogExport);
	mod::RegisterStatus("public", Status);
	mod::RegisterStatus("ranking", RankingStatus);
}

} // namespace sfve::adapter
