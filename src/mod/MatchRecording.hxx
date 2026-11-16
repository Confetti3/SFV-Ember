#pragma once

// The room battle being recorded (EMBER_LIBRARY.md section 26), for
// NetplayMode: its frames as its session reports them (Config::onFrame,
// onCheckpoint), kept as they become final, its header (made as its session
// starts), the end of each game in it, and the replay saved as the battle
// closes. Whole once its last WIN went back.

#include "core/ReplayRecorder.hxx"
#include "core/RollbackSession.hxx"

#include <stdint.h>

namespace sfve::mod {

class MatchRecording {
public:
	bool Active() const { return recorder_.Active(); }
	// The battle's first frame: recording begins, with an empty header, not whole.
	void Start();
	// Recording ends without a replay (the match is not known).
	void Stop() { recorder_.Stop(); }
	// What the session reports, as it plays.
	void OnFrame(int frame, const RecordedFrame& played) { recorder_.OnFrame(frame, played); }
	void OnCheckpoint(int frame, uint64_t hash) { recorder_.OnCheckpoint(frame, hash); }
	// The replay's header, filled as the session starts.
	replay::Header& Header() { return recording_; }
	// The recording's last WIN went back: the replay is whole.
	void MarkWhole() { recordingWhole_ = true; }
	// The frames `session` (the battle's, or the one kept for its watchers)
	// has made final so far: played, and confirmed on both fighters' copies.
	void Confirm(const RollbackSession* session);
	// A game of the match ended at `endFrame`, with the match's result.
	void NoteGame(int endFrame);
	// The battle closes: the replay is saved, unless its frames came out of
	// order or there are none.
	void Finish();

private:
	replay::Recorder recorder_;
	replay::Header recording_;
	bool recordingWhole_ = false;
};

} // namespace sfve::mod
