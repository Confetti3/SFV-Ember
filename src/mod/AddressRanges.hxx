#pragma once

// Which words of the game's state are addresses (LooksLikeAddress,
// mod/GameMemory.hxx): the hashes read them as zero, since two processes
// place the same objects at different addresses. The decision must be the
// same on two copies whose values are equal, or a word is zeroed in one
// copy's hash and not in the other's: a desync report with equal states
// (P2-E). So there are two rules, chosen at the battle's bind from where the
// game's own heap lies (ChooseAddressRule), and logged ("address test:"):
//
// * The value rule (AddressRule::Value): a word is an address when it is
//   4-byte aligned and lies above 1 TB, up to the end of user space. A
//   Windows x64 process with its default ASLR puts its heap and images there,
//   which is every PC seen so far, and this is exactly beta.9's rule: it reads
//   only the value, so two such copies always decide alike.
// * The low-heap rule (AddressRule::LowHeap), only for a copy whose game heap
//   lies below 1 TB (bottom-up ASLR off, Wine and Proton): the value rule, and
//   also a word inside one of the game allocator's arenas, read at the bind.
//   An arena is a run of committed private memory, joined across gaps of at
//   most kJoinGap, that holds one of the game's own heap objects (the
//   battle's units and its rollback regions). Thread stacks (an allocation
//   with a guard page), images and mapped views are never part of one, and
//   no arena is joined across them.
//
// Under Wine and Proton the heap lies low because Wine places memory bottom
// up with no randomization. There the mod asks for every later allocation
// top down (mod/WineMemory.hxx), which puts the game's heap above 1 TB as on
// Windows, so the value rule applies and a Linux copy decides as a Windows
// one does. What was there before the game ran stays low: the heaps then
// made and the images Wine loads at their preferred bases (the game's exe at
// 0x140000000). Their reservations are the start ranges (SetStartRanges),
// which every rule also takes as addresses, and which are empty on Windows.
// The two arena rules below then only serve a low heap the top-down
// allocations did not lift.
//
// What remains unsafe: a low-heap copy against a normal one can still part on
// an integer that falls inside the low copy's arenas (a count of 0x02A41F10
// is a number to the normal copy and an address to the low one), and two
// low-heap copies whose arenas differ can too. Private memory that is not the
// game's heap (a TEB, another allocator's block) lying within kJoinGap of an
// arena with nothing between is taken as arena. The start agreement does not
// carry the rule (that would change its wire and need a players' notice for
// the refused match), so such a pair plays, and a difference shows as a
// desync whose logs say which rule each copy took.

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

// How far apart two runs of the game's heap may lie and still be one arena.
constexpr uint64_t kJoinGap = 16ull << 20;
// Where the value rule's addresses begin.
constexpr uint64_t kHighAddresses = 1ull << 40;

enum class AddressRule : uint8_t {
	Value,   // words above 1 TB only (beta.9's rule)
	LowHeap, // also words in the game heap's arenas (a copy whose heap lies below 1 TB)
};

// The rule the game's own heap objects call for: LowHeap only when there are
// some and every one lies below 1 TB; a mix, or none, is the value rule.
AddressRule RuleForGameHeap(const std::vector<uint64_t>& gameHeap);

// A committed region as VirtualQuery reports it, as far as the arenas need.
struct QueriedRegion {
	uint64_t begin = 0;
	uint64_t end = 0;
	uint64_t allocationBase = 0;
	bool privateMemory = false; // MEM_PRIVATE (not an image or a mapped view)
	bool guard = false;         // PAGE_GUARD: its allocation is a thread's stack
};

// The game allocator's arenas among `committed` (any order): the private
// regions of no allocation that holds a guard page, joined across gaps of at
// most `joinGap` that no other committed region lies in, and of those runs
// only the ones holding one of `gameHeap`.
AddressRanges ArenasHolding(std::vector<QueriedRegion> committed, const std::vector<uint64_t>& gameHeap, uint64_t joinGap);

// This process's committed regions (VirtualQuery over user space).
std::vector<QueriedRegion> ReadCommittedRegions();

// One allocation as VirtualQuery reports it: from its AllocationBase to the
// end of its last region, reserved or committed.
struct Reservation {
	uint64_t begin = 0;
	uint64_t end = 0;
	bool image = false; // MEM_IMAGE: a loaded exe or DLL
};

// The start ranges (see the top): of `reservations`, those lying wholly below
// 1 TB that are an image or hold one of `heaps` (GetProcessHeaps' handles).
// Pure.
AddressRanges StartRanges(const std::vector<Reservation>& reservations, const std::vector<uint64_t>& heaps);

// This process's allocations (VirtualQuery over user space, one entry per
// AllocationBase).
std::vector<Reservation> ReadReservations();

// The start ranges, set once before any battle (mod/WineMemory.hxx); every
// rule's published set holds them. Empty unless set.
void SetStartRanges(AddressRanges ranges);
const AddressRanges& CurrentStartRanges();

// What LooksLikeAddress consults besides the value rule; none until the first
// publish. Safe to call from any thread: a published set is never freed.
void PublishProcessRanges(AddressRanges ranges);
const AddressRanges* PublishedProcessRanges();

// At the battle's bind, with the game's own heap objects (its units and its
// rollback regions): chooses the rule (RuleForGameHeap), publishes the start
// ranges with the arenas for the low-heap rule and alone for the value rule, and logs
// which ("address test:"). Game thread.
AddressRule ChooseAddressRule(const std::vector<uint64_t>& gameHeap);
// The rule last chosen (Value before any); any thread.
AddressRule CurrentAddressRule();
// The rule in words, for the log and a desync report's head: "value (words
// above 1 TB)" or "low heap (words above 1 TB, and in 3 arena(s) from
// 0x2000000 to 0x9F00000, 120 MB)". Any thread.
std::string AddressRuleText();

} // namespace sfve::mod
