#include "ember/PlaybackBook.hxx"

#include <algorithm>
#include <utility>

namespace sfve::adapter {

void PlaybackClock::Note(bool fight, unsigned game)
{
	if (game != game_) {
		// A game in place starts its rounds over.
		game_ = game;
		stretches_ = 0;
		inFight_ = false;
		round_ = 0;
		cursor_ = 0;
	}
	if (fight && !inFight_) {
		// A round's fight begins: its clock starts over.
		round_ = stretches_++;
		cursor_ = 0;
	}
	if (fight) cursor_++;
	inFight_ = fight;
}

void PlaybackClock::Rebuild(const ReplayFrames& frames, int from, int to)
{
	stretches_ = round_ = 0;
	cursor_ = 0;
	inFight_ = false;
	const int end = std::min(to, static_cast<int>(frames.size()));
	for (int frame = std::max(from, 0); frame < end; frame++) Note(frames[(size_t)frame].Fight(), game_);
}

void PlaybackBook::Start(std::shared_ptr<const replay::Replay> replay)
{
	*this = PlaybackBook();
	replay_ = std::move(replay);
	if (replay_) seen_.assign(replay_->checkpoints.size(), Seen::Not);
}

void PlaybackBook::NoteFrame(int frame, bool fight, unsigned game)
{
	if (!replay_) return;
	played_++;
	furthest_ = std::max(furthest_, frame + 1);
	clock_.Note(fight, game);
}

void PlaybackBook::NoteSeek(int frame, int from)
{
	if (!replay_) return;
	clock_.Rebuild(replay_->frames, from, frame);
}

PlaybackBook::Compared PlaybackBook::NoteCheckpoint(int frame, uint64_t hash, uint64_t* recorded)
{
	if (!replay_) return Compared::NotACheckpoint;
	const auto& checkpoints = replay_->checkpoints;
	const auto it = std::lower_bound(checkpoints.begin(), checkpoints.end(), frame,
		[](const std::pair<int, uint64_t>& c, int f) { return c.first < f; });
	if (it == checkpoints.end() || it->first != frame) return Compared::NotACheckpoint;
	if (recorded) *recorded = it->second;
	Seen& seen = seen_[(size_t)(it - checkpoints.begin())];
	comparisons_++;
	if (seen == Seen::Not) compared_++;
	if (it->second == hash) {
		// A checkpoint that differed once stays a difference.
		if (seen != Seen::Differs) seen = Seen::Equal;
		return seen == Seen::Equal ? Compared::Equal : Compared::Differs;
	}
	const bool first = seen != Seen::Differs;
	if (first) differing_++;
	seen = Seen::Differs;
	return first ? Compared::FirstDifference : Compared::Differs;
}

void PlaybackBook::NoteStart(bool equal)
{
	if (!replay_) return;
	startCompared_ = true;
	startDiffers_ = !equal;
}

} // namespace sfve::adapter
