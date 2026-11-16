// RollbackSession: the frames' hashes, the checkpoints' region hashes and
// kept states, and what the session answers of them.

#include "core/RollbackSession.hxx"

#include "core/CoreLog.hxx"

#include <windows.h>

namespace sfve {

namespace {

// Five minutes of checkpoints (about 550 regions x 16 bytes each: 8 MB), so a
// divergence that comes and goes is still found region by region after it.
// 900 at the interval of 20 (it was 300 at 60).
constexpr size_t kCheckpointsKept = 900;

} // namespace

bool RollbackSession::HashedFrame(int frame) const
{
	if (keptForWatchers_) return false; // no game's state to hash
	return frame % kCheckpointInterval == 0 || frame % config_.hashEveryFrames == 0;
}

void RollbackSession::HashConfirmed()
{
	if (!session_ || config_.watching) return;
	// The confirmed frame as of now, the local input added since GGPO's last
	// poll included (the GGPO port's confirmed-input-frame.patch): with the
	// last poll's number, a step's save would always be judged a frame late,
	// and with an input delay of 2 no save would ever be skipped.
	int confirmed = -1;
	if (ggpo_get_confirmed_input_frame(session_, &confirmed) == GGPO_OK ||
		ggpo_get_last_confirmed_frame(session_, &confirmed) == GGPO_OK) {
		if (confirmed > confirmed_) confirmed_ = confirmed;
	}
	// Oldest first: the checkpoints kept (CheckpointState) are the latest ones.
	for (;;) {
		Slot* next = nullptr;
		for (Slot& s : pool_) {
			if (!s.used || !s.pending || !Final(s.frame) || Replaced(s.frame)) continue;
			if (!next || s.frame < next->frame) next = &s;
		}
		if (!next) return;
		HashPending(*next);
	}
}

void RollbackSession::HashPending(Slot& slot)
{
	slot.pending = false;
	stats_.hashesOnConfirm++;
	game_->Layout().CountConfirmedHash();
	RecordHash(slot.frame, slot.bytes.data(), slot.copyTicks);
}

void RollbackSession::RecordHash(int frame, const uint8_t* bytes, long long copyTicks)
{
	StateLayout& layout = game_->Layout();
	const bool checkpoint = frame % kCheckpointInterval == 0;
	// A checkpoint's bytes are kept (CheckpointState), in the oldest of the
	// kept states (an empty one first) unless this frame's own (saved again
	// after a rollback) is there. A save with no copy has its state copied
	// there, its one copy.
	if (checkpoint) {
		KeptState* keep = nullptr;
		for (KeptState& kept : checkpointStates_) {
			if (kept.frame == frame) {
				keep = &kept;
				break;
			}
			if (!keep || kept.frame < keep->frame) keep = &kept;
		}
		keep->frame = frame;
		if (bytes) {
			keep->bytes.assign(bytes, bytes + layout.TotalSize());
		} else {
			LARGE_INTEGER from, copied;
			QueryPerformanceCounter(&from);
			keep->bytes.resize(layout.TotalSize());
			layout.SaveTo(keep->bytes.data());
			QueryPerformanceCounter(&copied);
			copyTicks = copied.QuadPart - from.QuadPart;
		}
		bytes = keep->bytes.data();
	}
	LARGE_INTEGER started, hashed, perSecond;
	QueryPerformanceCounter(&started);
	// The live state and its copy hash alike (StateLayout::HashLive).
	const uint64_t hash = bytes ? layout.HashSaved(bytes, HashScope::Peers) : layout.HashLive(HashScope::Peers);
	QueryPerformanceCounter(&hashed);
	history_[frame % kHistory] = {frame, hash};
	if (config_.keepAllHashes) {
		if ((size_t)frame >= allHashes_.size()) allHashes_.resize((size_t)frame + 1);
		allHashes_[frame] = {frame, hash};
	}
	if (checkpoint) {
		std::vector<RegionHash>& regions = checkpoints_[frame]; // a replay overwrites it
		regions.reserve(layout.Regions().size());
		layout.PeerRegionHashesSaved(bytes, regions);
		if (config_.onCheckpoint) config_.onCheckpoint(frame, hash); // RegionHashesAt(frame) answers
		QueryPerformanceFrequency(&perSecond);
		const auto us = [&](long long ticks) { return ticks * 1000000 / perSecond.QuadPart; };
		CoreLog("checkpoint: frame %d hash %016llx (copy %lld us, hash %lld us)", frame, (unsigned long long)hash,
			us(copyTicks), us(hashed.QuadPart - started.QuadPart));
		while (checkpoints_.size() > kCheckpointsKept) checkpoints_.erase(checkpoints_.begin());
	}
}

bool RollbackSession::HashAt(int frame, uint64_t* out) const
{
	if (frame < 0) return false;
	const HashRecord& r = (size_t)frame < allHashes_.size() ? allHashes_[frame] : history_[frame % kHistory];
	if (r.frame != frame) return false;
	*out = r.hash;
	return true;
}

const uint8_t* RollbackSession::SavedState(int frame) const
{
	for (const Slot& s : pool_) {
		if (s.used && s.copied && s.frame == frame) return s.bytes.data();
	}
	return nullptr;
}

const std::vector<uint8_t>* RollbackSession::CheckpointState(int frame) const
{
	for (const KeptState& kept : checkpointStates_) {
		if (frame >= 0 && kept.frame == frame) return &kept.bytes; // -1: none kept there yet
	}
	return nullptr;
}

bool RollbackSession::RegionHashesAt(int frame, std::vector<RegionHash>* out) const
{
	const auto it = checkpoints_.find(frame);
	if (it == checkpoints_.end()) return false;
	*out = it->second;
	return true;
}

int RollbackSession::LatestCheckpoint(int atOrBefore) const
{
	int latest = -1;
	for (const auto& [frame, hashes] : checkpoints_) {
		if (frame <= atOrBefore) latest = frame;
	}
	return latest;
}

} // namespace sfve
