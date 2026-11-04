#include "core/MatchLink.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace sfve {

namespace {

using json = nlohmann::json;

constexpr uint64_t kGeneration = 1; // one match per link for now
constexpr const char* kGrantKind = "sfve_grant";
constexpr const char* kSetupKind = "sfve_setup"; // the joiner's setup, answering the grant
constexpr const char* kStartKind = "sfve_start"; // the host has both setups: start
constexpr int64_t kMaxInputDelay = 15;

// JSON text; text that is not UTF-8 (a peer's, or a setup typed in) is
// replaced rather than thrown on.
std::string Text(const json& value)
{
	return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

// Typed reads of fields the peer or the helper sent: false when missing or
// of another type, never an exception.
bool Read(const json& object, const char* key, std::string* out)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_string()) return false;
	*out = it->get<std::string>();
	return true;
}

bool Read(const json& object, const char* key, uint64_t* out)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number_unsigned()) return false;
	*out = it->get<uint64_t>();
	return true;
}

bool Read(const json& object, const char* key, uint32_t* out)
{
	uint64_t wide = 0;
	if (!Read(object, key, &wide) || wide > UINT32_MAX) return false;
	*out = (uint32_t)wide;
	return true;
}

bool Read(const json& object, const char* key, int64_t* out)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_number_integer()) return false;
	*out = it->get<int64_t>();
	return true;
}

template <size_t N>
bool Read(const json& object, const char* key, std::array<uint8_t, N>* out)
{
	const auto it = object.find(key);
	if (it == object.end() || !it->is_array() || it->size() != N) return false;
	for (size_t i = 0; i < N; i++) {
		const json& byte = (*it)[i];
		if (!byte.is_number_unsigned() || byte.get<uint64_t>() > 0xFF) return false;
		(*out)[i] = (uint8_t)byte.get<uint64_t>();
	}
	return true;
}

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

void MatchLink::Host(int inputDelay, std::string setup)
{
	if (phase_ != Phase::Idle) return;
	if (setup.size() > kMaxSetup) return Fail("this side's setup is too long");
	host_ = true;
	hostDelay_ = inputDelay;
	setup_ = std::move(setup);
	phase_ = Phase::Opening;
	Command(json{{"type", "host"}, {"epoch", epoch_}, {"build", build_}});
}

void MatchLink::Join(const std::string& invitation, std::string setup)
{
	if (phase_ != Phase::Idle) return;
	if (setup.size() > kMaxSetup) return Fail("this side's setup is too long");
	host_ = false;
	setup_ = std::move(setup);
	phase_ = Phase::Opening;
	Command(json{{"type", "join"}, {"epoch", epoch_}, {"invitation", invitation}, {"build", build_}});
}

std::vector<std::string> MatchLink::TakeCommands()
{
	return std::exchange(commands_, {});
}

void MatchLink::Command(const json& command)
{
	commands_.push_back(Text(command));
}

void MatchLink::OnEvent(const std::string& eventJson)
{
	if (phase_ == Phase::Failed) return;
	// Events carry what the peer sent; a malformed one fails the match, it
	// never throws out of here.
	try {
		HandleEvent(json::parse(eventJson, nullptr, false));
	} catch (const json::exception& e) {
		Fail(std::string("a malformed event: ") + e.what());
	}
}

void MatchLink::HandleEvent(const json& event)
{
	std::string type;
	if (!event.is_object() || !Read(event, "type", &type)) return;
	uint64_t epoch = 0;
	if (event.contains("epoch") && (!Read(event, "epoch", &epoch) || epoch != epoch_)) return; // an older room's
	std::string peer;
	Read(event, "peer", &peer);
	if (type == "hosted" && phase_ == Phase::Opening && host_) {
		if (!Read(event, "invitation", &invitation_) || !Read(event, "room", &room_)) return Fail("the helper's room is malformed");
		phase_ = Phase::WaitingForPeer;
	} else if (type == "connected") {
		std::array<uint8_t, 16> room{};
		uint64_t control = 0;
		if (peer.empty() || !Read(event, "room", &room) || !Read(event, "control", &control)) {
			return Fail("the helper's connection is malformed");
		}
		OnConnected(peer, room, control);
	} else if (type == "message") {
		std::string payload;
		if (!peer.empty() && Read(event, "payload", &payload)) OnMessage(peer, payload);
	} else if (type == "game_ready") {
		uint64_t generation = 0, virtualPort = 0, maxPacket = 0;
		if (!Read(event, "generation", &generation) || !Read(event, "virtual_port", &virtualPort) ||
			!Read(event, "max_packet", &maxPacket) || virtualPort > 0xFFFF) {
			return Fail("the helper's link is malformed");
		}
		OnGameReady(peer, generation, (uint16_t)virtualPort, (size_t)maxPacket);
	} else if (type == "game_failed") {
		Fail("the game link failed: " + Text(event.value("reason", json())));
	} else if (type == "control_closed" || type == "peer_departed") {
		if (peer == peer_) Fail("the peer left");
	} else if (type == "room_closed") {
		Fail("the room closed");
	} else if (type == "error") {
		std::string message;
		Fail("the helper refused: " + (Read(event, "message", &message) ? message : Text(event)));
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
	Send(json{{"kind", kGrantKind}, {"generation", grant_.generation}, {"capability", grant_.capability},
		{"seed", grant_.seed}, {"delay", grant_.inputDelay}, {"setup", setup_}});
	Prepare(false);
}

void MatchLink::Send(const json& payload)
{
	Command(json{{"type", "send"}, {"epoch", epoch_}, {"peer", peer_}, {"message_id", nextMessageId_++},
		{"payload", Text(payload)}, {"control", control_}});
}

void MatchLink::OnMessage(const std::string& peer, const std::string& payload)
{
	if (!host_ && phase_ == Phase::Opening && peer_.empty()) {
		earlyGrant_ = payload; // kept until the room is known
		return;
	}
	if (peer != peer_) return;
	const json message = json::parse(payload, nullptr, false);
	std::string kind;
	if (!message.is_object() || !Read(message, "kind", &kind)) return;
	if (host_) {
		if (kind == kSetupKind) OnPeerSetup(message);
	} else if (kind == kGrantKind) {
		OnGrant(message);
	} else if (kind == kStartKind && phase_ == Phase::Linking && havePeerSetup_) {
		started_ = true;
		ReadyWhenComplete();
	}
}

// The joiner: the host's grant.
void MatchLink::OnGrant(const json& grant)
{
	if (phase_ != Phase::WaitingForGrant) return;
	int64_t delay = 0;
	if (!Read(grant, "generation", &grant_.generation) || !Read(grant, "capability", &grant_.capability) ||
		!Read(grant, "seed", &grant_.seed) || !Read(grant, "delay", &delay) || !Read(grant, "setup", &peerSetup_) ||
		grant_.generation == 0 || AllZero(grant_.capability) || delay < 0 || delay > kMaxInputDelay) {
		return Fail("the host's grant is malformed");
	}
	if (peerSetup_.size() > kMaxSetup) return Fail("the host's setup is too long");
	grant_.inputDelay = (int)delay;
	havePeerSetup_ = true;
	Send(json{{"kind", kSetupKind}, {"setup", setup_}});
	Prepare(true);
}

// The host: the joiner's answer to the grant. The host is ready with it and
// tells the joiner to start, so neither side starts a battle the other will
// not.
void MatchLink::OnPeerSetup(const json& message)
{
	if (havePeerSetup_ || phase_ != Phase::Linking) return;
	if (!Read(message, "setup", &peerSetup_)) return Fail("the joiner's setup is malformed");
	if (peerSetup_.size() > kMaxSetup) return Fail("the joiner's setup is too long");
	havePeerSetup_ = true;
	started_ = true;
	Send(json{{"kind", kStartKind}});
	ReadyWhenComplete();
}

// The host listens and the joiner dials, as Ember links P1 to each peer.
void MatchLink::Prepare(bool dial)
{
	localPort_ = deps_.reserveLocalPort();
	if (localPort_ == 0) return Fail("no free loopback port for GGPO");
	Command(json{{"type", "prepare_game"}, {"epoch", epoch_}, {"peer", peer_}, {"room", room_},
		{"generation", grant_.generation}, {"capability", grant_.capability}, {"local_port", localPort_},
		{"max_packet", maxPacket_}, {"dial", dial}});
	phase_ = Phase::Linking;
}

void MatchLink::OnGameReady(const std::string& peer, uint64_t generation, uint16_t virtualPort, size_t maxPacket)
{
	if (phase_ != Phase::Linking || peer != peer_ || generation != grant_.generation) return;
	if (virtualPort == 0 || maxPacket != maxPacket_) return Fail("the helper's link does not match the grant");
	endpoints_ = {host_ ? 0 : 1, localPort_, virtualPort, grant_.seed, grant_.inputDelay, {}};
	linked_ = true;
	ReadyWhenComplete();
}

void MatchLink::ReadyWhenComplete()
{
	if (!linked_ || !havePeerSetup_ || !started_ || phase_ != Phase::Linking) return;
	endpoints_.setups[0] = host_ ? setup_ : peerSetup_;
	endpoints_.setups[1] = host_ ? peerSetup_ : setup_;
	phase_ = Phase::Ready;
}

void MatchLink::Fail(std::string why)
{
	failure_ = std::move(why);
	phase_ = Phase::Failed;
}

std::string MatchLink::Describe() const
{
	return Text(json{{"phase", PhaseName(phase_)}, {"role", host_ ? "host" : "joiner"}, {"invitation", invitation_},
		{"failure", failure_}, {"localPort", endpoints_.localPort}, {"remotePort", endpoints_.remotePort}});
}

} // namespace sfve
