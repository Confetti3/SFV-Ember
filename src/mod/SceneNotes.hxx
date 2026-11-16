#pragma once

// What NetplayMode logs of a battle's scenes, for the log only (nothing here
// steers the battle): where the scenes between two rounds or after the
// deciding one start as this copy steps or watches, each skip the adapter
// applied in them with its frame, their length when the match's end is taken
// (IntroSkip.hxx), and the first frame of each round's fight with the battle
// frame's state word (the pause, KO and slow-motion bits a skipped scene must
// leave clear).

#include "mod/IntroSkip.hxx"
#include "mod/SfvAdapter.hxx"

namespace sfve::mod {

class SceneNotes {
public:
	// A new battle: no scenes seen, no fight noted.
	void Reset();
	// The frame about to be stepped or watched: the scenes' start or end, if
	// either is here.
	void Note(SfvAdapter& adapter, int frame);
	// The skip the adapter applied in the frame just played (`played`), if any.
	void Skip(SfvAdapter& adapter, int played);
	// The match's end taken at `frame`: how long its scenes took.
	void End(int frame);
	// The frame about to be stepped or watched: true (logged) when it is the
	// first of a round's fight, once per fight.
	bool FightStart(SfvAdapter& adapter, int frame);
	// Where this copy saw the decided match's scenes start (-1: not in them).
	int From() const { return scenes_.From(); }
	int Shown(int frame) const { return scenes_.Shown(frame); }

private:
	intro_skip::ScenesClock scenes_; // where this copy saw the decided match's scenes start
	int scenesSkips_ = 0;            // skips the adapter applied in them
	bool roundScenes_ = false;       // ...which are the scenes between two rounds, not the match's
	bool fightSeen_ = false;         // the fight's first frame was noted (FightStart)
};

} // namespace sfve::mod
