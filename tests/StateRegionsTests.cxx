#include <doctest/doctest.h>

#include "core/StateRegions.hxx"

#include <string.h>

#include <vector>

using namespace sfve;

TEST_CASE("save then load restores every region byte for byte")
{
	std::vector<uint8_t> a(1000), b(37);
	for (size_t i = 0; i < a.size(); i++) a[i] = (uint8_t)i;
	for (size_t i = 0; i < b.size(); i++) b[i] = (uint8_t)(255 - i);
	StateLayout layout;
	layout.Add("a", a.data(), a.size());
	layout.Add("b", b.data(), b.size(), false);
	CHECK(layout.TotalSize() == 1037);

	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	const auto a0 = a, b0 = b;
	memset(a.data(), 0xAA, a.size());
	memset(b.data(), 0xBB, b.size());
	layout.LoadFrom(saved.data());
	CHECK(a == a0);
	CHECK(b == b0);
}

TEST_CASE("live and saved hashes agree and track checked content only")
{
	std::vector<uint8_t> checked(5000, 1), unchecked(64, 2);
	StateLayout layout;
	layout.Add("checked", checked.data(), checked.size());
	layout.Add("unchecked", unchecked.data(), unchecked.size(), false);

	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	const uint64_t h = layout.HashLive();
	CHECK(h == layout.HashSaved(saved.data()));

	unchecked[5] = 99; // restored on rollback, never compared
	CHECK(layout.HashLive() == h);

	checked[4097] = 7; // past the first 4 KB chunk
	CHECK(layout.HashLive() != h);
}

TEST_CASE("masked words are left out of the hash, including a word cut by the region end")
{
	std::vector<uint8_t> bytes(4100, 0);
	StateLayout layout;
	const size_t r = layout.Add("r", bytes.data(), bytes.size());
	layout.IgnoreWord(r, 16);    // bytes 16..23
	layout.IgnoreWord(r, 4099);  // word 512 = bytes 4096..4103, only 4096..4099 exist
	CHECK(layout.IsIgnored(r, 23));
	CHECK_FALSE(layout.IsIgnored(r, 24));
	const uint64_t h = layout.HashLive();

	bytes[16] = 1;
	bytes[23] = 1;
	bytes[4096] = 1;
	bytes[4099] = 1;
	CHECK(layout.HashLive() == h);

	bytes[24] = 1;
	CHECK(layout.HashLive() != h);
}

TEST_CASE("the layout id changes with sizes, flags and masks, not names")
{
	uint8_t x[64] = {};
	StateLayout a, b;
	a.Add("x", x, 64);
	b.Add("x", x, 64);
	CHECK(a.LayoutId() == b.LayoutId());
	b.IgnoreWord(0, 8);
	CHECK(a.LayoutId() != b.LayoutId());
	StateLayout c;
	c.Add("x", x, 64, false);
	CHECK(a.LayoutId() != c.LayoutId());
	// Names are left out: they follow each process's table order.
	StateLayout d;
	d.Add("y", x, 64);
	CHECK(a.LayoutId() == d.LayoutId());
}

TEST_CASE("hashes and layout ids do not depend on region order")
{
	std::vector<uint8_t> a(100, 1), b(37, 2);
	StateLayout ab, ba;
	ab.Add("a", a.data(), a.size());
	ab.Add("b", b.data(), b.size());
	ba.Add("b", b.data(), b.size());
	ba.Add("a", a.data(), a.size());
	CHECK(ab.HashLive() == ba.HashLive());
	CHECK(ab.LayoutId() == ba.LayoutId());
	b[3] = 9;
	CHECK(ab.HashLive() == ba.HashLive());
}

namespace {
bool IsMarker(uint64_t value) { return value == 0x00000123456789A0ull; }
} // namespace

TEST_CASE("address words hash as zero at their real alignment, even in an unaligned region")
{
	alignas(8) uint8_t memory[64] = {};
	uint8_t* region = memory + 4; // starts 4 bytes into an 8-byte word
	StateLayout layout;
	layout.Add("r", region, 40);
	layout.SetAddressTest(IsMarker);
	const uint64_t h = layout.HashLive();

	const uint64_t marker = 0x00000123456789A0ull;
	memcpy(memory + 16, &marker, 8); // an aligned word inside the region
	CHECK(layout.HashLive() == h);

	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	CHECK(layout.HashSaved(saved.data()) == h);

	memcpy(memory + 24, &marker, 8);
	memory[25] ^= 1; // not an address any more: it counts
	CHECK(layout.HashLive() != h);
}

TEST_CASE("an address on a 4-byte boundary hashes as zero too")
{
	alignas(8) uint8_t memory[64] = {};
	StateLayout layout;
	layout.Add("r", memory, 64);
	layout.SetAddressTest(IsMarker);
	const uint64_t h = layout.HashLive();
	const uint64_t marker = 0x00000123456789A0ull;
	memcpy(memory + 20, &marker, 8); // 4 bytes past an 8-byte boundary
	CHECK(layout.HashLive() == h);
}
