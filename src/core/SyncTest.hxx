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
// inputs the game ignores is played with neutral inputs (but for the bits it
// reads confirmed, GivenInputs), and no check restores to its start, since a
// session never mispredicts it; a check from an earlier frame still replays
// across it, as a rollback does, unless the frame read bits confirmed: no
// rollback reaches back past such a frame.
//
// SyncTester is incremental so it can run inside the game, one rendered frame
// per Step (the mod's synctest mode); RunSyncTest drives it in a loop for the
// mock game.

#include "core/GameAdapter.hxx"
#include "core/ReplayFrames.hxx"

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
		// That frame read bits confirmed (GameAdapter::ConfirmedBits): a
		// session steps it confirmed, so no rollback reaches back past it.
		bool readsConfirmed = false;
	};

	Saved& At(int frame) { return saved_[frame % saved_.size()]; }
	void Save(int frame);
	// The oldest state within `depth` frames a rollback may start from, after
	// the last frame that read bits confirmed, or frame_ when there is none.
	int OldestRestorable(int depth);
	// When the check after this one comes, and how deep it goes.
	void ScheduleNextCheck();
	int Random(int from, int to); // inclusive
	// The inputs a session gives frame `frame`, the one the game is about to play.
	FrameInputs AsPlayed(const FrameInputs& inputs, int frame) const;
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

// A recording played the way a fighter's copy plays it when the other
// player's inputs arrive late: `remote`'s input for frame k is known only once
// frame k + lag is played, and until then the frame plays that player's last
// known input, as GGPO guesses. Each frame is played in its own rendered
// frame, so the game's presentation runs on the guessed timeline as it does
// live; the synctest instead plays its wrong stretches back to back inside
// one. When an input arrives that differs from the guess, the state from the
// first wrong frame is restored and the frames since are played again, all
// within the rendered frame, as a session's rollback does. Frames whose
// inputs the game ignores play the recording's (a session steps those that
// read bits confirmed). The state at the start of a frame is final once every
// input before it is known (ConfirmedThrough), and it should then equal the
// recording's at that frame.
//
// With a jitter seed the delay is not fixed but wanders as a network's does:
// each frame's input arrives 1 to `lag` frames late, the delay a random walk
// with occasional jumps to `lag`, and inputs arrive in order, so a late one
// holds back those after it and they then arrive together.
//
// With pose holds (core/PoseHold.hxx) it holds as a fighter's session does: a
// frame whose update wanted a drawn frame (GameAdapter::WantsDrawnFrame) ends
// the rendered frame at that frame's start, its state placed by playing the
// frame before again, and the next Step (or Finish) first plays the frames
// from it again. Until then the states from the held frame on are not final.
class LaggedReplay {
public:
	LaggedReplay(GameAdapter& game, const ReplayFrames& frames, int remote, int lag, uint32_t jitterSeed = 0,
		bool poseHolds = false);

	// Plays frame `frame`, the next one, after the rollback the inputs known
	// by now call for.
	void Step(int frame);
	// Every input known: the rollback the last guesses call for; nothing new
	// is played. False while a pose hold waits for a drawn frame: call it
	// again on the next rendered frame.
	bool Finish();
	// The states at the start of frames 0 to this one are final.
	int ConfirmedThrough() const
	{
		return holds_.Resuming() && confirmedThrough_ > holds_.HeldAt() ? holds_.HeldAt() : confirmedThrough_;
	}
	int PoseHolds() const { return poseHolds_; }
	int PoseHoldsRefused() const { return poseHoldsRefused_; }
	// The final state at the start of `frame` while it is kept (the last
	// lag + 2 frames played), or null.
	const uint8_t* SavedAt(int frame) const;
	int Rollbacks() const { return rollbacks_; }
	int LongestRollback() const { return longest_; }
	// Called with each state saved at the start of a frame some of whose
	// earlier inputs are guesses, the first time it is played (a fighter's
	// copy hashes such a checkpoint before the rollback that corrects it).
	std::function<void(int frame, const uint8_t* state)> onGuessedState;

private:
	struct Saved {
		std::vector<uint8_t> bytes;
		int frame = -1;
		bool guessed = false;   // the remote input was a guess
		PackedInput remote = 0; // the remote input the game was given
	};
	Saved& At(int frame) { return saved_[(size_t)frame % saved_.size()]; }
	// The remote inputs known now are those of frames up to `known`.
	void Resolve(int known);
	void Play(int frame, int known);
	// The last frame whose remote input has arrived when `frame` is played.
	int Known(int frame);
	// Pose holds: the state `frame` starts from is still kept.
	bool Kept(int frame) { return frame >= 0 && At(frame).frame == frame; }
	// The update of `frame` wanted a drawn frame (Book::Wanted let it through).
	void ConsiderPoseHold(int frame, pose_hold::Refusal why);
	// The end of a Step: the pending hold is made.
	void FinishPoseHold(int known);
	// The start of a Step: a hold's frames are played again.
	void ResumePoseHold(int known);
	pose_hold::Book holds_;
	bool holding_ = false;      // the hold's own replay of the frame before the held one
	bool replayWanted_ = false; // ...wanted a drawn frame too
	int poseHolds_ = 0;
	int poseHoldsRefused_ = 0;

	uint32_t jitter_;     // xorshift32 state; 0: a fixed delay
	int delay_ = 1;       // the walk's current delay
	int arrivedTo_ = -1;  // remote inputs arrived through this frame
	int scheduledTo_ = -1;
	std::vector<int> arrival_; // the frame each remote input arrives on

	GameAdapter& game_;
	const ReplayFrames& frames_;
	int remote_;
	int lag_;
	int next_ = 0; // the next frame to play
	int checked_ = -1;         // guesses compared through this frame
	int confirmedThrough_ = 0;
	int rollbacks_ = 0;
	int longest_ = 0;
	std::vector<Saved> saved_;
};

// Regions whose bytes differ between a saved buffer and live memory, ignoring
// masked words and unchecked regions.
std::vector<RegionDiff> DiffSavedAgainstLive(const StateLayout& layout, const uint8_t* saved);

} // namespace sfve
