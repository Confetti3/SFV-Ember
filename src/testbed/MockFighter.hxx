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
//
// Like SFV, the mock has a round flow: it latches the KO two frames after the
// battle records it. Outside the rollback state (SFV's own layout) a KO on
// guessed inputs can be latched for good; in it, the flow rolls back too.

#include "core/GameAdapter.hxx"

#include <memory>

namespace sfve::testbed {

enum MockBug : unsigned {
	kBugNone = 0,
	kBugStateOutsideLayout = 1u << 0,
	kBugReadsNonInput = 1u << 1,
	kBugUnmaskedPointer = 1u << 2,
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
	// `flowInLayout`: the round flow is part of the rollback state (what the
	// SFV adapter does with the game's flow), instead of living outside it.
	explicit MockFighter(unsigned bugs = kBugNone, bool flowInLayout = false);

	std::string BuildId() const override;
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs& inputs, bool resimulating) override;

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
	bool flowInLayout_;
	int32_t flowKoFrame_ = -1;   // the round flow: outside the layout unless flowInLayout_
	std::unique_ptr<int> heapMarker_;
	StateLayout layout_;
};

} // namespace sfve::testbed
