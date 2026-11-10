#include "ember/SfvVerification.hxx"

#include "ember/DesyncReporter.hxx"
#include "ember/SfvGame.hxx"
#include "mod/Log.hxx"

#include "common/ConfirmedCheckpoint.hxx"
#include "session/BattleVerifier.hxx"
#include "session/sf4e__SessionClient.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <nlohmann/json.hpp>

namespace sfve::adapter {

using sf4e::SessionProtocol::BattleHashV2;

std::optional<CheckpointExchange::Mismatch> CheckpointExchange::Step(const RollbackSession& session,
	const Publish* publish, std::map<int, BattleHashV2>& remote)
{
	const int confirmed = session.LastConfirmedFrame();
	if (confirmed < lastConfirmed_) {
		Reset();
		remote.clear();
	}
	lastConfirmed_ = confirmed;
	TakeCheckpoints(session, confirmed);
	while (local_.size() > kKeptCheckpoints) local_.erase(local_.begin());

	if (publish) {
		for (auto& [frame, checkpoint] : local_) {
			if (checkpoint.sent) continue;
			BattleHashV2 message;
			message.frameIdx = frame;
			message.overall = checkpoint.overall;
			message.fromPlayer = true;
			if (!(*publish)(message)) break;
			checkpoint.sent = true;
		}
	}

	std::optional<Mismatch> first;
	sf4e::statehash::ReconcileRemoteHashes(
		remote, confirmed,
		[this](int frame) -> const Checkpoint* {
			const auto it = local_.find(frame);
			return it == local_.end() ? nullptr : &it->second;
		},
		[&first](const Checkpoint& local, const BattleHashV2& theirs) {
			// A watcher's hash is a diagnostic only; it never ends a fight.
			if (!first && theirs.fromPlayer && local.overall != theirs.overall) {
				first = Mismatch{local.ggpoStateFrame, local.overall, theirs.overall};
			}
		});
	// Remote hashes older than every kept checkpoint can no longer be compared.
	if (!local_.empty()) remote.erase(remote.begin(), remote.lower_bound(local_.begin()->first));
	if (!first || mismatchReported_) return std::nullopt;
	mismatchReported_ = true;
	return first;
}

void CheckpointExchange::TakeCheckpoints(const RollbackSession& session, int confirmed)
{
	for (; sf4e::statehash::IsConfirmedCheckpoint(next_, confirmed); next_ += RollbackSession::kCheckpointInterval) {
		// Confirmed but not run and saved yet: taken on a later step.
		if (next_ > session.Frame()) return;
		std::uint64_t hash = 0;
		// Saved, and every checkpoint is hashed, so a miss is one that fell
		// out of the session's hash history: skipped.
		if (session.HashAt(next_, &hash)) local_[next_] = {next_, hash, false};
	}
}

void CheckpointExchange::Reset()
{
	local_.clear();
	next_ = RollbackSession::kCheckpointInterval;
	lastConfirmed_ = -1;
	mismatchReported_ = false;
}

namespace {

// The room client's check for the battle in play.
class SfvVerifier final : public sf4e::session::BattleVerifier {
public:
	// SFV sends no legacy snapshot and reads none.
	void OnRemoteSnapshot(sf4e::SessionClient&, const sf4e::SessionProtocol::StateSnapshot&) override {}

	void Step(sf4e::SessionClient& client, std::map<int, BattleHashV2>& remote) override
	{
		const RollbackSession* session = CurrentBattle().session;
		if (!session || !session->Live()) return;
		const bool player = client.IsLocalPlayer();
		const CheckpointExchange::Publish send = [&client](const BattleHashV2& hash) {
			nlohmann::json message = hash;
			return client.Send(message, nullptr) == sf4e::session::SendResult::Queued;
		};
		const auto mismatch = exchange_.Step(*session, player ? &send : nullptr, remote);
		if (!mismatch) return;
		mod::Log("desync: checkpoint %d is %016llx here and %016llx at the other player; %s", mismatch->frame,
			(unsigned long long)mismatch->local, (unsigned long long)mismatch->remote,
			player ? "the fight ends" : "this copy only watches");
		LogCheckpointRegions(*session, mismatch->frame);
		// Sent to the report intake by the launcher (DesyncReporter.hxx).
		ReportDesync(*session, client, *mismatch, player);
		sf4e::NetplayFacade::PushDesyncNotice(!player);
		if (player) ember::game::Game().battle.Fail();
	}

	void Reset() override { exchange_.Reset(); }

private:
	CheckpointExchange exchange_;
};

} // namespace

void LogCheckpointRegions(const RollbackSession& session, int frame)
{
	std::vector<RegionHash> hashes;
	if (!session.RegionHashesAt(frame, &hashes)) return;
	const std::vector<StateRegion>& regions = session.Layout().Regions();
	for (const RegionHash& h : hashes) {
		mod::Log("desync: checkpoint %d region %s, %llu bytes, hash %016llx", frame, regions[h.region].name.c_str(),
			(unsigned long long)h.size, (unsigned long long)h.hash);
	}
}

} // namespace sfve::adapter

std::unique_ptr<sf4e::session::BattleVerifier> sf4e::session::MakeBattleVerifier()
{
	return std::make_unique<sfve::adapter::SfvVerifier>();
}
