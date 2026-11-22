#include "mod/AddressRanges.hxx"
#include "mod/GameMemory.hxx"
#include "mod/WineMemory.hxx"

#include <windows.h>

#include <doctest/doctest.h>

#include <iterator>
#include <memory>
#include <vector>

using sfve::AddressRange;
using sfve::mod::AddressRanges;
using sfve::mod::AddressRule;
using sfve::mod::LooksLikeAddress;
using sfve::mod::QueriedRegion;

TEST_CASE("joined ranges: sorted, overlaps and near neighbours made one, far ones kept apart")
{
	const AddressRanges ranges = AddressRanges::Joined(
		{{0x5000000, 0x5100000}, {0x1000000, 0x2000000}, {0x1F00000, 0x2400000}, {0x2400000 + 0x1000, 0x2500000}, {0x9000000, 0x9000000}},
		0x10000);
	REQUIRE(ranges.Ranges().size() == 2);
	CHECK(ranges.Ranges()[0].begin == 0x1000000);
	CHECK(ranges.Ranges()[0].end == 0x2500000);
	CHECK(ranges.Ranges()[1].begin == 0x5000000);
	CHECK(ranges.Contains(0x1000000));
	CHECK(ranges.Contains(0x24FFFFF));
	CHECK_FALSE(ranges.Contains(0x2500000));
	CHECK_FALSE(ranges.Contains(0xFFFFFF));
	CHECK_FALSE(ranges.Contains(0x3000000));
	CHECK_FALSE(ranges.Contains(0x9000000)); // an empty range holds nothing
	CHECK_FALSE(AddressRanges().Contains(0x1000000));
}

TEST_CASE("published arenas make a low heap's words addresses, on top of the value rule")
{
	// A PC with bottom-up ASLR off, or Wine: the heap at 32 MB.
	const uint64_t heap = 0x02A41F10ull;
	sfve::mod::PublishProcessRanges(AddressRanges::Joined({{0x2000000, 0x4000000}}, 0));
	CHECK(LooksLikeAddress(heap));
	CHECK_FALSE(LooksLikeAddress(heap + 1)); // not 4-byte aligned
	CHECK_FALSE(LooksLikeAddress(0x1F4));    // a small number
	CHECK_FALSE(LooksLikeAddress(0x4000000)); // past the range's end
	CHECK(LooksLikeAddress(0x00000204F5F982A0ull)); // above 1 TB, as before
	CHECK_FALSE(LooksLikeAddress(0xFFFFFFFFFFFFFFF0ull)); // kernel space
	sfve::mod::PublishProcessRanges({});
	CHECK_FALSE(LooksLikeAddress(heap));
}

TEST_CASE("the rule follows the game's heap: low only when every object of it lies below 1 TB")
{
	using sfve::mod::RuleForGameHeap;
	CHECK(RuleForGameHeap({}) == AddressRule::Value);
	CHECK(RuleForGameHeap({0x00000204F5F98000ull, 0x00000204F6000000ull}) == AddressRule::Value);
	CHECK(RuleForGameHeap({0x02A41F10ull, 0x03000000ull}) == AddressRule::LowHeap);
	CHECK(RuleForGameHeap({0x02A41F10ull, 0x00000204F5F98000ull}) == AddressRule::Value); // a mix
}

TEST_CASE("two copies whose heap lies above 1 TB decide every word by its value alone")
{
	// Whatever this process's own memory holds, a normal copy publishes no
	// ranges: an integer is an address only above 1 TB, on every such copy.
	const auto local = std::make_unique<int[]>(16);
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull, 0x00000204F6000000ull}) == AddressRule::Value);
	CHECK(sfve::mod::CurrentAddressRule() == AddressRule::Value);
	CHECK(sfve::mod::AddressRuleText().rfind("value", 0) == 0);
	REQUIRE(sfve::mod::PublishedProcessRanges());
	CHECK(sfve::mod::PublishedProcessRanges()->Ranges().empty());
	const uint64_t own = reinterpret_cast<uint64_t>(local.get());
	CHECK(LooksLikeAddress(own) == (own >= sfve::mod::kHighAddresses));
	CHECK_FALSE(LooksLikeAddress(0x02A41F10ull));
	CHECK(LooksLikeAddress(0x00000204F5F982A0ull));
}

TEST_CASE("the arenas are the private runs that hold the game's heap, never a stack, an image or a run of nothing of it")
{
	const uint64_t mb = 1ull << 20;
	std::vector<QueriedRegion> committed = {
		// The game's heap: two private regions 7 MB apart, one run.
		{0x20 * mb, 0x21 * mb, 0x20 * mb, true, false},
		{0x28 * mb, 0x29 * mb, 0x28 * mb, true, false},
		// A thread's stack: its guard page and its committed pages, one allocation.
		{0x2C * mb, 0x2C * mb + 0x1000, 0x2B * mb, true, true},
		{0x2C * mb + 0x1000, 0x2D * mb, 0x2B * mb, true, false},
		// Private memory just past the stack: a run of its own, holding none of the game's heap.
		{0x2E * mb, 0x2F * mb, 0x2E * mb, true, false},
		// The exe's image, not private.
		{0x40 * mb, 0x48 * mb, 0x40 * mb, false, false},
		{0x49 * mb, 0x4A * mb, 0x49 * mb, true, false}, // private beside the image: its own run
		// Another arena of the game's, far off.
		{0x90 * mb, 0x98 * mb, 0x90 * mb, true, false},
	};
	const std::vector<uint64_t> gameHeap = {0x20 * mb + 0x40, 0x93 * mb};
	const AddressRanges arenas = sfve::mod::ArenasHolding(committed, gameHeap, sfve::mod::kJoinGap);
	REQUIRE(arenas.Ranges().size() == 2);
	CHECK(arenas.Ranges()[0].begin == 0x20 * mb);
	CHECK(arenas.Ranges()[0].end == 0x29 * mb);
	CHECK(arenas.Ranges()[1].begin == 0x90 * mb);
	CHECK(arenas.Contains(0x24 * mb)); // a gap the heap may grow into
	CHECK_FALSE(arenas.Contains(0x2C * mb + 0x2000)); // the stack
	CHECK_FALSE(arenas.Contains(0x2E * mb + 0x100));  // memory the game's objects are not in
	CHECK_FALSE(arenas.Contains(0x41 * mb));          // the image
	CHECK_FALSE(arenas.Contains(0x49 * mb + 0x100));
	CHECK(sfve::mod::ArenasHolding(committed, {}, sfve::mod::kJoinGap).Ranges().empty());
}

TEST_CASE("this process's arenas hold its heap and neither its stack nor its code")
{
	const auto heap = std::make_unique<int[]>(16);
	int local = 0;
	const AddressRanges arenas =
		sfve::mod::ArenasHolding(sfve::mod::ReadCommittedRegions(), {reinterpret_cast<uint64_t>(heap.get())}, sfve::mod::kJoinGap);
	CHECK(arenas.Contains(reinterpret_cast<uint64_t>(heap.get())));
	CHECK_FALSE(arenas.Contains(reinterpret_cast<uint64_t>(&local)));
	CHECK_FALSE(arenas.Contains(reinterpret_cast<uint64_t>(&sfve::mod::ReadCommittedRegions)));
}

namespace {
// EMBER-147 (Z41 v KEN, 0.1.2), P1: Windows with its heap below 1 TB. Four of
// its arenas' runs and the page holding three projectiles of
// ProjectileGroupUnit[0], 104 MB below the lowest arena; no registered run
// lies in that page.
const std::vector<QueriedRegion> kEmber147Committed = {
	{0x12FE0000, 0x12FF0000, 0x12FE0000, true, false}, // the three projectiles' page
	{0x19850000, 0x1CFB0000, 0x19850000, true, false}, // the battle frame's arena
	{0x32E90000, 0x36C31000, 0x32E90000, true, false}, // the group's run is in this one
	{0x7A1C0000, 0x7BE90000, 0x7A1C0000, true, false}, // the input unit's, and five more projectiles
};
// The battle frame, ProjectileGroupUnit[0]+0x280 and inputunit.counters, as P1 logged them.
const std::vector<uint64_t> kEmber147GameHeap = {0x1B0DD390, 0x35D87760, 0x7BE44600};
// The group's first eight pointers on P1, and on P2 (heap above 1 TB).
const uint64_t kEmber147P1Words[] = {0x12FE3400, 0x12FE2800, 0x12FE1000, 0x7A81EC00, 0x7A81D400, 0x7A81C800, 0x7A81C400, 0x7A81B800};
const uint64_t kEmber147P2Words[] = {0x1FD87345800ull, 0x1FD87342000ull, 0x1FD87347400ull, 0x1FD8734E800ull, 0x1FD8734E400ull,
	0x1FD87343400ull, 0x1FD87346400ull, 0x1FD0A101400ull};
} // namespace

TEST_CASE("a projectile its group points to is in an arena even when it registered no run (EMBER-147)")
{
	const std::vector<uint64_t> projectiles(std::begin(kEmber147P1Words), std::end(kEmber147P1Words));
	// 0.1.2: the registered objects alone leave the page out, so P1 hashed
	// the first three pointers as numbers; the other five lay in an arena
	// another object held.
	const AddressRanges registered = sfve::mod::ArenasHolding(kEmber147Committed, kEmber147GameHeap, sfve::mod::kJoinGap);
	REQUIRE(registered.Ranges().size() == 3);
	CHECK_FALSE(registered.Contains(0x12FE1000));
	CHECK(registered.Contains(0x7A81EC00));
	// The battle's known objects hold the page too.
	const AddressRanges arenas = sfve::mod::LowHeapArenas(kEmber147Committed, kEmber147GameHeap, projectiles, sfve::mod::kJoinGap);
	REQUIRE(arenas.Ranges().size() == 4);
	CHECK(arenas.Ranges()[0].begin == 0x12FE0000);
	CHECK(arenas.Ranges()[0].end == 0x12FF0000);
	for (const uint64_t p : projectiles) CHECK(arenas.Contains(p));
	CHECK_FALSE(arenas.Contains(0x12FF0000));
	CHECK_FALSE(arenas.Contains(0x3F800000)); // 1.0f in a low dword, between the arenas
	// No known object makes a stack, an image or memory above 1 TB an arena.
	const uint64_t mb = 1ull << 20;
	const std::vector<QueriedRegion> others = {
		{0x2C * mb, 0x2C * mb + 0x1000, 0x2B * mb, true, true}, // a stack's guard page
		{0x2C * mb + 0x1000, 0x2D * mb, 0x2B * mb, true, false},
		{0x40 * mb, 0x48 * mb, 0x40 * mb, false, false}, // an image
		{0x00000204F0000000ull, 0x00000204F1000000ull, 0x00000204F0000000ull, true, false},
	};
	CHECK(sfve::mod::LowHeapArenas(others, {}, {0x2C * mb + 0x2000, 0x41 * mb, 0x00000204F0000100ull}, sfve::mod::kJoinGap)
			  .Ranges()
			  .empty());
	CHECK(sfve::mod::LowHeapArenas(kEmber147Committed, kEmber147GameHeap, {}, sfve::mod::kJoinGap).Ranges() == registered.Ranges());
}

TEST_CASE("with the projectiles' page an arena, P1 and P2 zero every pointer of the group's run and decide equal values alike")
{
	sfve::mod::SetStartRanges({});
	// P1 at its bind (the low-heap rule's arenas), then the start agreement:
	// it shares them and takes them with P2's none.
	const std::vector<uint64_t> projectiles(std::begin(kEmber147P1Words), std::end(kEmber147P1Words));
	sfve::mod::PublishProcessRanges(
		sfve::mod::LowHeapArenas(kEmber147Committed, kEmber147GameHeap, projectiles, sfve::mod::kJoinGap));
	const std::vector<AddressRange> shared = sfve::mod::RangesToShare(16);
	REQUIRE(shared.size() == 4);
	CHECK(shared[0].begin == 0x12FE0000);
	sfve::mod::AdoptAgreedRanges(shared);
	for (const uint64_t word : kEmber147P1Words) CHECK(LooksLikeAddress(word));
	// Values both copies may hold as numbers: the group's count, nothing, a
	// 16.16 3.0, 1.0f, a number inside the projectiles' page, and the other
	// copy's pointers.
	std::vector<uint64_t> values = {0x18, 0, 0x30000, 0x3F800000, 0x12FE5000};
	values.insert(values.end(), std::begin(kEmber147P1Words), std::end(kEmber147P1Words));
	values.insert(values.end(), std::begin(kEmber147P2Words), std::end(kEmber147P2Words));
	std::vector<bool> p1;
	for (const uint64_t v : values) p1.push_back(LooksLikeAddress(v));
	CHECK_FALSE(LooksLikeAddress(0x18));
	CHECK(LooksLikeAddress(0x12FE5000));

	// P2 at its bind (heap above 1 TB), then the ranges the start agreement settled.
	CHECK(sfve::mod::ChooseAddressRule({0x1FD0B386CC0ull}, {0x1FD87345800ull}) == AddressRule::Value);
	sfve::mod::AdoptAgreedRanges(shared);
	for (const uint64_t word : kEmber147P2Words) CHECK(LooksLikeAddress(word));
	for (size_t i = 0; i < values.size(); i++) {
		INFO("value " << values[i]);
		const bool atP1 = p1[i];
		CHECK(LooksLikeAddress(values[i]) == atP1);
	}
	sfve::mod::ChooseAddressRule({0x00000204F5F98000ull});
}

TEST_CASE("the battle's other known objects widen a low heap's arenas but never choose the rule")
{
	sfve::mod::SetStartRanges({});
	void* block = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	REQUIRE(block);
	const uint64_t object = reinterpret_cast<uint64_t>(block) + 0x400;
	// A heap above 1 TB is the value rule, whatever else the battle holds.
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}, {0x02A41F10ull, object}) == AddressRule::Value);
	REQUIRE(sfve::mod::PublishedProcessRanges());
	CHECK(sfve::mod::PublishedProcessRanges()->Ranges().empty());
	// A heap below 1 TB: the private run a known object lies in is an arena
	// when it lies below 1 TB (above, the value rule has it already).
	CHECK(sfve::mod::ChooseAddressRule({0x02A41F10ull}, {object}) == AddressRule::LowHeap);
	CHECK(sfve::mod::PublishedProcessRanges()->Contains(object) == (object < sfve::mod::kHighAddresses));
	CHECK(LooksLikeAddress(object));
	VirtualFree(block, 0, MEM_RELEASE);
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
}

TEST_CASE("a copy whose heap lies low publishes its arenas and says so, and the next battle's normal heap drops them")
{
	const auto heap = std::make_unique<int[]>(16);
	const uint64_t own = reinterpret_cast<uint64_t>(heap.get());
	if (own >= sfve::mod::kHighAddresses) {
		// This process's heap is above 1 TB: a low heap is played with an
		// object below it, which no arena holds.
		CHECK(sfve::mod::ChooseAddressRule({0x02A41F10ull}) == AddressRule::LowHeap);
		CHECK(sfve::mod::AddressRuleText() == "low heap (words above 1 TB; no arena found)");
	} else {
		CHECK(sfve::mod::ChooseAddressRule({own}) == AddressRule::LowHeap);
		CHECK(LooksLikeAddress(own));
		CHECK(sfve::mod::AddressRuleText().rfind("low heap (words above 1 TB, and in ", 0) == 0);
	}
	CHECK(sfve::mod::CurrentAddressRule() == AddressRule::LowHeap);
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
	CHECK(sfve::mod::PublishedProcessRanges()->Ranges().empty());
	CHECK(sfve::mod::CurrentAddressRule() == AddressRule::Value);
}

TEST_CASE("the start ranges are the low heaps and images made before the game ran, nothing else")
{
	using sfve::mod::Reservation;
	const uint64_t mb = 1ull << 20;
	const std::vector<Reservation> reservations = {
		{0x20 * mb, 0x21 * mb, false},                          // a thread's stack, or a TEB: private, no heap in it
		{0xEE0000, 0xEE0000 + 0x190000 + 0x10000, false},       // the process heap (Wine puts it at 0xEE0000)
		{0x140000000ull, 0x140000000ull + 0x7000000ull, true},  // the game's exe at its preferred base
		{0x00007FFB12340000ull, 0x00007FFB12400000ull, true},    // an image above 1 TB: the value rule has it
		{0x00000204F0000000ull, 0x00000204F1000000ull, false},  // a heap above 1 TB, likewise
	};
	const AddressRanges start = sfve::mod::StartRanges(reservations, {0xEE0000, 0x00000204F0000000ull});
	REQUIRE(start.Ranges().size() == 2);
	CHECK(start.Contains(0xEE0000 + 0x190000)); // a block of the heap
	CHECK(start.Contains(0x140001000ull));      // a vtable in the exe
	CHECK_FALSE(start.Contains(0x20 * mb + 0x100));
	CHECK_FALSE(start.Contains(0x00007FFB12340000ull));
	CHECK(sfve::mod::StartRanges(reservations, {}).Ranges().size() == 1); // no heap: the exe alone
}

TEST_CASE("this process's reservations hold its heap and its code")
{
	const auto heap = std::make_unique<int[]>(16);
	const uint64_t own = reinterpret_cast<uint64_t>(heap.get());
	const uint64_t code = reinterpret_cast<uint64_t>(&sfve::mod::ReadReservations);
	bool heapSeen = false, codeImage = false;
	for (const sfve::mod::Reservation& r : sfve::mod::ReadReservations()) {
		CHECK(r.begin < r.end);
		heapSeen = heapSeen || (own >= r.begin && own < r.end);
		codeImage = codeImage || (code >= r.begin && code < r.end && r.image);
	}
	CHECK(heapSeen);
	CHECK(codeImage);
}

TEST_CASE("start ranges are addresses under either rule, and the arenas come on top of them")
{
	sfve::mod::SetStartRanges(AddressRanges::Joined({{0x140000000ull, 0x147000000ull}}, 0));
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
	CHECK(LooksLikeAddress(0x140001000ull));
	CHECK_FALSE(LooksLikeAddress(0x02A41F10ull));
	CHECK(sfve::mod::AddressRuleText() == "value (words above 1 TB, and in 1 start range(s) (heaps and images made before the game ran))");
	CHECK(sfve::mod::ChooseAddressRule({0x02A41F10ull}) == AddressRule::LowHeap);
	CHECK(LooksLikeAddress(0x140001000ull));
	sfve::mod::SetStartRanges({});
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
	CHECK_FALSE(LooksLikeAddress(0x140001000ull));
	CHECK(sfve::mod::AddressRuleText() == "value (words above 1 TB)");
}

TEST_CASE("fitted ranges join the closest neighbours first and never lose an address")
{
	const AddressRanges four = AddressRanges::Joined({{0x10000, 0x20000}, {0x30000, 0x40000}, {0x100000, 0x110000}, {0x140000000ull, 0x144000000ull}}, 0);
	CHECK(four.Fitted(4).Ranges().size() == 4);
	const AddressRanges three = four.Fitted(3);
	REQUIRE(three.Ranges().size() == 3);
	CHECK(three.Ranges()[0].begin == 0x10000); // the 64 KB gap went first
	CHECK(three.Ranges()[0].end == 0x40000);
	CHECK(three.Ranges()[1].begin == 0x100000);
	const AddressRanges one = four.Fitted(1);
	REQUIRE(one.Ranges().size() == 1);
	for (const AddressRange& r : four.Ranges()) CHECK((one.Contains(r.begin) && one.Contains(r.end - 4)));
	CHECK(four.Fitted(0).Ranges().empty());
}

TEST_CASE("a Proton copy shares its start ranges, and the agreed ranges make a number in them an address on every copy")
{
	// A Windows copy: nothing below 1 TB to share, and 0x30000 is a number.
	sfve::mod::SetStartRanges({});
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
	CHECK(sfve::mod::RangesToShare(16).empty());
	CHECK(sfve::mod::LowRangesText() == "none");
	CHECK_FALSE(LooksLikeAddress(0x30000));
	// The Proton fighter's ranges, as the start agreement hands them over.
	const std::vector<AddressRange> proton = {{0x30000, 0x130000}, {0x140000000ull, 0x14441F000ull}};
	sfve::mod::AdoptAgreedRanges(proton);
	CHECK(LooksLikeAddress(0x30000));
	CHECK(LooksLikeAddress(0x140001000ull));
	CHECK_FALSE(LooksLikeAddress(0x130000));
	CHECK(sfve::mod::AddressRuleText() == "value (words above 1 TB, and in the 2 range(s) the fighters agreed on before frame 0)");
	CHECK(sfve::mod::LowRangesText() == "0x30000-0x130000, 0x140000000-0x14441F000");
	// The next battle starts from this copy's own again.
	CHECK(sfve::mod::ChooseAddressRule({0x00000204F5F98000ull}) == AddressRule::Value);
	CHECK_FALSE(LooksLikeAddress(0x30000));
	CHECK(sfve::mod::AddressRuleText() == "value (words above 1 TB)");

	// The Proton copy itself shares its start ranges, at most as many as asked.
	sfve::mod::SetStartRanges(AddressRanges::Joined(proton, 0));
	sfve::mod::ChooseAddressRule({0x00007FFFE0000000ull});
	CHECK(sfve::mod::RangesToShare(16) == proton);
	REQUIRE(sfve::mod::RangesToShare(1).size() == 1);
	CHECK(sfve::mod::RangesToShare(1)[0].begin == 0x30000);
	sfve::mod::SetStartRanges({});
	sfve::mod::ChooseAddressRule({0x00000204F5F98000ull});
}

TEST_CASE("under Wine only an allocation for this process at no fixed address and no zero-bits limit goes top down")
{
	using sfve::mod::TopDownFits;
	CHECK(TopDownFits(true, false, 0, MEM_RESERVE));
	CHECK(TopDownFits(true, false, 0, MEM_RESERVE | MEM_COMMIT));
	CHECK(TopDownFits(true, false, 0, MEM_COMMIT));
	CHECK_FALSE(TopDownFits(true, true, 0, MEM_COMMIT));     // committing inside a reservation
	CHECK_FALSE(TopDownFits(false, false, 0, MEM_RESERVE));  // another process's memory
	CHECK_FALSE(TopDownFits(true, false, 31, MEM_RESERVE));  // asked to lie low
	CHECK_FALSE(TopDownFits(true, false, 0, MEM_RESET));
}

namespace {
// The highest address (inclusive) a zero-bits value allows for an allocation
// at no fixed address, as Wine reads it (get_zero_bits_limit); 0 is no limit.
// Values below 32 count leading zero bits above bit 32, larger ones are masks.
uint64_t ZeroBitsLimit(uint64_t zeroBits)
{
	if (zeroBits == 0) return 0;
	unsigned shift = 0;
	if (zeroBits < 32) {
		shift = 32 + static_cast<unsigned>(zeroBits);
	} else {
		shift = 63;
		if (zeroBits >> 32) { shift -= 32; zeroBits >>= 32; }
		if (zeroBits >> 16) { shift -= 16; zeroBits >>= 16; }
		if (zeroBits >> 8) { shift -= 8; zeroBits >>= 8; }
		if (zeroBits >> 4) { shift -= 4; zeroBits >>= 4; }
		if (zeroBits >> 2) { shift -= 2; zeroBits >>= 2; }
		if (zeroBits >> 1) shift -= 1;
	}
	return ~0ull >> shift;
}
} // namespace

TEST_CASE("a top down allocation is limited to below 8 TB, as 43-bit pointer code needs")
{
	using namespace sfve::mod;
	CHECK(kTopDownZeroBits == 0x000007FFFFFFFFFFull);
	// Wine reads a zero-bits value above 32 as a mask and limits the result to it.
	CHECK(ZeroBitsLimit(kTopDownZeroBits) == 0x000007FFFFFFFFFFull);
	CHECK(ZeroBitsLimit(kTopDownZeroBits) + 1 == kWineUserSpace);
	CHECK(ZeroBitsLimit(0) == 0);
	CHECK(ZeroBitsLimit(21) == (~0ull >> 53));
	CHECK(ZeroBitsLimit(0xFFFFFFFFull) == 0xFFFFFFFFull);
	// A caller that already asked for top down still gets the limit, and one with
	// its own zero bits keeps its own.
	CHECK(TopDownFits(true, false, 0, MEM_RESERVE | MEM_TOP_DOWN));
	CHECK_FALSE(TopDownFits(true, false, kTopDownZeroBits, MEM_RESERVE | MEM_TOP_DOWN));
	CHECK_FALSE(TopDownFits(true, false, 0xFFFFFFFFull, MEM_RESERVE));
	// Where a result may lie.
	CHECK(InTopDownRange(kHighAddresses));
	CHECK(InTopDownRange(0x000007FFFFFA0000ull));
	CHECK_FALSE(InTopDownRange(kHighAddresses - 1));
	CHECK_FALSE(InTopDownRange(kWineUserSpace));
	CHECK_FALSE(InTopDownRange(0x00007FFFFFBA0000ull)); // what 0.1.1 handed out
	CHECK_FALSE(InTopDownRange(0));
}
