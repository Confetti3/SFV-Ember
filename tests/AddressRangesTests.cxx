#include "mod/AddressRanges.hxx"
#include "mod/GameMemory.hxx"
#include "mod/WineMemory.hxx"

#include <windows.h>

#include <doctest/doctest.h>

#include <memory>

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
