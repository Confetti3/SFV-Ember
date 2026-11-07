#pragma once

// Keeps a battle's frames as its RollbackSession plays them, for a replay
// (EMBER_LIBRARY.md section 26). The session reports each frame it plays and
// each checkpoint's hash (Config::onFrame, onCheckpoint), a rollback's
// replay of a frame again; the recorder keeps the last report of each. The
// caller says after each step how far the frames are final (Confirm): played
// and confirmed, so their last report is of the confirmed inputs and no
// rollback can report them again. Only those are taken.
//
// Game thread only: the session reports inside its own calls.

#include "core/ReplayFile.hxx"
#include "core/ReplayFrames.hxx"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

namespace sfve::replay {

class Recorder {
public:
	// A new battle's recording.
	void Start()
	{
		frames_.clear();
		checkpoints_.clear();
		final_ = 0;
		broken_ = false;
		active_ = true;
	}
	void Stop() { active_ = false; }
	bool Active() const { return active_; }
	// A frame was reported out of order (not the next frame or one already
	// reported), or past the most a replay holds: nothing is saved.
	bool Broken() const { return broken_; }

	void OnFrame(int frame, const RecordedFrame& played)
	{
		if (!active_ || broken_) return;
		if (frame < 0 || (size_t)frame > frames_.size() || (size_t)frame >= kMostFrames) {
			broken_ = true;
			return;
		}
		if ((size_t)frame == frames_.size()) frames_.push_back(played);
		else frames_[(size_t)frame] = played;
	}
	void OnCheckpoint(int frame, uint64_t hash)
	{
		if (active_ && frame >= 0) checkpoints_[frame] = hash;
	}

	// The frames before `frames` are final (RollbackSession: played, and
	// confirmed: min(Frame(), LastConfirmedFrame() + 1)). Never goes back.
	void Confirm(int frames)
	{
		if (!active_ || frames <= 0) return;
		final_ = std::max(final_, std::min((size_t)frames, frames_.size()));
	}
	size_t FinalFrames() const { return final_; }

	// The final frames, and the checkpoints they reach (frame N is the state
	// after N frames, so N up to the final count).
	void Take(ReplayFrames* frames, std::vector<std::pair<int, uint64_t>>* checkpoints) const
	{
		frames->assign(frames_.begin(), frames_.begin() + (ptrdiff_t)final_);
		checkpoints->clear();
		for (const auto& [frame, hash] : checkpoints_) {
			if ((size_t)frame <= final_) checkpoints->push_back({frame, hash});
		}
	}

private:
	ReplayFrames frames_;
	std::map<int, uint64_t> checkpoints_;
	size_t final_ = 0;
	bool broken_ = false;
	bool active_ = false;
};

} // namespace sfve::replay
