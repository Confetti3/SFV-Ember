#include "core/PlaybackSnapshots.hxx"

#include <algorithm>
#include <utility>

namespace sfve {

void PlaybackSnapshots::Clear(int game)
{
	game_ = game;
	count_ = 0;
	spacing_ = firstSpacing_;
}

bool PlaybackSnapshots::Due(int frame) const
{
	const Snapshot* before = nullptr;
	for (size_t i = 0; i < count_ && held_[i].frame <= frame; i++) before = &held_[i];
	return !before || frame - before->frame >= spacing_;
}

void PlaybackSnapshots::Take(const StateLayout& layout, int frame)
{
	if (count_ >= most_) Thin();
	// In frame order: a seek back leaves later states held, and a frame
	// between two held ones may be due again.
	size_t at = 0;
	while (at < count_ && held_[at].frame < frame) at++;
	if (held_.size() <= count_) held_.emplace_back();
	// The spare buffer at the end moves to its place; those after it shift up.
	std::rotate(held_.begin() + at, held_.begin() + count_, held_.begin() + count_ + 1);
	Snapshot& snapshot = held_[at];
	snapshot.frame = frame;
	snapshot.bytes.resize(layout.TotalSize());
	layout.SaveTo(snapshot.bytes.data());
	count_++;
}

const PlaybackSnapshots::Snapshot* PlaybackSnapshots::NearestBefore(int frame) const
{
	const Snapshot* best = nullptr;
	for (size_t i = 0; i < count_ && held_[i].frame < frame; i++) best = &held_[i];
	return best;
}

std::vector<int> PlaybackSnapshots::Frames() const
{
	std::vector<int> frames;
	for (size_t i = 0; i < count_; i++) frames.push_back(held_[i].frame);
	return frames;
}

void PlaybackSnapshots::Thin()
{
	// The kept ones move to the front; the dropped ones' buffers go behind
	// them, spare.
	size_t kept = 0;
	for (size_t i = 0; i < count_; i += 2) {
		if (kept != i) std::swap(held_[kept], held_[i]);
		kept++;
	}
	count_ = kept;
	spacing_ *= 2;
}

} // namespace sfve
