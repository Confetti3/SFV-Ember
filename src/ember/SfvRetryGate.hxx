#pragma once

// When a picture that was not usable yet may be tried again: the one rule
// the backdrop's cycle (SfvBackdropRules' Cycle::Retry) and the room and pick
// pictures (SfvArt) share. A picture whose texture never came in time, or
// that the game has not streamed whole yet, is not lost: it is tried again
// once its delay has passed, and the delay grows with each failure in a row
// (to a most), so one that never comes right cannot spin the loader. Only a
// picture the game does not have at all is given up on, which is not this
// rule's. Pure, no clock of its own: the caller's seconds.

namespace sfve {

struct RetryRule {
	double delay = 1.0;  // after the first failure, seconds
	double growth = 1.0; // each failure in a row multiplies the delay by this ...
	double most = 0;     // ... up to this many seconds; 0 for no limit
	// The delay after the `failures`-th failure in a row (1 for the first).
	double DelayAfter(int failures) const
	{
		double wait = delay;
		for (int i = 1; i < failures; i++) {
			wait *= growth;
			if (most > 0 && wait >= most) return most;
		}
		return most > 0 && wait > most ? most : wait;
	}
};

class RetryGate {
public:
	// It was not usable yet at `now`: not to be tried again before the rule's
	// delay for this failure has passed.
	void Failed(double now, const RetryRule& rule)
	{
		failures_++;
		next_ = now + rule.DelayAfter(failures_);
	}
	// A try is allowed at `now` and after, and the failures start again.
	void Reset(double now)
	{
		failures_ = 0;
		next_ = now;
	}
	bool Ready(double now) const { return !(now < next_); }
	int Failures() const { return failures_; }

private:
	double next_ = 0;
	int failures_ = 0;
};

} // namespace sfve
