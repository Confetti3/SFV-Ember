#pragma once

// A two-player match over Ember's helper (sf4-net), linked the way SF4 Ember
// links one: the helper carries the game's GGPO datagrams between a loopback
// port on each side and the peer, over Iroh; the game process itself only
// ever talks to loopback.
//
// This is the game's side of that handshake, without I/O: helper events in
// (OnEvent), helper commands out (TakeCommands), so it is tested against
// scripted events. The host opens a room; when a peer connects it grants the
// match (a generation, a fresh capability for the link, the session seed and
// the input delay), sends the grant to the peer and asks its helper to listen.
// The joiner, on the grant, asks its helper to dial. When the helper reports
// the link ready, Endpoints() has what the RollbackSession needs.
//
// Each side also brings its setup: an opaque text the game defines (SFV:
// its player's fighter; the host's also names the stage). The host's goes
// in the grant; the joiner answers the grant with its own, and the host,
// with both, tells the joiner to start. The match is ready on a side once
// its helper link is and that side knows the host has both setups, so a
// joiner never starts a battle its host will not.
//
// Everything from the peer is checked field by field: a malformed or
// wrongly typed event fails the match, it never throws.
//
// The helper's protocol is Ember's (rust/sf4-net/src/service/protocol.rs):
// commands and events are JSON objects tagged by "type", fenced by a room
// epoch. The grant travels as the opaque payload of a "send".

#include <stddef.h>
#include <stdint.h>

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace sfve {

// GGPO's largest packet for this input size, as Ember bounds it (a 32-byte
// prefix, 64 queued frames of two players, one flag bit plus the input bits
// per frame); the helper refuses a link for a bigger one.
constexpr size_t GgpoMaximumPacket(size_t inputBytes)
{
	return 32 + (64 * (2 * inputBytes * 8 + 1) + 7) / 8;
}

class MatchLink {
public:
	enum class Phase {
		Idle,
		Opening,        // host or join sent
		WaitingForPeer, // hosted: the invitation is ready
		WaitingForGrant,
		Linking,        // prepare_game sent
		Ready,
		Failed
	};

	struct Endpoints {
		int localPlayer;      // 0 host (P1), 1 joiner (P2)
		uint16_t localPort;   // GGPO binds this loopback port
		uint16_t remotePort;  // the helper's loopback port for the peer
		uint32_t seed;
		int inputDelay;
		std::string setups[2]; // by player: the host's (P1), the joiner's (P2)
	};

	struct Dependencies {
		std::function<void(uint8_t* bytes, size_t size)> random;
		std::function<uint16_t()> reserveLocalPort; // a free loopback UDP port, or 0
	};

	MatchLink(std::string build, size_t maxPacket, Dependencies deps);

	// A setup longer than kMaxSetup fails the match.
	static constexpr size_t kMaxSetup = 256;

	void Host(int inputDelay, std::string setup);
	void Join(const std::string& invitation, std::string setup);
	void OnEvent(const std::string& eventJson);
	std::vector<std::string> TakeCommands();

	Phase GetPhase() const { return phase_; }
	const std::string& Invitation() const { return invitation_; }
	const std::string& Failure() const { return failure_; }
	const Endpoints& GetEndpoints() const { return endpoints_; }
	std::string Describe() const; // JSON for the status report

private:
	struct Grant {
		uint64_t generation = 0;
		std::array<uint8_t, 32> capability{};
		uint32_t seed = 0;
		int inputDelay = 0;
	};

	void Command(const nlohmann::json& command);
	void HandleEvent(const nlohmann::json& event);
	void OnConnected(const std::string& peer, const std::array<uint8_t, 16>& room, uint64_t control);
	void OnMessage(const std::string& peer, const std::string& payload);
	void OnGameReady(const std::string& peer, uint64_t generation, uint16_t virtualPort, size_t maxPacket);
	void OnGrant(const nlohmann::json& grant);
	void OnPeerSetup(const nlohmann::json& message);
	void Send(const nlohmann::json& payload);
	void ReadyWhenComplete();
	void Prepare(bool dial);
	void Fail(std::string why);

	const std::string build_;
	const size_t maxPacket_;
	const Dependencies deps_;
	bool host_ = false;
	std::string setup_;      // this side's
	std::string peerSetup_;  // the other side's: in the grant (joiner), in its answer (host)
	bool havePeerSetup_ = false;
	bool started_ = false;   // the host has both setups (the host itself, or its start message)
	bool linked_ = false;    // the helper reported the game link ready
	int hostDelay_ = 0;
	uint64_t epoch_ = 1;
	uint64_t nextMessageId_ = 2; // the helper delivers native messages numbered from 2 (id 1 stays wrapped)
	Phase phase_ = Phase::Idle;
	std::string invitation_;
	std::string peer_;
	std::string earlyGrant_; // a grant that came before this side's connected event
	uint64_t control_ = 0;
	std::array<uint8_t, 16> room_{};
	Grant grant_;
	uint16_t localPort_ = 0;
	Endpoints endpoints_{};
	std::string failure_;
	std::vector<std::string> commands_;
};

const char* PhaseName(MatchLink::Phase phase);

} // namespace sfve
