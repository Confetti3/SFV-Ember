#pragma once

// What a replay's playback keeps of itself as it plays, seeks included
// (docs/design/beta10/replay-seek.md): its round and clock for the strip,
// how far into the recording it has been, and how its checkpoints compare
// with the recording's.
//
// Frames go back with a seek, so nothing here counts frames played to know
// where the playback is:
// * the clock (round, time into the round's fight) is what the recording's
//   own fight frames make of the frames before the playback's place: after a
//   seek it is made again from the game's start (NoteSeek);
// * a checkpoint is compared by its frame id, and the playback may reach it
//   again: its latest comparison is counted once, and a checkpoint that ever
//   differed stays a difference;
// * a playback is whole when it has been to the recording's last frame,
//   however many frames it played to get there.
//
// No game and no log here (SfvPlayback.cxx logs); game thread.

#include "core/ReplayFile.hxx"

#include <stdint.h>

#include <memory>
#include <vector>

namespace sfve::adapter {

// The round and the time into its fight: a round starts with its first fight
// frame, and a new game (a rematch in place) starts the rounds over.
class PlaybackClock {
public:
	// One frame played: whether it read inputs, and the game's number.
	void Note(bool fight, unsigned game);
	// The clock at the start of `to`, the game being the one that began at
	// frame `from` (its frames before `to` are the recording's).
	void Rebuild(const ReplayFrames& frames, int from, int to);
	int Round() const { return round_; }
	uint32_t Cursor() const { return cursor_; }

private:
	unsigned game_ = 0;
	int stretches_ = 0, round_ = 0;
	uint32_t cursor_ = 0;
	bool inFight_ = false;
};

class PlaybackBook {
public:
	// A playback of `replay` starts.
	void Start(std::shared_ptr<const replay::Replay> replay);
	bool Active() const { return replay_ != nullptr; }

	// The frame `frame` was played: whether it read inputs and the game's number.
	void NoteFrame(int frame, bool fight, unsigned game);
	// The playback was put back at the start of `frame`, in the game that began at `from`.
	void NoteSeek(int frame, int from);

	enum class Compared { NotACheckpoint, Equal, FirstDifference, Differs };
	// A checkpoint the playback reached, with the recording's hash for it
	// (`recorded`, when it is one the recording has).
	Compared NoteCheckpoint(int frame, uint64_t hash, uint64_t* recorded);
	// The start state compared with the one P1's copy started the battle from.
	void NoteStart(bool equal);

	// Frames played in all, seeks' replays included.
	int Played() const { return played_; }
	// One past the furthest frame reached: the recording's frame count when the playback got to its end.
	int Furthest() const { return furthest_; }
	bool Whole() const { return replay_ && (size_t)furthest_ == replay_->frames.size(); }
	// Checkpoints compared (each once, the start state's included) and those that ever differed.
	int CheckpointsCompared() const { return compared_ + (startCompared_ ? 1 : 0); }
	int Differing() const { return differing_ + (startDiffers_ ? 1 : 0); }
	// Every comparison made, a checkpoint reached again counted again.
	int Comparisons() const { return comparisons_; }
	int Round() const { return clock_.Round(); }
	uint32_t Cursor() const { return clock_.Cursor(); }

private:
	enum class Seen : uint8_t { Not, Equal, Differs };
	std::shared_ptr<const replay::Replay> replay_;
	PlaybackClock clock_;
	std::vector<Seen> seen_; // per recorded checkpoint
	int played_ = 0, furthest_ = 0;
	int compared_ = 0, differing_ = 0, comparisons_ = 0;
	bool startCompared_ = false, startDiffers_ = false;
};

} // namespace sfve::adapter
