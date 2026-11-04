#include "core/MatchLink.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace sfve {

namespace {

using json = nlohmann::json;

constexpr uint64_t kGeneration = 1; // one match per link for now
constexpr const char* kGrantKind = "sfve_grant";

bool AllZero(const std::array<uint8_t, 32>& bytes)
{
	return std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0; });
}

} // namespace

const char* PhaseName(MatchLink::Phase phase)
{
	switch (phase) {
	case MatchLink::Phase::Idle: return "idle";
	case MatchLink::Phase::Opening: return "opening";
	case MatchLink::Phase::WaitingForPeer: return "waiting for a peer";
	case MatchLink::Phase::WaitingForGrant: return "waiting for the host";
	case MatchLink::Phase::Linking: return "linking";
	case MatchLink::Phase::Ready: return "ready";
	case MatchLink::Phase::Failed: return "failed";
	}
	return "?";
}

MatchLink::MatchLink(std::string build, size_t maxPacket, Dependencies deps)
	: build_(std::move(build)), maxPacket_(maxPacket), deps_(std::move(deps))
{
}

void MatchLink::Host(int inputDelay)
{
	if (phase_ != Phase::Idle) return;
	host_ = true;
	hostDelay_ = inputDelay;
	phase_ = Phase::Opening;
	Command(json{{"type", "host"}, {"epoch", epoch_}, {"build", build_}}.dump());
}

void MatchLink::Join(const std::string& invitation)
{
	if (phase_ != Phase::Idle) return;
	host_ = false;
	phase_ = Phase::Opening;
	Command(json{{"type", "join"}, {"epoch", epoch_}, {"invitation", invitation}, {"build", build_}}.dump());
}

std::vector<std::string> MatchLink::TakeCommands()
{
	return std::exchange(commands_, {});
}

void MatchLink::Command(const std::string& json)
{
	commands_.push_back(json);
}

void MatchLink::OnEvent(const std::string& eventJson)
{
	const json event = json::parse(eventJson, nullptr, false);
	if (!event.is_object() || phase_ == Phase::Failed) return;
	const std::string type = event.value("type", std::string());
	if (event.contains("epoch") && event.value("epoch", uint64_t(0)) != epoch_) return; // an older room's
	if (type == "hosted" && phase_ == Phase::Opening && host_) {
		invitation_ = event.value("invitation", std::string());
		room_ = event.at("room").get<std::array<uint8_t, 16>>();
		phase_ = Phase::WaitingForPeer;
	} else if (type == "connected") {
		OnConnected(event.value("peer", std::string()), event.at("room").get<std::array<uint8_t, 16>>(),
			event.value("control", uint64_t(0)));
	} else if (type == "message") {
		OnMessage(event.value("peer", std::string()), event.value("payload", std::string()));
	} else if (type == "game_ready") {
		OnGameReady(event.value("peer", std::string()), event.value("generation", uint64_t(0)),
			event.value("virtual_port", uint16_t(0)), event.value("max_packet", size_t(0)));
	} else if (type == "game_failed") {
		Fail("the game link failed: " + event.value("reason", json()).dump());
	} else if (type == "control_closed" || type == "peer_departed") {
		if (event.value("peer", std::string()) == peer_) Fail("the peer left");
	} else if (type == "room_closed") {
		Fail("the room closed");
	} else if (type == "error") {
		Fail("the helper refused: " + event.value("message", event.dump()));
	}
}

void MatchLink::OnConnected(const std::string& peer, const std::array<uint8_t, 16>& room, uint64_t control)
{
	if (!peer_.empty() && peer != peer_) return; // two players: the first peer is the match
	peer_ = peer;
	room_ = room;
	control_ = control;
	if (!host_) {
		if (phase_ != Phase::Opening) return;
		phase_ = Phase::WaitingForGrant;
		// The host's grant can arrive before this side's own connected event.
		if (!earlyGrant_.empty()) OnMessage(peer_, std::exchange(earlyGrant_, {}));
		return;
	}
	if (phase_ != Phase::WaitingForPeer) return;
	grant_.generation = kGeneration;
	deps_.random(grant_.capability.data(), grant_.capability.size());
	deps_.random(reinterpret_cast<uint8_t*>(&grant_.seed), sizeof(grant_.seed));
	grant_.inputDelay = hostDelay_;
	const json grant = {{"kind", kGrantKind}, {"generation", grant_.generation}, {"capability", grant_.capability},
		{"seed", grant_.seed}, {"delay", grant_.inputDelay}};
	Command(json{{"type", "send"}, {"epoch", epoch_}, {"peer", peer_}, {"message_id", nextMessageId_++},
		{"payload", grant.dump()}, {"control", control_}}.dump());
	Prepare(false);
}

void MatchLink::OnMessage(const std::string& peer, const std::string& payload)
{
	if (host_) return;
	if (phase_ == Phase::Opening && peer_.empty()) {
		earlyGrant_ = payload; // kept until the room is known
		return;
	}
	if (peer != peer_ || phase_ != Phase::WaitingForGrant) return;
	const json grant = json::parse(payload, nullptr, false);
	if (!grant.is_object() || grant.value("kind", std::string()) != kGrantKind) return;
	grant_.generation = grant.value("generation", uint64_t(0));
	grant_.capability = grant.value("capability", std::array<uint8_t, 32>{});
	grant_.seed = grant.value("seed", uint32_t(0));
	grant_.inputDelay = grant.value("delay", 0);
	if (grant_.generation == 0 || AllZero(grant_.capability)) return Fail("the host's grant is malformed");
	Prepare(true);
}

// The host listens and the joiner dials, as Ember links P1 to each peer.
void MatchLink::Prepare(bool dial)
{
	localPort_ = deps_.reserveLocalPort();
	if (localPort_ == 0) return Fail("no free loopback port for GGPO");
	Command(json{{"type", "prepare_game"}, {"epoch", epoch_}, {"peer", peer_}, {"room", room_},
		{"generation", grant_.generation}, {"capability", grant_.capability}, {"local_port", localPort_},
		{"max_packet", maxPacket_}, {"dial", dial}}.dump());
	phase_ = Phase::Linking;
}

void MatchLink::OnGameReady(const std::string& peer, uint64_t generation, uint16_t virtualPort, size_t maxPacket)
{
	if (phase_ != Phase::Linking || peer != peer_ || generation != grant_.generation) return;
	if (virtualPort == 0 || maxPacket != maxPacket_) return Fail("the helper's link does not match the grant");
	endpoints_ = {host_ ? 0 : 1, localPort_, virtualPort, grant_.seed, grant_.inputDelay};
	phase_ = Phase::Ready;
}

void MatchLink::Fail(std::string why)
{
	failure_ = std::move(why);
	phase_ = Phase::Failed;
}

std::string MatchLink::Describe() const
{
	return json{{"phase", PhaseName(phase_)}, {"role", host_ ? "host" : "joiner"}, {"invitation", invitation_},
		{"failure", failure_}, {"localPort", endpoints_.localPort}, {"remotePort", endpoints_.remotePort}}
		.dump();
}

} // namespace sfve
