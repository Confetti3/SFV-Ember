#include "core/PoseHold.hxx"

namespace sfve::pose_hold {

const char* RefusalName(Refusal why)
{
	switch (why) {
	case Refusal::None: return "none";
	case Refusal::FirstFrame: return "first frame";
	case Refusal::HeldFrame: return "held frame";
	case Refusal::StateNotKept: return "state not kept";
	case Refusal::NotFreshAfterHold: return "not fresh after the hold";
	case Refusal::ChainLimit: return "chain limit";
	case Refusal::Refused: return "refused by GGPO";
	}
	return "?";
}

void Book::Start(bool on)
{
	*this = {};
	on_ = on;
}

int Book::Wanted(int frame, bool wanted, Refusal* why)
{
	*why = Refusal::None;
	// Only the first update after a hold can show the hold did not work.
	const bool firstAfterHold = firstUpdateDue_ >= 0 && firstUpdateDue_ == frame;
	firstUpdateDue_ = -1;
	if (!on_ || !wanted) return -1;
	if (pending_ >= 0 && pending_ <= frame) return -1;
	if (refused_ >= 0 && refused_ <= frame) return -1;
	if (firstAfterHold) *why = Refusal::NotFreshAfterHold;
	else if (frame < 1) *why = Refusal::FirstFrame;
	return frame;
}

void Book::Pend(int frame)
{
	if (pending_ < 0 || frame < pending_) pending_ = frame;
	// The hold replays the frames after it, a refused one among them.
	if (refused_ >= frame) refused_ = -1;
}

bool Book::Refuse(int frame)
{
	if (refused_ < 0 || frame < refused_) refused_ = frame;
	if (pending_ > frame) {
		pending_ = -1;
		return true;
	}
	return false;
}

int Book::Loaded(int frame)
{
	int dropped = -1;
	if (pending_ >= frame) {
		dropped = pending_;
		pending_ = -1;
	}
	if (refused_ >= frame) refused_ = -1;
	return dropped;
}

void Book::Held(int frame, int resumeTo)
{
	pending_ = -1;
	heldAt_ = frame;
	resumeTo_ = resumeTo;
	firstUpdateDue_ = frame;
}

void Book::EndTick()
{
	pending_ = -1;
	refused_ = -1;
}

void Book::Resumed()
{
	heldAt_ = -1;
	resumeTo_ = -1;
}

} // namespace sfve::pose_hold
