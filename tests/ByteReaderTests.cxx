// The core's bounded reader (core/ByteReader.hxx), which the start state's
// datagrams and the replay file are read with.

#include <doctest/doctest.h>

#include "core/ByteReader.hxx"

#include <stdint.h>

#include <vector>

using sfve::ByteReader;

TEST_CASE("values are read in order, little-endian, and a read past the end fails and moves nothing")
{
	const uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x7F};
	ByteReader r(bytes, sizeof(bytes));
	uint32_t u32 = 0;
	uint8_t u8 = 0;
	uint64_t u64 = 0;
	REQUIRE(r.Get(&u32));
	CHECK(u32 == 0x04030201u);
	REQUIRE(r.Get(&u8));
	CHECK(u8 == 0xAA);
	REQUIRE(r.Get(&u64));
	CHECK(u64 == 0x8070605040302010ull);
	CHECK(r.Left() == 1);
	CHECK_FALSE(r.Get(&u32)); // 4 bytes asked, 1 left
	CHECK(r.Left() == 1);
	const uint8_t* rest = nullptr;
	CHECK_FALSE(r.Bytes(2, &rest));
	REQUIRE(r.Bytes(1, &rest));
	CHECK(*rest == 0x7F);
	CHECK(r.Left() == 0);
	CHECK_FALSE(r.Get(&u8));
}

TEST_CASE("bytes are copied out only when all of them are there")
{
	const uint8_t bytes[] = {1, 2, 3};
	ByteReader r(bytes, sizeof(bytes));
	std::vector<uint8_t> out{9};
	CHECK_FALSE(r.Get(&out, 4));
	CHECK(out == std::vector<uint8_t>{9});
	REQUIRE(r.Get(&out, 3));
	CHECK(out == std::vector<uint8_t>{1, 2, 3});
	ByteReader empty(nullptr, 0);
	CHECK(empty.Left() == 0);
	CHECK(empty.Get(&out, 0));
	CHECK(out.empty());
}
