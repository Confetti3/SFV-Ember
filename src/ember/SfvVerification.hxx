#pragma once

// SFV's desync check behind Ember's room client (the library's
// session/BattleVerifier.hxx; MakeBattleVerifier is defined in
// SfvVerification.cxx). A player publishes one overall hash per confirmed
// checkpoint of its rollback session: the peers' hash RollbackSession keeps
// every kCheckpointInterval frames. Its flow, chara0 and chara1 are 0, and no
// legacy (v1) snapshot is ever sent. Every client compares a remote hash once
// its own checkpoint of that frame is confirmed; the battle's first mismatch
// ends the fight (the adapter's Battle::Fail) and is reported, a watcher's
// too (DesyncReporter.hxx).
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
		// The last checkpoint, before this one, at which the players' hashes
		// agreed; -1 when none did (the first checkpoint already differs).
		int previousMatch = -1;
	};
	// Sends one hash to the room; false when the room did not take it (it is
	// sent again on the next step).
	using Publish = std::function<bool(const sf4e::SessionProtocol::BattleHashV2&)>;

	// One step of the room client: publishes, in frame order, the checkpoints
	// confirmed and saved since the last step (none when `publish` is null: a
	// client that watches publishes nothing), and compares and erases the
	// remote hashes whose checkpoint is confirmed here. Returns the battle's
	// first mismatch, once: later steps of the same battle report none.
	//
	// GGPO can confirm a frame before the session has run and saved it (by up
	// to the input delay), so a checkpoint is taken only once the session has
	// saved it. One the session no longer keeps (older than its hash history)
	// is skipped.
	//
	// A session whose confirmed frame went back (a new battle) starts the
	// check over, as Reset does, and drops the remote hashes it holds: they
	// belong to the battle before.
	std::optional<Mismatch> Step(const RollbackSession& session, const Publish* publish,
		std::map<int, sf4e::SessionProtocol::BattleHashV2>& remote);
	// The battle is over: its checkpoints and its reported mismatch go. The
	// room client clears the remote hashes it buffers itself.
	void Reset();

	// How many confirmed checkpoints are kept for remote hashes still to come:
	// the 64 seconds of play it was at 60 frames to a checkpoint (64), now 192
	// at 20.
	static constexpr std::size_t kKeptCheckpoints = 64 * 60 / RollbackSession::kCheckpointInterval;

private:
	struct Checkpoint {
		int ggpoStateFrame;
		std::uint64_t overall;
		bool sent;
	};
	void TakeCheckpoints(const RollbackSession& session, int confirmed);

	std::map<int, Checkpoint> local_;
	int next_ = RollbackSession::kCheckpointInterval; // the next checkpoint to take
	int lastConfirmed_ = -1;
	int lastMatch_ = -1;            // the newest checkpoint whose hashes both players agreed on
	bool mismatchReported_ = false; // this battle's mismatch was returned already
};

// A checkpoint's regions by name, size and the hash the peers compare, one
// "desync: checkpoint F region R, N bytes, hash H" line each (the form
// tools/runcross.py and tools/compare.py read): a room match's mismatch,
// and a replay's playback with -sfve_replay_regions.
void LogCheckpointRegions(const RollbackSession& session, int frame);

} // namespace sfve::adapter
