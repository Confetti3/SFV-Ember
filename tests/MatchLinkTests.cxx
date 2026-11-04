#include <doctest/doctest.h>

#include "core/MatchLink.hxx"

#include <nlohmann/json.hpp>

#include <map>

using namespace sfve;
using json = nlohmann::json;

namespace {

constexpr size_t kMaxPacket = GgpoMaximumPacket(4);

MatchLink::Dependencies Deps(uint8_t fill, uint16_t port)
{
	return {[fill](uint8_t* bytes, size_t size) {
				for (size_t i = 0; i < size; i++) bytes[i] = uint8_t(fill + i);
			},
		[port] { return port; }};
}

// Stands in for two helpers and the network between them: answers each
// side's commands with the events Ember's helper sends.
struct ScriptedHelpers {
	MatchLink host{"sfv-test", kMaxPacket, Deps(1, 50001)};
	MatchLink joiner{"sfv-test", kMaxPacket, Deps(9, 50002)};
	std::map<std::string, json> lastCommand; // by side, then type
	const std::array<uint8_t, 16> room{7, 7, 7};

	void Pump()
	{
		for (int round = 0; round < 8; round++) {
			for (const std::string& c : host.TakeCommands()) Handle(true, json::parse(c));
			for (const std::string& c : joiner.TakeCommands()) Handle(false, json::parse(c));
		}
	}

	void Handle(bool fromHost, const json& c)
	{
		MatchLink& self = fromHost ? host : joiner;
		MatchLink& other = fromHost ? joiner : host;
		const std::string type = c.at("type");
		lastCommand[std::string(fromHost ? "host." : "joiner.") + type] = c;
		const uint64_t epoch = c.value("epoch", uint64_t(0));
		if (type == "host") {
			self.OnEvent(json{{"type", "hosted"}, {"epoch", epoch}, {"invitation", "sf4e1:invite"}, {"room", room}}.dump());
		} else if (type == "join") {
			self.OnEvent(json{{"type", "connected"}, {"epoch", epoch}, {"peer", "HOST"}, {"room", room}, {"control", 1}}.dump());
			other.OnEvent(json{{"type", "connected"}, {"epoch", epoch}, {"peer", "JOINER"}, {"room", room}, {"control", 2}}.dump());
		} else if (type == "send") {
			other.OnEvent(json{{"type", "message"}, {"epoch", epoch}, {"peer", fromHost ? "HOST" : "JOINER"},
				{"message_id", c.at("message_id")},
				{"payload", c.at("payload")}}.dump());
		} else if (type == "prepare_game") {
			self.OnEvent(json{{"type", "game_ready"}, {"epoch", epoch}, {"peer", c.at("peer")}, {"generation", c.at("generation")},
				{"virtual_port", fromHost ? 60001 : 60002}, {"max_packet", c.at("max_packet")}, {"route", "direct"},
				{"fixed_port", false}}.dump());
		}
	}
};

} // namespace

TEST_CASE("a host and a joiner link a match through their helpers")
{
	ScriptedHelpers h;
	h.host.Host(2, "KEN:1:2 S25");
	h.Pump();
	REQUIRE(h.host.GetPhase() == MatchLink::Phase::WaitingForPeer);
	CHECK(h.host.Invitation() == "sf4e1:invite");

	h.joiner.Join(h.host.Invitation(), "RYU");
	h.Pump();
	REQUIRE(h.host.GetPhase() == MatchLink::Phase::Ready);
	REQUIRE(h.joiner.GetPhase() == MatchLink::Phase::Ready);

	const MatchLink::Endpoints& a = h.host.GetEndpoints();
	const MatchLink::Endpoints& b = h.joiner.GetEndpoints();
	CHECK(a.localPlayer == 0);
	CHECK(b.localPlayer == 1);
	CHECK(a.localPort == 50001);
	CHECK(b.localPort == 50002);
	CHECK(a.remotePort == 60001);
	CHECK(b.remotePort == 60002);
	CHECK(a.seed == b.seed);
	CHECK(a.inputDelay == 2);
	CHECK(b.inputDelay == 2);
	// Both sides know both setups, by player.
	CHECK(a.setups[0] == "KEN:1:2 S25");
	CHECK(a.setups[1] == "RYU");
	CHECK(b.setups[0] == "KEN:1:2 S25");
	CHECK(b.setups[1] == "RYU");

	// The host listens, the joiner dials, both with the host's capability and
	// SFV's packet bound.
	const json& hp = h.lastCommand.at("host.prepare_game");
	const json& jp = h.lastCommand.at("joiner.prepare_game");
	CHECK(hp.at("dial") == false);
	CHECK(jp.at("dial") == true);
	CHECK(hp.at("capability") == jp.at("capability"));
	CHECK(hp.at("generation") == jp.at("generation"));
	CHECK(hp.at("max_packet") == 552);
	CHECK(hp.at("peer") == "JOINER");
	CHECK(jp.at("peer") == "HOST");
}

TEST_CASE("a grant that arrives before the joiner's connected event is kept")
{
	MatchLink joiner("sfv-test", kMaxPacket, Deps(9, 50002));
	joiner.Join("sf4e1:invite", "RYU");
	joiner.TakeCommands();
	std::array<uint8_t, 32> capability{};
	capability[0] = 1;
	const json grant = {{"kind", "sfve_grant"}, {"generation", 1}, {"capability", capability}, {"seed", 5}, {"delay", 2}};
	joiner.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "HOST"}, {"message_id", 1}, {"payload", grant.dump()}}.dump());
	CHECK(joiner.GetPhase() == MatchLink::Phase::Opening);
	joiner.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "HOST"}, {"room", std::array<uint8_t, 16>{1}}, {"control", 1}}.dump());
	CHECK(joiner.GetPhase() == MatchLink::Phase::Linking);
	const std::vector<std::string> commands = joiner.TakeCommands();
	REQUIRE(commands.size() == 2); // its setup for the host, then the link
	CHECK(json::parse(commands[0]).at("type") == "send");
	CHECK(json::parse(commands[1]).at("type") == "prepare_game");
}

TEST_CASE("events of another room epoch are ignored")
{
	MatchLink link("sfv-test", kMaxPacket, Deps(1, 50001));
	link.Host(1, "");
	link.TakeCommands();
	link.OnEvent(json{{"type", "hosted"}, {"epoch", 99}, {"invitation", "stale"}, {"room", std::array<uint8_t, 16>{}}}.dump());
	CHECK(link.GetPhase() == MatchLink::Phase::Opening);
	CHECK(link.Invitation().empty());
}

TEST_CASE("a failed link, a departed peer or a malformed grant fail the match")
{
	ScriptedHelpers h;
	h.host.Host(1, "");
	h.Pump();
	h.host.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "JOINER"}, {"room", h.room}, {"control", 2}}.dump());
	h.host.OnEvent(json{{"type", "game_failed"}, {"epoch", 1}, {"peer", "JOINER"}, {"generation", 1}, {"reason", "timeout"},
		{"route", ""}, {"max_packet", kMaxPacket}, {"max_datagram", 1200}}.dump());
	CHECK(h.host.GetPhase() == MatchLink::Phase::Failed);

	MatchLink joiner("sfv-test", kMaxPacket, Deps(9, 50002));
	joiner.Join("sf4e1:invite", "");
	joiner.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "HOST"}, {"room", h.room}, {"control", 1}}.dump());
	REQUIRE(joiner.GetPhase() == MatchLink::Phase::WaitingForGrant);
	const json grant = {{"kind", "sfve_grant"}, {"generation", 1}, {"capability", std::array<uint8_t, 32>{}}, {"seed", 5}, {"delay", 1}};
	joiner.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "HOST"}, {"message_id", 1}, {"payload", grant.dump()}}.dump());
	CHECK(joiner.GetPhase() == MatchLink::Phase::Failed); // an all-zero capability

	MatchLink left("sfv-test", kMaxPacket, Deps(9, 50002));
	left.Join("sf4e1:invite", "");
	left.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "HOST"}, {"room", h.room}, {"control", 1}}.dump());
	left.OnEvent(json{{"type", "peer_departed"}, {"epoch", 1}, {"peer", "HOST"}}.dump());
	CHECK(left.GetPhase() == MatchLink::Phase::Failed);
}

TEST_CASE("the host is ready only once the joiner's setup has come")
{
	MatchLink host("sfv-test", kMaxPacket, Deps(1, 50001));
	host.Host(1, "KEN");
	host.OnEvent(json{{"type", "hosted"}, {"epoch", 1}, {"invitation", "i"}, {"room", std::array<uint8_t, 16>{}}}.dump());
	host.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "JOINER"}, {"room", std::array<uint8_t, 16>{}}, {"control", 2}}.dump());
	host.OnEvent(json{{"type", "game_ready"}, {"epoch", 1}, {"peer", "JOINER"}, {"generation", 1}, {"virtual_port", 60001},
		{"max_packet", kMaxPacket}}.dump());
	CHECK(host.GetPhase() == MatchLink::Phase::Linking);
	// Another peer's setup, or a message of another kind, does not count.
	host.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "OTHER"}, {"message_id", 2},
		{"payload", json{{"kind", "sfve_setup"}, {"setup", "GUL"}}.dump()}}.dump());
	host.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "JOINER"}, {"message_id", 2},
		{"payload", json{{"kind", "chat"}, {"setup", "GUL"}}.dump()}}.dump());
	CHECK(host.GetPhase() == MatchLink::Phase::Linking);
	host.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "JOINER"}, {"message_id", 3},
		{"payload", json{{"kind", "sfve_setup"}, {"setup", "RYU"}}.dump()}}.dump());
	REQUIRE(host.GetPhase() == MatchLink::Phase::Ready);
	CHECK(host.GetEndpoints().setups[1] == "RYU");
}

TEST_CASE("a setup over the bound fails the match")
{
	MatchLink host("sfv-test", kMaxPacket, Deps(1, 50001));
	host.Host(1, std::string(MatchLink::kMaxSetup + 1, 'x'));
	CHECK(host.GetPhase() == MatchLink::Phase::Failed);

	MatchLink joiner("sfv-test", kMaxPacket, Deps(9, 50002));
	joiner.Join("sf4e1:invite", "RYU");
	joiner.OnEvent(json{{"type", "connected"}, {"epoch", 1}, {"peer", "HOST"}, {"room", std::array<uint8_t, 16>{}}, {"control", 1}}.dump());
	std::array<uint8_t, 32> capability{};
	capability[0] = 1;
	const json grant = {{"kind", "sfve_grant"}, {"generation", 1}, {"capability", capability}, {"seed", 5}, {"delay", 1},
		{"setup", std::string(MatchLink::kMaxSetup + 1, 'x')}};
	joiner.OnEvent(json{{"type", "message"}, {"epoch", 1}, {"peer", "HOST"}, {"message_id", 2}, {"payload", grant.dump()}}.dump());
	CHECK(joiner.GetPhase() == MatchLink::Phase::Failed);
}

TEST_CASE("the GGPO packet bound follows the input size as Ember computes it")
{
	CHECK(GgpoMaximumPacket(8) == 1064); // SF4's 8-byte input
	CHECK(GgpoMaximumPacket(4) == 552);  // SFV's packed input
}
