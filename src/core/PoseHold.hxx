#pragma once

// Pose holds (docs/design/beta9/pose-hold.md).
//
// Some reads a battle update makes give the right value only on a frame whose
// start state the engine has just drawn (SFV: a fight socket read, the
// position of a bone in the pose the engine last evaluated; mod/SocketReads).
// A rollback's replayed frames, and the new frame after a rollback in the same
// tick, start from states that were not drawn; their reads are served from a
// record of earlier fresh reads, and a read of a pose never recorded keeps the
// live, wrong value: a desync (GameAdapter::WantsDrawnFrame).
//
// A hold puts that right. When the update of frame F wanted a drawn frame, the
// tick does not end where it is: once its own work is done, the session loads
// the state frame F - 1 starts from and plays F - 1 again, whose last phase
// places the models for F's start state, and stops there. The engine draws
// that state, so the next tick's first update, F played again, reads it
// fresh, and the frames after F are replayed up to where the tick stood
// before the next one is played. Nothing is played twice for good: the
// frames after F that the tick played on the live value are replaced before
// any of them is hashed, confirmed or reported as final.
//
// Book is the bookkeeping both drivers share (RollbackSession over GGPO, and
// LaggedReplay): within a tick, which frame a hold is pending for (the
// earliest) or was refused at; across ticks, the frame held at and the frame
// its replay reaches. Game thread only, no game and no GGPO in it.

#include <stdint.h>

namespace sfve::pose_hold {

// Why an update that wanted a drawn frame got none: its read keeps the live
// value.
enum class Refusal : uint8_t {
	None,
	FirstFrame,        // frame 0: no update before it places the models
	HeldFrame,         // it, or the frame before it, is a held frame (StepHeld): no update would place the models
	StateNotKept,      // the state frame - 1 starts from is no longer kept (no copy, out of the ring, its inputs gone)
	NotFreshAfterHold, // the first update after a hold for this frame missed again: no drawn frame came between, or something ran or was restored first
	ChainLimit,        // the hold's own replay of the frame before missed too, kChainLimit holds deep
	Refused,           // GGPO refused the hold at the end of the tick (never expected: its checks passed during the tick)
};
constexpr int kRefusals = 7;
const char* RefusalName(Refusal why);

// How many frames back one tick's hold may go when its own replay of the frame
// before the held one wanted a drawn frame too.
constexpr int kChainLimit = 4;

// What a session tells the game about a hold (GameAdapter::NotePoseHold).
struct Event {
	enum class Kind : uint8_t {
		Pending,   // the update just run wanted a drawn frame: the tick ends held at `frame` unless a rollback replays it again
		Cancelled, // a rollback in the same tick loaded `frame`'s state or an earlier one: the pending hold is dropped
		Held,      // the tick ended at `frame`'s start, drawn; `replayed` frames are played again (frame - 1 now, the rest next tick)
		Refused,   // no hold for `frame` (`why`); its read keeps the live value
	};
	Kind kind = Kind::Pending;
	int frame = -1; // the session's frame of the update that wanted a drawn frame
	int replayed = 0;
	Refusal why = Refusal::None;
};

class Book {
public:
	// A new session or playback; `on`: holds are made (Config::poseHolds).
	void Start(bool on);
	bool On() const { return on_; }

	// After every update the driver runs, with its frame and whether it wanted a
	// drawn frame. Returns the frame to consider for a hold, -1 for nothing:
	// holds off, not wanted, or a hold pending (or a refusal) this tick at or
	// before this frame already (the hold replays this frame too; after a
	// refusal the timeline keeps live values from there). `why` is set when the
	// book alone refuses: frame 0, or the first update after a hold for this
	// same frame (it was not fresh). The driver adds its own checks, then
	// calls Pend or Refuse.
	int Wanted(int frame, bool wanted, Refusal* why);
	// A hold for `frame` ends this tick (the earliest pending wins).
	void Pend(int frame);
	// No hold for `frame`. True when a later pending hold was dropped with it
	// (it would replay after a frame that keeps a live value).
	bool Refuse(int frame);
	// The driver loaded the state `frame` starts from (a rollback): a pending
	// hold or a refusal at or after it is void, as the frames are replayed.
	// Returns the pending hold's frame dropped, or -1.
	int Loaded(int frame);
	// The frame a hold is pending for this tick, or -1.
	int Pending() const { return pending_; }

	// The tick ended held at `frame` (the pending one, or one earlier after a
	// chain), with the frames up to `resumeTo` (exclusive) left to replay.
	void Held(int frame, int resumeTo);
	// The tick is over: nothing is pending or refused any more.
	void EndTick();

	// A hold's frames wait to be replayed (from HeldAt() up to ResumeTo()).
	bool Resuming() const { return heldAt_ >= 0; }
	int HeldAt() const { return heldAt_; }
	int ResumeTo() const { return resumeTo_; }
	// The replay reached ResumeTo(), or the session ended.
	void Resumed();

private:
	bool on_ = false;
	int pending_ = -1;
	int refused_ = -1;
	int heldAt_ = -1;
	int resumeTo_ = -1;
	// A hold for this frame waits for its first update (Wanted then knows
	// whether that update missed again).
	int firstUpdateDue_ = -1;
};

} // namespace sfve::pose_hold
