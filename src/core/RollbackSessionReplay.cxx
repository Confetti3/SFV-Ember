// RollbackSession: a replay's frames (Config::replay), a lagged replay's
// checkpoints and the replay check's log. RestoreReplay is ReplaySeeker.cxx's.

#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"
#include "core/SyncTest.hxx"

#include <stdio.h>
#include <string.h>

namespace sfve {

bool RollbackSession::ReplayInputs(bool held, FrameInputs* inputs)
{
	const ReplayFrames& frames = *config_.replay;
	char why[128];
	if (frame_ < 0 || (size_t)frame_ >= frames.size()) {
		snprintf(why, sizeof(why), "the replay ends at frame %zu", frames.size());
		Fail(why);
		return false;
	}
	const RecordedFrame& recorded = frames[(size_t)frame_];
	// The game decides each frame's kind from its own state (the WIN held,
	// the fight reading inputs). One the recording did not have means the
	// playback is no longer the recorded battle. A lagged replay's live state
	// may be a guess, so its kinds are not compared.
	const bool fight = !held && game_->InputsMatter();
	if (!lagged_ && (recorded.Held() != held || (!held && recorded.Fight() != fight))) {
		snprintf(why, sizeof(why), "the playback left the recording at frame %d (recorded %s, played %s)", frame_,
			recorded.Held() ? "held" : recorded.Fight() ? "fight" : "ignored", held ? "held" : fight ? "fight" : "ignored");
		Fail(why);
		return false;
	}
	*inputs = recorded.inputs;
	return true;
}

// A lagged replay's checkpoints, each once its state is final: the
// recording's states, if the rollbacks restored everything.
void RollbackSession::HashLagged()
{
	StateLayout& layout = game_->Layout();
	Slot& slot = pool_[0];
	for (int frame = laggedHashed_ + 1; frame <= lagged_->ConfirmedThrough(); frame++) {
		if (frame % kCheckpointInterval != 0 && frame % config_.hashEveryFrames != 0) continue;
		if (const uint8_t* bytes = lagged_->SavedAt(frame)) {
			memcpy(slot.bytes.data(), bytes, layout.TotalSize());
		} else if (frame == frame_) {
			layout.SaveTo(slot.bytes.data());
		} else {
			CoreLog("replay lag: the state at frame %d was not kept; not hashed", frame);
			continue;
		}
		slot.frame = frame;
		slot.used = true;
		slot.copied = true;
		RecordHash(frame, slot.bytes.data(), 0);
	}
	if (lagged_->ConfirmedThrough() > laggedHashed_) laggedHashed_ = lagged_->ConfirmedThrough();
}

// Each rollback whose replay differed: the frame and the regions; a region's
// first differing words the first time it differs (SyncTestMode's learning
// log, in the same form).
void RollbackSession::LogReplayCheck()
{
	const SyncTestReport& r = replayCheck_->Report();
	if (r.failures == replayCheckFailures_) return;
	replayCheckFailures_ = r.failures;
	std::string names;
	for (const RegionDiff& d : r.diffs) names += (names.empty() ? "" : ", ") + d.region;
	CoreLog("replay check: frame %d differs after rollback %d: %s", r.failedFrame, r.rollbacks, names.c_str());
	for (const RegionDiff& d : r.diffs) {
		if (!replayCheckLogged_.insert(d.region).second) continue;
		CoreLog("learn: %s differs at frame %d: %zu byte(s) from +0x%zX", d.region.c_str(), r.failedFrame, d.differingBytes,
			d.firstOffset);
		for (const WordDiff& w : d.words) {
			CoreLog("learn:     +0x%04zX  played %016llX  replayed %016llX", w.offset, (unsigned long long)w.saved,
				(unsigned long long)w.live);
		}
	}
}

} // namespace sfve
