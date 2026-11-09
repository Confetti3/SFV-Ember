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

#include "core/GameAdapter.hxx"

#include <memory>
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
};

class MockFighter : public GameAdapter {
public:
	explicit MockFighter(unsigned bugs = kBugNone);

	std::string BuildId() const override;
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs& inputs, bool resimulating) override;
	bool InputsMatter() const override { return world_.frame < ignoreFrom_ || world_.frame >= ignoreTo_; }

	// Frames [from, to) read no inputs (as SFV's battle outside the fight).
	void IgnoreInputsBetween(int from, int to)
	{
		ignoreFrom_ = from;
		ignoreTo_ = to;
	}
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
	}
	const char* StateFailure() const override { return stateFailure_; }
	// The frame each resimulation started from (each rollback's target).
	const std::vector<int>& RollbackTargets() const { return rollbackTargets_; }

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
	};
	struct Presentation {
		float animationClock; // advances with ticks; restored, never hashed
		float pad;
	};

	void Build();

	unsigned bugs_;
	World world_{};
	Presentation presentation_{};
	int32_t comboCounter_ = 0;     // outside the layout (kBugStateOutsideLayout)
	int32_t callsSinceStart_ = 0;  // never state (kBugReadsNonInput)
	int resimulated_ = 0;
	bool lastWasResimulation_ = false;
	std::vector<int> rollbackTargets_;
	int ignoreFrom_ = 0, ignoreTo_ = 0;
	int desyncFrom_ = -1;
	bool failNextRestore_ = false;
	const char* stateFailure_ = nullptr;
	int32_t flowKoFrame_ = -1;   // the round flow (outside the layout with kBugFlowOutsideLayout)
	std::unique_ptr<int> heapMarker_;
	StateLayout layout_;
};

} // namespace sfve::testbed
