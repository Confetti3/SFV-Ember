#include "mod/AddressRanges.hxx"
#include "mod/GameMemory.hxx"

#include <doctest/doctest.h>

#include <memory>

using sfve::mod::AddressRange;
using sfve::mod::AddressRanges;
using sfve::mod::LooksLikeAddress;

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

TEST_CASE("a heap far below 1 TB reads as addresses once its ranges are published, and as numbers before")
{
	// A PC with bottom-up ASLR off, or Wine: the heap at 32 MB, the exe at 4 GB.
	const uint64_t heap = 0x02A41F10ull;
	CHECK_FALSE(LooksLikeAddress(heap));
	sfve::mod::PublishProcessRanges(AddressRanges::Joined({{0x2000000, 0x4000000}, {0x140000000ull, 0x145000000ull}}, 0));
	CHECK(LooksLikeAddress(heap));
	CHECK(LooksLikeAddress(0x140ABCDE0ull));
	CHECK_FALSE(LooksLikeAddress(heap + 1)); // not 4-byte aligned
	CHECK_FALSE(LooksLikeAddress(0x1F4));    // a small number
	CHECK_FALSE(LooksLikeAddress(0x4000000)); // past the range's end
	CHECK(LooksLikeAddress(0x00000204F5F982A0ull)); // above 1 TB, as before
	CHECK_FALSE(LooksLikeAddress(0xFFFFFFFFFFFFFFF0ull)); // kernel space
	sfve::mod::PublishProcessRanges({});
	CHECK_FALSE(LooksLikeAddress(heap));
}

TEST_CASE("the process's own ranges hold its heap, its stack and its code")
{
	const AddressRanges ranges = sfve::mod::ReadProcessRanges();
	const auto heap = std::make_unique<int[]>(16);
	int local = 0;
	CHECK(ranges.Contains(reinterpret_cast<uint64_t>(heap.get())));
	CHECK(ranges.Contains(reinterpret_cast<uint64_t>(&local)));
	CHECK(ranges.Contains(reinterpret_cast<uint64_t>(&sfve::mod::ReadProcessRanges)));
	CHECK_FALSE(ranges.Contains(0x1000)); // the null page's neighbourhood
}
