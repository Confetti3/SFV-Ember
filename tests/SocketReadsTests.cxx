// The fight's reads of a fighter's model (mod/SocketReads.hxx): which call
// sites are the fight's, the socket's key, and the record of straight reads
// that a read on a stale pose takes.

#include <doctest/doctest.h>

#include "mod/SfvOffsets.hxx"
#include "mod/SocketReads.hxx"

#include <stdint.h>

#include <algorithm>

using namespace sfve::mod;
using socket_reads::Cache;
using socket_reads::Key;
using socket_reads::Position;

namespace {

Position At(float x, float y, float z)
{
	Position p;
	p.xyzw[0] = x;
	p.xyzw[1] = y;
	p.xyzw[2] = z;
	p.xyzw[3] = 1.0f;
	return p;
}

} // namespace

TEST_CASE("the socket read's site is in the build table")
{
	CHECK(sfv::kSocketWorldPosition.rva == 0x3919D0);
	CHECK(sfv::kSocketWorldPosition.bytes[0] == 0x48);
	CHECK(sfv::kSocketWorldPosition.bytes[15] == 0x63);
	const GameBuild* build = sfv::VerifiedBuild();
	CHECK(std::any_of(build->sites, build->sites + build->siteCount,
		[](const CodeSite& listed) { return listed.rva == sfv::kSocketWorldPosition.rva; }));
}

TEST_CASE("the reads that are not the fight's are left as they are")
{
	// Bone 28 in Chara PreInput, the camera's handlers, CharaManager's, and the
	// head for Chara PostVisual (after the tick).
	for (const uint32_t other : {0x37D48Eu, 0x31B10Eu, 0x31B1DEu, 0x32FA5Fu, 0x33C41Fu, 0x3E3EF0u, 0x3E3F57u, 0x38F401u, 0x38F69Bu,
			 0x38F6B7u}) {
		CHECK(socket_reads::EveryTick(other));
	}
	// Spawns at a bone: any script's spawn with a bone (through rva 0x32BAA0's
	// jump, so from rva 0x4CFB70), Z33's part as its V-Trigger ends, and the
	// other two parts that read a socket.
	for (const uint32_t spawn : {0x4CFC99u, 0x3BDB02u, 0x3BC294u, 0x3BDF3Bu}) CHECK_FALSE(socket_reads::EveryTick(spawn));
}

TEST_CASE("a socket's key is its name")
{
	CHECK(socket_reads::NameKey(L"b_R_Foot") == socket_reads::NameKey(L"b_R_Foot"));
	CHECK(socket_reads::NameKey(L"b_R_Foot") != socket_reads::NameKey(L"b_L_Foot"));
	// Two copies of the name are the same socket.
	const wchar_t copy[] = L"g_X_Trajectory";
	CHECK(socket_reads::NameKey(copy) == socket_reads::NameKey(L"g_X_Trajectory"));
	CHECK(socket_reads::NameKey(nullptr) == socket_reads::NameKey(L""));
}

TEST_CASE("a stale read takes the straight read of the same state, socket and side")
{
	Cache cache;
	const Key key{0x1234, socket_reads::NameKey(L"b_R_Foot"), 0};
	Position found;
	CHECK_FALSE(cache.Find(key, &found)); // never read fresh: the live value stays
	CHECK_FALSE(cache.Record(key, At(2.772f, 0.237f, -0.057f)));
	REQUIRE(cache.Find(key, &found));
	CHECK(found == At(2.772f, 0.237f, -0.057f));
	// Another state, socket or side is another read.
	CHECK_FALSE(cache.Find(Key{0x1235, key.socket, 0}, &found));
	CHECK_FALSE(cache.Find(Key{0x1234, socket_reads::NameKey(L"b_L_Foot"), 0}, &found));
	CHECK_FALSE(cache.Find(Key{0x1234, key.socket, 1}, &found));
	// The same state read fresh again with the same value is no conflict; with
	// another value it is, and the newer is kept.
	CHECK_FALSE(cache.Record(key, At(2.772f, 0.237f, -0.057f)));
	CHECK(cache.Record(key, At(3.271f, 0.211f, -0.045f)));
	REQUIRE(cache.Find(key, &found));
	CHECK(found == At(3.271f, 0.211f, -0.045f));
	CHECK(cache.Size() == 1);
	cache.Clear();
	CHECK(cache.Size() == 0);
	CHECK_FALSE(cache.Find(key, &found));
}

TEST_CASE("past its capacity the record drops its oldest reads")
{
	Cache cache(3);
	for (uint64_t state = 1; state <= 5; state++) cache.Record(Key{state, 7, 0}, At((float)state, 0, 0));
	CHECK(cache.Size() == 3);
	Position found;
	CHECK_FALSE(cache.Find(Key{1, 7, 0}, &found));
	CHECK_FALSE(cache.Find(Key{2, 7, 0}, &found));
	for (uint64_t state = 3; state <= 5; state++) {
		REQUIRE(cache.Find(Key{state, 7, 0}, &found));
		CHECK(found.xyzw[0] == (float)state);
	}
	// A key recorded again keeps its place and its new value.
	cache.Record(Key{3, 7, 0}, At(30, 0, 0));
	CHECK(cache.Size() == 3);
	REQUIRE(cache.Find(Key{3, 7, 0}, &found));
	CHECK(found.xyzw[0] == 30.0f);
	Cache none(0);
	none.Record(Key{1, 1, 0}, At(1, 1, 1));
	CHECK(none.Size() == 0);
}
