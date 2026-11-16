#include "mod/TickCounts.hxx"

namespace sfve::mod {

std::string TickCounts::Note(int32_t frame, uint32_t ticks, bool replayed)
{
	updates_++;
	if (ticks == 0) none_++;
	if (ticks > 1) more_++;
	const int64_t before = last_;
	last_ = ticks;
	if (before == (int64_t)ticks) return {};
	changes_++;
	if (lines_ >= kMostLines) return {};
	lines_++;
	return "ticks: input frame " + std::to_string(frame) + (replayed ? " (replayed)" : "") + ": " + std::to_string(ticks) +
		" tick(s) an update from here" + (before < 0 ? std::string() : ", " + std::to_string(before) + " before");
}

std::string TickCounts::Summary() const
{
	if (!updates_) return {};
	return std::to_string(updates_) + " updates: " + std::to_string(none_) + " with no tick, " + std::to_string(more_) +
		" with more than one; " + std::to_string(changes_) + " change(s) of the count" +
		(changes_ > (uint64_t)kMostLines ? " (the first " + std::to_string(kMostLines) + " logged)" : std::string());
}

} // namespace sfve::mod
