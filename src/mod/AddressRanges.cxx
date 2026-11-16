#include "mod/AddressRanges.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <iterator>

namespace sfve::mod {

namespace {

std::atomic<const AddressRanges*> s_published{nullptr};

} // namespace

AddressRanges AddressRanges::Joined(std::vector<AddressRange> committed, uint64_t joinGap)
{
	std::sort(committed.begin(), committed.end(), [](const AddressRange& a, const AddressRange& b) { return a.begin < b.begin; });
	AddressRanges joined;
	for (const AddressRange& range : committed) {
		if (range.end <= range.begin) continue;
		if (!joined.ranges_.empty() && range.begin <= joined.ranges_.back().end + joinGap) {
			joined.ranges_.back().end = std::max(joined.ranges_.back().end, range.end);
		} else {
			joined.ranges_.push_back(range);
		}
	}
	return joined;
}

bool AddressRanges::Contains(uint64_t value) const
{
	// The last range that begins at or before `value`.
	const auto next = std::upper_bound(ranges_.begin(), ranges_.end(), value,
		[](uint64_t v, const AddressRange& range) { return v < range.begin; });
	return next != ranges_.begin() && value < std::prev(next)->end;
}

AddressRanges ReadProcessRanges()
{
	SYSTEM_INFO system;
	GetSystemInfo(&system);
	std::vector<AddressRange> committed;
	const uint64_t last = reinterpret_cast<uint64_t>(system.lpMaximumApplicationAddress);
	uint64_t at = reinterpret_cast<uint64_t>(system.lpMinimumApplicationAddress);
	while (at < last) {
		MEMORY_BASIC_INFORMATION info;
		if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) break;
		const uint64_t begin = reinterpret_cast<uint64_t>(info.BaseAddress);
		const uint64_t end = begin + info.RegionSize;
		if (info.State == MEM_COMMIT && (info.Type == MEM_PRIVATE || info.Type == MEM_IMAGE)) committed.push_back({begin, end});
		if (end <= at) break;
		at = end;
	}
	return AddressRanges::Joined(std::move(committed), kJoinGap);
}

void PublishProcessRanges(AddressRanges ranges)
{
	// A reader on another thread may still hold the set it fetched, so a
	// replaced set is kept (a few KB per battle).
	s_published.store(new AddressRanges(std::move(ranges)), std::memory_order_release);
}

const AddressRanges* PublishedProcessRanges()
{
	return s_published.load(std::memory_order_acquire);
}

void RefreshProcessRanges()
{
	AddressRanges ranges = ReadProcessRanges();
	const std::vector<AddressRange>& list = ranges.Ranges();
	const uint64_t image = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
	if (list.empty()) {
		Log("address test: no committed memory read; only words above 1 TB count as addresses");
	} else {
		Log("address test: %zu joined ranges of committed private memory and images, 0x%llX to 0x%llX; the exe's image at 0x%llX "
			"(%s 1 TB); words above 1 TB also count",
			list.size(), (unsigned long long)list.front().begin, (unsigned long long)list.back().end, (unsigned long long)image,
			image >= (1ull << 40) ? "above" : "below");
	}
	PublishProcessRanges(std::move(ranges));
}

} // namespace sfve::mod
