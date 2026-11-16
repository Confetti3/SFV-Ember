#include "mod/MatchRecording.hxx"

#include "ember/SfvReplays.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"

#include <algorithm>
#include <optional>

namespace sfve::mod {

void MatchRecording::Start()
{
	recorder_.Start();
	recording_ = {};
	recordingWhole_ = false;
}

void MatchRecording::Confirm(const RollbackSession* session)
{
	// Played, and confirmed on both fighters' copies (a watcher's every frame
	// is): no rollback reports these frames again.
	if (!recorder_.Active() || !session || !session->Live()) return;
	recorder_.Confirm(std::min(session->Frame(), session->LastConfirmedFrame() + 1));
}

void MatchRecording::NoteGame(int endFrame)
{
	if (!recorder_.Active()) return;
	const std::optional<MatchResult> result = DecidedMatchResult();
	replay::Game game;
	if (result) {
		game.decided = result->decided;
		game.winner = result->winner;
		game.wins[0] = result->wins[0];
		game.wins[1] = result->wins[1];
		game.rounds = result->rounds;
		game.bestOf = result->bestOf;
	}
	game.endFrame = endFrame;
	recording_.games.push_back(game);
}

void MatchRecording::Finish()
{
	if (!recorder_.Active()) return;
	recorder_.Stop();
	if (recorder_.Broken()) {
		Log("netplay: the battle's frames came out of order; its replay is not saved");
		return;
	}
	replay::Replay r;
	r.header = recording_;
	r.header.complete = recordingWhole_;
	recorder_.Take(&r.frames, &r.checkpoints);
	if (r.frames.empty()) {
		Log("netplay: the battle played no frame; no replay of it");
		return;
	}
	adapter::SaveReplay(std::move(r));
}

} // namespace sfve::mod
