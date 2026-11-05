#pragma once

// SFV's desync check behind Ember's room client (the library's
// session/BattleVerifier.hxx; MakeBattleVerifier is defined in
// SfvVerification.cxx). A player publishes one overall hash per confirmed
// checkpoint of its rollback session: the peers' hash RollbackSession keeps
// every kCheckpointInterval frames. Its flow, chara0 and chara1 are 0, and no
// legacy (v1) snapshot is ever sent. Every client compares a remote hash once
// its own checkpoint of that frame is confirmed; a mismatch ends the fight
// (the adapter's Battle::Fail).
//
// CheckpointExchange is the check itself, over one rollback session, apart
// from the room client so it is tested against real peers.

#include "core/RollbackSession.hxx"

#include "session/sf4e__SessionProtocol.hxx"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace sfve::adapter {

class CheckpointExchange {
public:
	struct Mismatch {
		int frame;
		std::uint64_t local;
		std::uint64_t remote;
	};
	// Sends one hash to the room; false when the room did not take it (it is
	// sent again on the next step).
	using Publish = std::function<bool(const sf4e::SessionProtocol::BattleHashV2&)>;

	// One step of the room client: publishes, in frame order, the checkpoints
	// confirmed since the last step (none when `publish` is null: a client
	// that watches publishes nothing), and compares and erases the remote
	// hashes whose checkpoint is confirmed here. Returns the earliest mismatch
	// of this step. A session whose confirmed frame went back (a new battle)
	// starts the check over.
	std::optional<Mismatch> Step(const RollbackSession& session, const Publish* publish,
		std::map<int, sf4e::SessionProtocol::BattleHashV2>& remote);
	void Reset();

	// How many confirmed checkpoints are kept for remote hashes still to come.
	static constexpr std::size_t kKeptCheckpoints = 64;

private:
	struct Checkpoint {
		int ggpoStateFrame;
		std::uint64_t overall;
		bool sent;
	};
	std::map<int, Checkpoint> local_;
	int next_ = RollbackSession::kCheckpointInterval; // the next checkpoint to take
	int lastConfirmed_ = -1;
};

} // namespace sfve::adapter
