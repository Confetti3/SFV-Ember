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

TEST_CASE("a hash of some regions follows those regions only, masks and all")
{
	std::vector<uint8_t> a(256, 1), b(128, 2), c(64, 3);
	StateLayout layout;
	layout.Add("a", a.data(), a.size());
	layout.Add("b", b.data(), b.size());
	layout.Add("c", c.data(), c.size(), false);
	layout.IgnoreWord(0, 16);
	const std::vector<size_t> ab = {0, 1};
	const uint64_t h = layout.HashLiveRegions(ab);
	CHECK(h == layout.HashLiveRegions({1, 0}));      // the order does not matter
	CHECK(h == layout.HashLiveRegions({0, 1, 2}));   // an unhashed region adds nothing
	CHECK(h == layout.HashLiveRegions({0, 1, 99}));  // nor an index past the layout
	CHECK(h == layout.HashLive());                   // all of the hashed regions: the whole hash
	c[5] = 9;
	CHECK(h == layout.HashLiveRegions(ab));
	a[16] = 9; // the masked word
	CHECK(h == layout.HashLiveRegions(ab));
	b[3] = 9;
	CHECK(h != layout.HashLiveRegions(ab));
	CHECK(layout.HashLiveRegions({0}) != layout.HashLiveRegions(ab));
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

TEST_CASE("a dword masked for peers leaves the peers' hash only, and not its neighbours")
{
	alignas(8) uint8_t memory[64] = {};
	StateLayout layout;
	const size_t r = layout.Add("r", memory, sizeof(memory));
	layout.IgnoreForPeers(r, 20); // bytes 20..23, the upper half of the word at 16
	const uint64_t local = layout.HashLive();
	const uint64_t peers = layout.HashLive(HashScope::Peers);

	memory[21] = 0x55; // a per-process value: the synctest still sees it change
	CHECK(layout.HashLive() != local);
	CHECK(layout.HashLive(HashScope::Peers) == peers);

	memory[16] = 1; // the live dword beside it still counts for peers
	CHECK(layout.HashLive(HashScope::Peers) != peers);

	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	CHECK(layout.HashSaved(saved.data(), HashScope::Peers) == layout.HashLive(HashScope::Peers));
}

TEST_CASE("a dword mask applies across the chunk edges of a large region")
{
	std::vector<uint8_t> bytes(9000, 0);
	StateLayout layout;
	const size_t r = layout.Add("r", bytes.data(), bytes.size());
	layout.IgnoreForPeers(r, 4096);
	layout.IgnoreForPeers(r, 8999); // the region's last dword
	const uint64_t peers = layout.HashLive(HashScope::Peers);
	bytes[4096] = bytes[4099] = bytes[8996] = bytes[8999] = 9;
	CHECK(layout.HashLive(HashScope::Peers) == peers);
	bytes[4100] = 9;
	CHECK(layout.HashLive(HashScope::Peers) != peers);
}

TEST_CASE("a region excluded from peers is still part of the local hash")
{
	std::vector<uint8_t> state(32, 1), presentation(16, 2);
	StateLayout layout;
	layout.Add("state", state.data(), state.size());
	const size_t p = layout.Add("presentation", presentation.data(), presentation.size());
	layout.ExcludeFromPeers(p);
	const uint64_t local = layout.HashLive();
	const uint64_t peers = layout.HashLive(HashScope::Peers);
	presentation[3] = 9;
	CHECK(layout.HashLive() != local);
	CHECK(layout.HashLive(HashScope::Peers) == peers);
	state[3] = 9;
	CHECK(layout.HashLive(HashScope::Peers) != peers);

	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	std::vector<RegionHash> regions;
	layout.PeerRegionHashesSaved(saved.data(), regions);
	REQUIRE(regions.size() == 1);
	CHECK(regions[0].size == state.size());
}

TEST_CASE("peers' region hashes come in the same order whatever order the regions are listed in")
{
	std::vector<uint8_t> a(40, 1), b(40, 2), c(8, 3);
	StateLayout one, two;
	one.Add("a", a.data(), a.size());
	one.Add("b", b.data(), b.size());
	one.Add("c", c.data(), c.size());
	two.Add("c", c.data(), c.size());
	two.Add("b", b.data(), b.size());
	two.Add("a", a.data(), a.size());
	std::vector<uint8_t> savedOne(one.TotalSize()), savedTwo(two.TotalSize());
	one.SaveTo(savedOne.data());
	two.SaveTo(savedTwo.data());
	std::vector<RegionHash> hashesOne, hashesTwo;
	one.PeerRegionHashesSaved(savedOne.data(), hashesOne);
	two.PeerRegionHashesSaved(savedTwo.data(), hashesTwo);
	CHECK(hashesOne == hashesTwo);
	CHECK(hashesOne.front().size == 8);

	b[0] = 7; // a real difference still shows in the list
	two.SaveTo(savedTwo.data());
	two.PeerRegionHashesSaved(savedTwo.data(), hashesTwo);
	CHECK(hashesOne != hashesTwo);
}

TEST_CASE("the layout id changes with what peers compare")
{
	uint8_t x[64] = {};
	StateLayout plain, masked, excluded;
	plain.Add("x", x, 64);
	masked.Add("x", x, 64);
	excluded.Add("x", x, 64);
	masked.IgnoreForPeers(0, 12);
	excluded.ExcludeFromPeers(0);
	CHECK(plain.LayoutId() != masked.LayoutId());
	CHECK(plain.LayoutId() != excluded.LayoutId());
	CHECK(masked.LayoutId() != excluded.LayoutId());
}

namespace {
// The mod's test (GameMemory.hxx LooksLikeAddress), for the overlap case.
bool HeapLike(uint64_t value) { return value >= (1ull << 40) && value <= 0x00007FFFFFFFFFFFull && (value & 3) == 0; }
} // namespace

TEST_CASE("an address's high half and the value after it are not taken for another address")
{
	// The same object in two processes: an address, then a small int. Read 4
	// bytes in, the pair looks like an address only where the address's high
	// half happens to be a multiple of 4.
	alignas(8) uint8_t one[16] = {}, two[16] = {};
	const uint64_t addressOne = 0x00000204F5F982A4ull, addressTwo = 0x00000246994182A4ull;
	const uint32_t value = 0x640;
	memcpy(one, &addressOne, 8);
	memcpy(one + 8, &value, 4);
	memcpy(two, &addressTwo, 8);
	memcpy(two + 8, &value, 4);
	StateLayout a, b;
	a.Add("r", one, sizeof(one));
	b.Add("r", two, sizeof(two));
	a.SetAddressTest(HeapLike);
	b.SetAddressTest(HeapLike);
	CHECK(a.HashLive() == b.HashLive());
	two[8] = 1; // the value itself still counts
	CHECK(a.HashLive() != b.HashLive());
}

namespace {
// Zeroed storage that really is 8-aligned, so a region's real alignment is
// the offset it is given into it.
std::vector<uint64_t> Words(size_t bytes) { return std::vector<uint64_t>((bytes + 7) / 8, 0); }
uint8_t* Bytes(std::vector<uint64_t>& words) { return reinterpret_cast<uint8_t*>(words.data()); }

uint64_t HashWithAddresses(uint8_t* region, size_t size)
{
	StateLayout layout;
	layout.Add("r", region, size);
	layout.SetAddressTest(HeapLike);
	return layout.HashLive();
}
} // namespace

TEST_CASE("an address across the edge of two hashed chunks hashes as zero, and what follows it still counts")
{
	// At offset 4092 of an aligned region, an address's low half ends the
	// first 4 KB chunk and its high half starts the next. The two processes'
	// high halves differ as in the test above, and with the value after them
	// one would pass for an address if read 4 bytes in.
	auto one = Words(8192), two = Words(8192);
	const uint64_t addressOne = 0x00000204F5F982A4ull, addressTwo = 0x00000246994182A4ull;
	const uint32_t value = 0x640;
	memcpy(Bytes(one) + 4092, &addressOne, 8);
	memcpy(Bytes(two) + 4092, &addressTwo, 8);
	memcpy(Bytes(one) + 4100, &value, 4);
	memcpy(Bytes(two) + 4100, &value, 4);
	const uint64_t h = HashWithAddresses(Bytes(one), 8192);
	CHECK(HashWithAddresses(Bytes(two), 8192) == h);

	StateLayout layout;
	layout.Add("r", Bytes(two), 8192);
	layout.SetAddressTest(HeapLike);
	std::vector<uint8_t> saved(layout.TotalSize());
	layout.SaveTo(saved.data());
	CHECK(layout.HashSaved(saved.data()) == h);

	Bytes(two)[4100] = 1; // the value right after the address
	CHECK(HashWithAddresses(Bytes(two), 8192) != h);
}

TEST_CASE("an address across a chunk edge hashes as zero in an unaligned region too")
{
	// A region 1 byte past an 8-byte boundary: its 4-byte boundaries are at
	// offsets 3, 7, ..., and the address at 4095 has 7 of its bytes in the
	// second chunk.
	auto one = Words(8192), two = Words(8192);
	uint8_t* regionOne = Bytes(one) + 1;
	uint8_t* regionTwo = Bytes(two) + 1;
	const uint64_t addressOne = 0x00000204F5F982A4ull, addressTwo = 0x00000246994182A4ull;
	memcpy(regionOne + 4095, &addressOne, 8);
	memcpy(regionTwo + 4095, &addressTwo, 8);
	CHECK(HashWithAddresses(regionOne, 8000) == HashWithAddresses(regionTwo, 8000));
	regionTwo[4103] = 1; // the byte right after the address
	CHECK(HashWithAddresses(regionOne, 8000) != HashWithAddresses(regionTwo, 8000));
}

TEST_CASE("the dword after an address that ends a chunk is tested on its own")
{
	// An address ends the first chunk and a small value starts the next. Read
	// 4 bytes into the address, across the edge, the pair would pass for an
	// address (high half 0x204, then 0x204 or 0x208) and hide the value.
	auto one = Words(8192), two = Words(8192);
	const uint64_t address = 0x00000204F5F982A4ull;
	const uint32_t valueOne = 0x204, valueTwo = 0x208;
	memcpy(Bytes(one) + 4088, &address, 8);
	memcpy(Bytes(two) + 4088, &address, 8);
	memcpy(Bytes(one) + 4096, &valueOne, 4);
	memcpy(Bytes(two) + 4096, &valueTwo, 4);
	CHECK(HashWithAddresses(Bytes(one), 8192) != HashWithAddresses(Bytes(two), 8192));
}

namespace {
// Fills the region with 0x11 bytes, none of whose 8-byte words is HeapLike
// (each is above 0x00007FFFFFFFFFFF, and stays so with one byte 0x12), so the
// address test has nothing to zero. Checks that the region then hashes as it
// does without an address test, and that a change to each byte of [from, to)
// changes its hash and still matches the hash without the test.
void CheckEveryByteCounts(uint8_t* region, size_t size, size_t from, size_t to)
{
	memset(region, 0x11, size);
	StateLayout plain;
	plain.Add("r", region, size);
	const uint64_t h = HashWithAddresses(region, size);
	CHECK(h == plain.HashLive());
	for (size_t i = from; i < to; i++) {
		CAPTURE(i);
		region[i] = 0x12;
		CHECK(HashWithAddresses(region, size) != h);
		CHECK(HashWithAddresses(region, size) == plain.HashLive());
		region[i] = 0x11;
	}
}
} // namespace

TEST_CASE("the bytes before an unaligned region's first 4-byte boundary count")
{
	// A region 1 byte past an 8-byte boundary has its first 4-byte boundary
	// at offset 3: its first three bytes are never tested as an address and
	// are hashed as they are. Likewise for every other start.
	for (size_t start = 0; start < 8; start++) {
		CAPTURE(start);
		auto words = Words(64 + 8);
		CheckEveryByteCounts(Bytes(words) + start, 64, 0, 8);
	}
}

TEST_CASE("a word that is no address across a chunk edge leaves the bytes after the edge counting")
{
	// In a region 1 byte past an 8-byte boundary, the word tested at 4095 is
	// the first chunk's last and runs 7 bytes into the second. It is no
	// address, so the next test is at 4099, and bytes 4096..4098 are plain
	// state of the second chunk. 3 bytes past, the word at 4093 is followed by
	// 4097. Every start, both chunk edges, and the region's last bytes.
	for (size_t start = 0; start < 8; start++) {
		CAPTURE(start);
		auto words = Words(8300 + 8);
		uint8_t* region = Bytes(words) + start;
		CheckEveryByteCounts(region, 8300, 4088, 4104);
		CheckEveryByteCounts(region, 8300, 8184, 8200);
		CheckEveryByteCounts(region, 8300, 8292, 8300);
	}
}
