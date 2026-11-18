#include "mod/AddressRanges.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <set>

namespace sfve::mod {

namespace {

std::atomic<const AddressRanges*> s_published{nullptr};
std::atomic<AddressRule> s_rule{AddressRule::Value};
AddressRanges s_start; // set before any battle, then only read
// For the rule's text: this battle's arenas (ChooseAddressRule) and the
// ranges the start agreement added (AdoptAgreedRanges). Each is replaced
// whole and never freed, so a reader keeps the set it fetched.
std::atomic<const AddressRanges*> s_arenas{nullptr};
std::atomic<const AddressRanges*> s_agreed{nullptr};

// `a` and `b` as one set.
AddressRanges Union(const AddressRanges& a, const AddressRanges& b)
{
	std::vector<AddressRange> all = a.Ranges();
	all.insert(all.end(), b.Ranges().begin(), b.Ranges().end());
	return AddressRanges::Joined(std::move(all), 0);
}

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

AddressRanges AddressRanges::Fitted(size_t most) const
{
	AddressRanges fitted = *this;
	std::vector<AddressRange>& r = fitted.ranges_;
	while (most && r.size() > most) {
		// The two neighbours closest together become one.
		size_t closest = 0;
		for (size_t i = 1; i + 1 < r.size(); i++) {
			if (r[i + 1].begin - r[i].end < r[closest + 1].begin - r[closest].end) closest = i;
		}
		r[closest].end = r[closest + 1].end;
		r.erase(r.begin() + (ptrdiff_t)closest + 1);
	}
	if (!most) r.clear();
	return fitted;
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

AddressRanges StartRanges(const std::vector<Reservation>& reservations, const std::vector<uint64_t>& heaps)
{
	std::vector<AddressRange> kept;
	for (const Reservation& r : reservations) {
		if (r.end <= r.begin || r.end > kHighAddresses) continue;
		bool keep = r.image;
		for (const uint64_t heap : heaps) keep = keep || (heap >= r.begin && heap < r.end);
		if (keep) kept.push_back({r.begin, r.end});
	}
	return AddressRanges::Joined(std::move(kept), 0);
}

std::vector<Reservation> ReadReservations()
{
	SYSTEM_INFO system;
	GetSystemInfo(&system);
	std::vector<Reservation> reservations;
	const uint64_t last = reinterpret_cast<uint64_t>(system.lpMaximumApplicationAddress);
	uint64_t at = reinterpret_cast<uint64_t>(system.lpMinimumApplicationAddress);
	while (at < last) {
		MEMORY_BASIC_INFORMATION info;
		if (!VirtualQuery(reinterpret_cast<void*>(at), &info, sizeof(info))) break;
		const uint64_t begin = reinterpret_cast<uint64_t>(info.BaseAddress);
		const uint64_t end = begin + info.RegionSize;
		if (info.State != MEM_FREE) {
			const uint64_t base = reinterpret_cast<uint64_t>(info.AllocationBase);
			// An allocation's regions come in order, so its entry is the last one.
			if (!reservations.empty() && reservations.back().begin == base) {
				reservations.back().end = end;
			} else {
				reservations.push_back({base, end, info.Type == MEM_IMAGE});
			}
		}
		if (end <= at) break;
		at = end;
	}
	return reservations;
}

void SetStartRanges(AddressRanges ranges)
{
	s_start = std::move(ranges);
}

const AddressRanges& CurrentStartRanges()
{
	return s_start;
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
	// The ranges first, then the rule: the value rule's set (the start ranges
	// alone) drops the last battle's arenas.
	const AddressRanges arenas = rule == AddressRule::LowHeap ? ArenasHolding(ReadCommittedRegions(), gameHeap, kJoinGap) : AddressRanges();
	s_arenas.store(new AddressRanges(arenas), std::memory_order_release);
	s_agreed.store(nullptr, std::memory_order_release);
	PublishProcessRanges(Union(s_start, arenas));
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

std::vector<AddressRange> RangesToShare(size_t most)
{
	const AddressRanges* published = PublishedProcessRanges();
	std::vector<AddressRange> low;
	for (const AddressRange& r : published ? published->Ranges() : std::vector<AddressRange>()) {
		if (r.begin < kHighAddresses) low.push_back({r.begin, (std::min)(r.end, kHighAddresses)});
	}
	return AddressRanges::Joined(std::move(low), 0).Fitted(most).Ranges();
}

void AdoptAgreedRanges(const std::vector<AddressRange>& agreed)
{
	const AddressRanges taken = AddressRanges::Joined(agreed, 0);
	const AddressRanges* published = PublishedProcessRanges();
	// The agreed set first (the text reads it with the published set), then
	// the set the hashes read.
	s_agreed.store(new AddressRanges(taken), std::memory_order_release);
	PublishProcessRanges(published ? Union(*published, taken) : taken);
	Log("address test: %s, as the start agreement settled; below 1 TB: %s", AddressRuleText().c_str(), LowRangesText().c_str());
}

std::string AddressRuleText()
{
	char start[192] = "";
	if (!s_start.Ranges().empty()) {
		snprintf(start, sizeof(start), ", and in %zu start range(s) (heaps and images made before the game ran)", s_start.Ranges().size());
	}
	const AddressRanges* agreed = s_agreed.load(std::memory_order_acquire);
	if (agreed && !agreed->Ranges().empty()) {
		const size_t length = strlen(start);
		snprintf(start + length, sizeof(start) - length, ", and in the %zu range(s) the fighters agreed on before frame 0",
			agreed->Ranges().size());
	}
	if (CurrentAddressRule() == AddressRule::Value) return std::string("value (words above 1 TB") + start + ")";
	const AddressRanges* chosen = s_arenas.load(std::memory_order_acquire);
	const std::vector<AddressRange> arenas = chosen ? chosen->Ranges() : std::vector<AddressRange>();
	if (arenas.empty()) return std::string("low heap (words above 1 TB") + start + "; no arena found)";
	uint64_t bytes = 0;
	for (const AddressRange& r : arenas) bytes += r.end - r.begin;
	char text[160];
	snprintf(text, sizeof(text), "low heap (words above 1 TB, and in %zu arena(s) from 0x%llX to 0x%llX, %llu MB", arenas.size(),
		(unsigned long long)arenas.front().begin, (unsigned long long)arenas.back().end, (unsigned long long)(bytes >> 20));
	return std::string(text) + start + ")";
}

std::string LowRangesText()
{
	const AddressRanges* published = PublishedProcessRanges();
	std::string text;
	for (const AddressRange& r : published ? published->Ranges() : std::vector<AddressRange>()) {
		if (r.begin >= kHighAddresses) continue;
		char one[48];
		snprintf(one, sizeof(one), "%s0x%llX-0x%llX", text.empty() ? "" : ", ", (unsigned long long)r.begin, (unsigned long long)r.end);
		text += one;
	}
	return text.empty() ? "none" : text;
}

} // namespace sfve::mod
