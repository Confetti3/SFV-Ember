#pragma once

// A small deterministic two-player fighter that implements GameAdapter, so
// the rollback session, the synctest harness and desync detection are tested
// without Street Fighter V.
//
// Its bugs can be switched on, one per class of real problem the port has to
// find in SFV:
//
// * StateOutsideLayout: a value the simulation reads and writes that is not
//   in the layout (SFV's InputUnit counters and tick counter).
// * ReadsNonInput: the simulation reads something that is neither state nor
//   input (a wall clock, a per-process counter).
// * UnmaskedPointer: a heap address kept in the state and not masked, so two
//   processes hash differently while playing identically.
// * UnmaskedClock: a clock reading kept in the state (SFV's input unit keeps
//   one) and not masked for peers: one process replays it exactly, so only
//   peers see it differ.
//
// Like SFV, the mock has a round flow: it latches the KO two frames after the
// battle records it. It is part of the rollback state, as the SFV adapter
// makes the game's flow; kBugFlowOutsideLayout leaves it out (as the game's
// own snapshot does), so a KO on guessed inputs can be latched for good.
//
// Pose reads (PoseReadsEvery, for the pose holds of core/PoseHold.hxx): like
// SFV's fight socket reads, some frames read a value only a drawn picture of
// the state they start from gives right. Draw() is the engine's frame: it
// shows the pose the last update left. A read is fresh when a frame was
// drawn since the last update and nothing was restored since; a stale read
// takes a record of the poses read fresh before (SocketReads' record), and a
// pose never read fresh keeps the drawn one, which is wrong: the update then
// wants a drawn frame (WantsDrawnFrame).

#include "core/GameAdapter.hxx"

#include <memory>
#include <unordered_set>
#include <vector>

namespace sfve::testbed {

enum MockBug : unsigned {
	kBugNone = 0,
	kBugStateOutsideLayout = 1u << 0,
	kBugReadsNonInput = 1u << 1,
	kBugUnmaskedPointer = 1u << 2,
	kBugFlowOutsideLayout = 1u << 3,
	kBugUnmaskedClock = 1u << 4,
};

// Input bits the mock reads.
enum MockInput : PackedInput {
	kBack = 1u << 0,    // away from the opponent
	kForward = 1u << 1, // toward the opponent
	kUp = 1u << 2,
	kPunch = 1u << 3,
	// Ends the frames that ignore inputs early, read confirmed on their skip
	// frames (SkipEvery), as SFV's intro reads its skip.
	kSkip = 1u << 4,
};

class MockFighter : public GameAdapter {
public:
	explicit MockFighter(unsigned bugs = kBugNone);

	std::string BuildId() const override;
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs& inputs, bool resimulating) override;
	bool InputsMatter() const override { return world_.frame < ignoreFrom_ || world_.frame >= IgnoredUntil(); }
	PackedInput ConfirmedBits(int frame) const override
	{
		return !InputsMatter() && skipEvery_ > 0 && frame % skipEvery_ == 0 ? PackedInput{kSkip} : PackedInput{0};
	}

	// Frames [from, to) read no inputs (as SFV's battle outside the fight).
	void IgnoreInputsBetween(int from, int to)
	{
		ignoreFrom_ = from;
		ignoreTo_ = to;
	}
	// Of those, every `every`th frame reads kSkip confirmed, and one given it
	// ends them: inputs matter again from the next frame (as SFV's intro).
	void SkipEvery(int every) { skipEvery_ = every; }
	// The frame the skip ended the ignored frames at, or -1.
	int SkippedAt() const { return world_.skippedAt; }
	// From battle frame `frame` on, this copy's player 1 loses a point of
	// health every frame: a real desync, which only peers can see.
	void DesyncFrom(int frame) { desyncFrom_ = frame; }
	// The mock's state stops being faithful, as SFV's flow record does when it
	// can no longer mirror or write back the game (GameAdapter::StateFailure):
	// at once (a capture after an update failing), or at the next restore (a
	// write-back failing).
	static constexpr const char* kStateFailure = "the mock's state can no longer be restored";
	void FailState() { stateFailure_ = kStateFailure; }
	void FailNextRestore() { failNextRestore_ = true; }
	void AfterLoad() override
	{
		if (failNextRestore_) stateFailure_ = kStateFailure;
		// A restore changes the state, not the models.
		fresh_ = false;
		placedSinceRestore_ = false;
	}
	const char* StateFailure() const override { return stateFailure_; }
	// The frame each resimulation started from (each rollback's target).
	const std::vector<int>& RollbackTargets() const { return rollbackTargets_; }

	// Frames f > 0 with f % every < run that read inputs make a pose read.
	// `needsPlacement`: a stale read takes the record only once an update has
	// run since the last restore (as SFV's rebuild at another root needs the
	// models placed), so a rollback's first replayed frame misses even a pose
	// read before.
	void PoseReadsEvery(int every, int run = 1, bool needsPlacement = false);
	// The engine draws a frame: the pose the last update placed is shown.
	void Draw()
	{
		drawn_ = placed_;
		fresh_ = true;
	}
	bool WantsDrawnFrame() const override { return missed_; }
	void NotePoseHold(const pose_hold::Event& event) override;
	int PoseMisses() const { return poseMisses_; }
	int PoseHoldsMade() const { return poseHolds_; }
	int PoseHoldsRefused() const { return poseRefused_; }
	int PoseHoldsRefused(pose_hold::Refusal why) const { return refusedBy_[(int)why]; }
	// Holds whose first update after was another frame's, or not fresh.
	int NotFreshAfterHold() const { return notFreshAfterHold_; }
	int PoseHoldsCancelled() const { return poseCancelled_; }
	// Holds whose first update after was the held frame's, fresh.
	int FreshAfterHold() const { return freshAfterHold_; }
	// Pending holds noted, a chained one's included.
	int PoseHoldsPending() const { return posePending_; }

	int Frame() const { return world_.frame; }
	int Health(int player) const { return world_.fighters[player].health; }
	int X(int player) const { return world_.fighters[player].x; }
	int ResimulatedFrames() const { return resimulated_; }
	// The frame the round flow latched the KO at, or -1.
	int FlowKoFrame() const { return flowKoFrame_; }

private:
	struct Fighter {
		int32_t x, y, vx, vy;
		int32_t health;
		int32_t attackFrames;
		int32_t hitstun;
		int32_t facing;
	};
	struct World {
		int32_t frame;
		uint32_t rng;
		Fighter fighters[kPlayers];
		void* owner; // a heap pointer: differs between processes
		int32_t roundTimer;
		int32_t koAt; // battle frame of the KO, -1 before
		int32_t clock; // read once, at construction; differs between two games
		int32_t skippedAt; // the first frame after a skip of the ignored frames, -1 before
	};
	static_assert(sizeof(World) == 96, "no padding in the world: every byte is hashed");
	struct Presentation {
		float animationClock; // advances with ticks; restored, never hashed
		float pad;
	};

	void Build();
	// The pose a state shows: where the fighters stand and what they do.
	static uint32_t PoseOf(const World& world);
	// The frame's pose read, if it makes one (PoseReadsEvery).
	void ReadPose();
	// The end of the ignored frames: ignoreTo_, or earlier once skipped.
	int IgnoredUntil() const
	{
		return world_.skippedAt >= 0 && world_.skippedAt < ignoreTo_ ? world_.skippedAt : ignoreTo_;
	}

	unsigned bugs_;
	World world_{};
	Presentation presentation_{};
	int32_t comboCounter_ = 0;     // outside the layout (kBugStateOutsideLayout)
	int32_t callsSinceStart_ = 0;  // never state (kBugReadsNonInput)
	int resimulated_ = 0;
	bool lastWasResimulation_ = false;
	std::vector<int> rollbackTargets_;
	int ignoreFrom_ = 0, ignoreTo_ = 0;
	int skipEvery_ = 0;
	int desyncFrom_ = -1;
	bool failNextRestore_ = false;
	const char* stateFailure_ = nullptr;
	int32_t flowKoFrame_ = -1;   // the round flow (outside the layout with kBugFlowOutsideLayout)
	std::unique_ptr<int> heapMarker_;
	StateLayout layout_;
	// Pose reads: the frames that make one, what the models show (placed by
	// the last update, drawn by the last Draw, fresh, placed since the last
	// restore), the poses read fresh (never rollback state: SFV's record is
	// not either), whether the last update missed, and the counts.
	int poseEvery_ = 0, poseRun_ = 1;
	bool poseNeedsPlacement_ = false;
	uint32_t placed_ = 0, drawn_ = 0;
	bool fresh_ = false;
	bool placedSinceRestore_ = false;
	std::unordered_set<uint32_t> posesSeen_;
	bool missed_ = false;
	int awaitingHold_ = -1;
	int poseMisses_ = 0, poseHolds_ = 0, poseRefused_ = 0, poseCancelled_ = 0, freshAfterHold_ = 0, posePending_ = 0;
	int notFreshAfterHold_ = 0;
	int refusedBy_[pose_hold::kRefusals] = {};
};

} // namespace sfve::testbed
