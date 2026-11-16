#include "mod/AddressRanges.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <set>

namespace sfve::mod {

namespace {

std::atomic<const AddressRanges*> s_published{nullptr};
std::atomic<AddressRule> s_rule{AddressRule::Value};

} // namespace

AddressRanges AddressRanges::Joined(std::vector<AddressRange> committed, uint64_t joinGap)
{
	std::sort(committed.begin(), committed.end(), [](const AddressRange& a, const AddressRange& b) { return a.begin < b.begin; });
	AddressRanges joined;
	for (const AddressRange& range : committed) {
		if (range.end <= range.begin) continue;
		if (!joined.ranges_.empty() && range.begin <= joined.ranges_.back().end + joinGap) {
			joined.ranges_.back().end = (std::max)(joined.ranges_.back().end, range.end);
		} else {
			joined.ranges_.push_back(range);
		}
	}
	return joined;
}

bool AddressRanges::Contains(uint64_t value) const
{
	// Most words tested are small numbers, below every range.
	if (ranges_.empty() || value < ranges_.front().begin || value >= ranges_.back().end) return false;
	// The last range that begins at or before `value`.
	const auto next = std::upper_bound(ranges_.begin(), ranges_.end(), value,
		[](uint64_t v, const AddressRange& range) { return v < range.begin; });
	return next != ranges_.begin() && value < std::prev(next)->end;
}

AddressRule RuleForGameHeap(const std::vector<uint64_t>& gameHeap)
{
	if (gameHeap.empty()) return AddressRule::Value;
	for (const uint64_t object : gameHeap) {
		if (object >= kHighAddresses) return AddressRule::Value;
	}
	return AddressRule::LowHeap;
}

AddressRanges ArenasHolding(std::vector<QueriedRegion> committed, const std::vector<uint64_t>& gameHeap, uint64_t joinGap)
{
	std::sort(committed.begin(), committed.end(), [](const QueriedRegion& a, const QueriedRegion& b) { return a.begin < b.begin; });
	std::set<uint64_t> stacks; // the allocations holding a guard page
	for (const QueriedRegion& r : committed) {
		if (r.guard) stacks.insert(r.allocationBase);
	}
	// Runs of arena regions; any other committed region ends a run, so no run
	// reaches over a stack, an image or a mapped view.
	std::vector<AddressRange> runs;
	bool open = false;
	for (const QueriedRegion& r : committed) {
		if (r.end <= r.begin) continue;
		const bool arena = r.privateMemory && !r.guard && stacks.count(r.allocationBase) == 0;
		if (!arena) {
			open = false;
			continue;
		}
		if (open && r.begin <= runs.back().end + joinGap) {
			runs.back().end = (std::max)(runs.back().end, r.end);
		} else {
			runs.push_back({r.begin, r.end});
			open = true;
		}
	}
	std::vector<AddressRange> held;
	for (const AddressRange& run : runs) {
		for (const uint64_t object : gameHeap) {
			if (object >= run.begin && object < run.end) {
				held.push_back(run);
				break;
			}
		}
	}
	return AddressRanges::Joined(std::move(held), 0);
}

std::vector<QueriedRegion> ReadCommittedRegions()
{
	SYSTEM_INFO system;
	GetSystemInfo(&system);
	std::vector<QueriedRegion> committed;
	const uint64_t last = reinterpret_cast<uint64_t>(system.lpMaximumApplicationAddress);
	uint64_t at = reinterpret_cast<uint64_t>(system.lpMinimumApplicationAddress);
	while (at < last) {
		MEMORY_BASIC_INFORMATION info;
		if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) break;
		const uint64_t begin = reinterpret_cast<uint64_t>(info.BaseAddress);
		const uint64_t end = begin + info.RegionSize;
		if (info.State == MEM_COMMIT) {
			QueriedRegion r;
			r.begin = begin;
			r.end = end;
			r.allocationBase = reinterpret_cast<uint64_t>(info.AllocationBase);
			r.privateMemory = info.Type == MEM_PRIVATE;
			r.guard = (info.Protect & PAGE_GUARD) != 0;
			committed.push_back(r);
		}
		if (end <= at) break;
		at = end;
	}
	return committed;
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

AddressRule ChooseAddressRule(const std::vector<uint64_t>& gameHeap)
{
	uint64_t lowest = UINT64_MAX, highest = 0;
	for (const uint64_t object : gameHeap) {
		lowest = (std::min)(lowest, object);
		highest = (std::max)(highest, object);
	}
	if (gameHeap.empty()) lowest = 0;
	const AddressRule rule = RuleForGameHeap(gameHeap);
	// The ranges first, then the rule: the value rule's empty set drops the
	// last battle's arenas.
	PublishProcessRanges(rule == AddressRule::LowHeap ? ArenasHolding(ReadCommittedRegions(), gameHeap, kJoinGap) : AddressRanges());
	s_rule.store(rule, std::memory_order_release);
	if (rule == AddressRule::Value) {
		Log("address test: %s; the game's heap objects lie from 0x%llX to 0x%llX%s", AddressRuleText().c_str(),
			(unsigned long long)lowest, (unsigned long long)highest,
			lowest < kHighAddresses && !gameHeap.empty() ? ", not all below 1 TB" : "");
	} else {
		Log("address test: %s; the game's heap lies below 1 TB (its objects from 0x%llX to 0x%llX). A copy whose heap lies "
			"above 1 TB reads a number in these arenas as a number, so the two can part on such a word",
			AddressRuleText().c_str(), (unsigned long long)lowest, (unsigned long long)highest);
	}
	return rule;
}

AddressRule CurrentAddressRule()
{
	return s_rule.load(std::memory_order_acquire);
}

std::string AddressRuleText()
{
	if (CurrentAddressRule() == AddressRule::Value) return "value (words above 1 TB)";
	const AddressRanges* arenas = PublishedProcessRanges();
	if (!arenas || arenas->Ranges().empty()) return "low heap (words above 1 TB; no arena found)";
	const std::vector<AddressRange>& list = arenas->Ranges();
	uint64_t bytes = 0;
	for (const AddressRange& r : list) bytes += r.end - r.begin;
	char text[160];
	snprintf(text, sizeof(text), "low heap (words above 1 TB, and in %zu arena(s) from 0x%llX to 0x%llX, %llu MB)", list.size(),
		(unsigned long long)list.front().begin, (unsigned long long)list.back().end, (unsigned long long)(bytes >> 20));
	return text;
}

} // namespace sfve::mod
