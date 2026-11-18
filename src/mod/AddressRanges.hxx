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
// The start ranges and the arenas are this process's own, so on their own
// they would part two copies on a number: 0x30000 (a 16.16 value of 3.0 in
// CharaMotionPart) lies in a Proton copy's first heap and is zeroed in its
// hash, while a Windows copy hashes it as the number it is. Equal states,
// unequal hashes: the false desyncs of 2026-11-17 (a Proton Karin against
// Windows copies, EMBER-122, the Proton copy the odd one out in every match).
// So the copies agree on the ranges before frame 0 (core/StartAgreement.hxx):
// each fighter sends its own (RangesToShare), and every copy of the match
// takes both fighters' as addresses (AdoptAgreedRanges), so a word in either
// fighter's ranges is zeroed in every copy's hash alike. A Windows copy's own
// are none, so two Windows copies hash as they always did.
//
// What remains: a watcher's own ranges are not the fighters' (they never wait
// for a watcher), so a Proton watcher of a Windows match can still see a
// false mismatch, which is a watcher's diagnostic only; and a session that
// started without the agreement (no answer in time) keeps each copy's own.
// The report lists the words each copy zeroed below 1 TB, so such a case
// shows for what it is (adapter::desync::WordLines).

#include "core/StateRegions.hxx"

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod {

class AddressRanges {
public:
	// `committed` in any order, overlapping or not: sorted, and ranges
	// closer together than `joinGap` bytes joined (a heap grows into the
	// space beside what it has, so a pointer to memory made after the
	// ranges were read falls in a gap).
	static AddressRanges Joined(std::vector<AddressRange> committed, uint64_t joinGap);

	bool Contains(uint64_t value) const;
	// At most `most` ranges holding every address these hold: past `most`,
	// the neighbours with the smallest gap between them are joined (more is
	// taken as addresses, never less). Pure.
	AddressRanges Fitted(size_t most) const;
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

// What this copy sends the others before frame 0 (core/StartAgreement.hxx):
// the published set's ranges below 1 TB (the start ranges, and the arenas
// under the low-heap rule), at most `most` (Fitted). None on a Windows copy
// whose heap lies above 1 TB. Game thread, after ChooseAddressRule.
std::vector<AddressRange> RangesToShare(size_t most);
// Takes `agreed` (the fighters' ranges as the start agreement settled them,
// this copy's own among them) as addresses on top of the published set, and
// logs it ("address test:"); the state hash changes with it. The next
// battle's ChooseAddressRule drops it again. Game thread.
void AdoptAgreedRanges(const std::vector<AddressRange>& agreed);

// The rule in words, for the log and a desync report's head: "value (words
// above 1 TB)" or "low heap (words above 1 TB, and in 3 arena(s) from
// 0x2000000 to 0x9F00000, 120 MB)", with the start ranges and the agreed
// ranges when there are any. Any thread.
std::string AddressRuleText();
// Every range below 1 TB this copy's hashes zero words in, "0x30000-0x130000,
// 0x140000000-0x14441F000", or "none". Any thread.
std::string LowRangesText();

} // namespace sfve::mod
