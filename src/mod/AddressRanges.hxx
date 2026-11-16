#pragma once

// Which memory of this process holds addresses: the ranges LooksLikeAddress
// (mod/GameMemory.hxx) takes a word to point into. A word is an address when
// it is 4-byte aligned and lies above 1 TB (where a Windows x64 process puts
// its heap and images with bottom-up and high-entropy ASLR on) or inside the
// committed private memory and images this process really has (read at the
// battle's bind). The ranges are what make a PC with those ASLR options off,
// or Wine and Proton, whose heap sits far below 1 TB, read its pointers as
// pointers.

#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

struct AddressRange {
	uint64_t begin = 0; // first byte
	uint64_t end = 0;   // one past the last
};

class AddressRanges {
public:
	// `committed` in any order, overlapping or not: sorted, and ranges
	// closer together than `joinGap` bytes joined (a heap grows into the
	// space beside what it has, so a pointer to memory made after the
	// ranges were read falls in a gap).
	static AddressRanges Joined(std::vector<AddressRange> committed, uint64_t joinGap);

	bool Contains(uint64_t value) const;
	const std::vector<AddressRange>& Ranges() const { return ranges_; }

private:
	std::vector<AddressRange> ranges_; // sorted, disjoint
};

// The committed private regions and images of this process (VirtualQuery),
// joined by kJoinGap.
constexpr uint64_t kJoinGap = 16ull << 20;
AddressRanges ReadProcessRanges();

// What LooksLikeAddress consults; none until the first publish. Safe to call
// from any thread: a published set is never freed.
void PublishProcessRanges(AddressRanges ranges);
const AddressRanges* PublishedProcessRanges();

// Reads this process's ranges, publishes them and logs where they lie (the
// lowest and highest address of the committed private memory and images, the
// exe's image and how many joined ranges), so a field log shows what the address test saw.
void RefreshProcessRanges();

} // namespace sfve::mod
