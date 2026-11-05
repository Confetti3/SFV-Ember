#pragma once

// One-process rollback check (the first proof before any networking).
//
// Plays forward one frame per Step, saving every frame. Every `distance`
// frames it restores the state from `distance` frames back and replays those
// frames with the inputs they were first played with, comparing each replayed
// frame's hash with the hash recorded the first time. With a schedule seed
// the checks come as a network's rollbacks do instead: each a random 1 to
// `distance` frames after the last, rolling back a random 1 to `distance`
// frames, so rollbacks of every depth start on every kind of frame and
// overlap (the same seed gives the same schedule). Any difference means
// some state the simulation depends on is missing from the layout, or the
// simulation reads something that is not its inputs. The report names the
// regions that differ and the first differing byte in each.
//
// Frames follow netplay's rule (GameAdapter::InputsMatter): a frame whose
// inputs the game ignores is played with neutral inputs, and no check
// restores to its start, since a session never mispredicts it; a check from
// an earlier frame still replays across it, as a rollback does.
//
// SyncTester is incremental so it can run inside the game, one rendered frame
// per Step (the mod's synctest mode); RunSyncTest drives it in a loop for the
// mock game.

#include "core/GameAdapter.hxx"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace sfve {

struct WordDiff {
	size_t offset = 0;    // of the 8-byte word in its region
	uint64_t saved = 0;   // as first played
	uint64_t live = 0;    // as replayed
};

struct RegionDiff {
	std::string region;
	size_t firstOffset = 0;
	size_t differingBytes = 0;
	std::vector<WordDiff> words; // the first few differing words, to tell pointers from data
};

struct SyncTestReport {
	bool ok = true;
	int framesRun = 0;
	int rollbacks = 0;
	int failedFrame = -1;           // first frame whose replay differed
	bool loadMismatch = false;      // the restored state itself did not hash as saved
	std::vector<RegionDiff> diffs;  // checked regions that differ at failedFrame
	std::string message;
	// Learning mode only: every replay that differed, and how often each
	// region was among the differences.
	int failures = 0;
	std::map<std::string, int> regionFailures;
	std::map<std::string, RegionDiff> regionExamples; // the first difference seen in each
};

class SyncTester {
public:
	// `learn`: keep going after a difference, tallying the regions involved
	// (to tell presentation state that drifts from a real desync); replayed
	// frames are saved again, so the game stays on one timeline.
	// `perturb`: before each replay, also play the same frames with wrong
	// inputs and restore again, as a session does after a misprediction.
	// State that the wrong timeline leaves behind outside the layout then
	// shows up as a difference.
	// `scheduleSeed`: 0 for a check every `distance` frames of `distance`
	// frames, otherwise random spacing and depth (above).
	SyncTester(GameAdapter& game, int distance, bool learn = false, bool perturb = false, uint32_t scheduleSeed = 0);

	// One forward frame, then a rollback check when one is due. Returns false
	// once a check has failed (never in learning mode).
	bool Step(const FrameInputs& inputs);
	const SyncTestReport& Report() const { return report_; }

private:
	struct Saved {
		std::vector<uint8_t> bytes;
		uint64_t hash = 0;
		FrameInputs inputs;      // the players' inputs for the frame starting from this state
		bool restorable = false; // that frame read them: a rollback may start here
	};

	Saved& At(int frame) { return saved_[frame % saved_.size()]; }
	void Save(int frame);
	// The oldest state within `depth` frames a rollback may start from, or
	// frame_ when there is none.
	int OldestRestorable(int depth);
	// When the check after this one comes, and how deep it goes.
	void ScheduleNextCheck();
	int Random(int from, int to); // inclusive
	// The inputs a session gives the frame the game is about to play.
	FrameInputs AsPlayed(const FrameInputs& inputs) const;
	// Restores state `from`, replays to `now` and compares every frame.
	bool CheckRollback(int from, int now);
	void Fail(int frame, const char* what);
	static FrameInputs Perturbed(const FrameInputs& inputs, int frame);

	GameAdapter& game_;
	int distance_;
	bool learn_;
	bool perturb_;
	int frame_ = 0;
	uint32_t schedule_;  // xorshift32 state; 0: the fixed schedule
	int nextCheck_ = 0;  // the frame the next check comes after
	int nextDepth_ = 0;
	std::vector<Saved> saved_;
	SyncTestReport report_;
};

struct SyncTestOptions {
	int frames = 600;
	int distance = 7;           // frames rolled back each check (GGPO allows up to 8)
	uint32_t scheduleSeed = 0;  // nonzero: random spacing and depth up to `distance`
	std::function<FrameInputs(int frame)> inputs;
};

SyncTestReport RunSyncTest(GameAdapter& game, const SyncTestOptions& options);

// Regions whose bytes differ between a saved buffer and live memory, ignoring
// masked words and unchecked regions.
std::vector<RegionDiff> DiffSavedAgainstLive(const StateLayout& layout, const uint8_t* saved);

} // namespace sfve
