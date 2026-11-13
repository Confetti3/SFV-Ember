// The fight's reads of a fighter's model (mod/SocketReads.hxx): which call
// sites are the fight's, what keys a read, the record of straight reads that
// a read on a stale pose takes, and the rebuild at another root.

#include <doctest/doctest.h>

#include "mod/SfvOffsets.hxx"
#include "mod/SocketReads.hxx"

#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace sfve::mod;
using socket_reads::Cache;
using socket_reads::Key;
using socket_reads::ModelTransform;
using socket_reads::Position;
using socket_reads::Reading;

namespace {

template <class T>
void Put(unsigned char* base, uint32_t offset, T value)
{
	memcpy(base + offset, &value, sizeof(T));
}

template <class T>
T Get(const unsigned char* base, uint32_t offset)
{
	T value;
	memcpy(&value, base + offset, sizeof(T));
	return value;
}

// A UClass with its cast flags, and a skinned mesh component of it, laid out
// as the engine's (sfv::mesh).
struct FakeClass {
	alignas(16) unsigned char bytes[0x100] = {};
	explicit FakeClass(uint64_t cast) { Put(bytes, sfv::mesh::kClassCastFlags, cast); }
};

struct FakeComponent {
	alignas(16) unsigned char bytes[0x600] = {};
	FakeComponent(FakeClass& cls, int32_t predicted, int32_t forced = 0)
	{
		Put(bytes, sfv::mesh::kObjectClass, static_cast<unsigned char*>(cls.bytes));
		Put(bytes, sfv::mesh::kPredictedLodLevel, predicted);
		Put(bytes, sfv::mesh::kForcedLodModel, forced);
		Put(bytes, sfv::mesh::kUpdateFlag, uint8_t(2));
	}
	int32_t Forced() const { return Get<int32_t>(bytes, sfv::mesh::kForcedLodModel); }
	uint8_t Flag() const { return Get<uint8_t>(bytes, sfv::mesh::kUpdateFlag); }
};

// A fighter's actor: its mesh, second mesh and layers (sfv::visual).
struct FakeActor {
	alignas(16) unsigned char bytes[0x440] = {};
	unsigned char* layers[4] = {};
	void Set(FakeComponent* mesh, FakeComponent* second, std::vector<FakeComponent*> layerList, std::optional<int32_t> count = std::nullopt)
	{
		Put(bytes, sfv::visual::kActorMesh, mesh ? static_cast<unsigned char*>(mesh->bytes) : nullptr);
		Put(bytes, sfv::visual::kActorSecondMesh, second ? static_cast<unsigned char*>(second->bytes) : nullptr);
		for (size_t i = 0; i < layerList.size() && i < 4; i++) layers[i] = layerList[i]->bytes;
		Put(bytes, sfv::visual::kActorLayers, static_cast<unsigned char**>(layers));
		Put(bytes, sfv::visual::kActorLayerCount, count ? *count : (int32_t)layerList.size());
	}
};

Position At(float x, float y, float z)
{
	Position p;
	p.xyzw[0] = x;
	p.xyzw[1] = y;
	p.xyzw[2] = z;
	p.xyzw[3] = 1.0f;
	return p;
}

// A fresh read recorded without its transforms: only ever given back exactly.
Reading Raw(uint64_t root, float x, float y, float z)
{
	Reading r;
	r.root = root;
	r.out = At(x, y, z);
	return r;
}

// A mesh facing the fight's way at `x, y, z` (cm): no rotation, scale 1.
ModelTransform Mesh(float x, float y, float z)
{
	ModelTransform t;
	t.rotation[3] = 1.0f;
	t.translation[0] = x;
	t.translation[1] = y;
	t.translation[2] = z;
	t.scale[0] = t.scale[1] = t.scale[2] = 1.0f;
	return t;
}

// A fresh read with the transforms it came from: the socket's world
// translation (cm), which the read wrote as FightPosition.
Reading Modelled(uint64_t root, const ModelTransform& mesh, const float socket[3])
{
	Reading r;
	r.root = root;
	r.model = true;
	r.at.mesh = mesh;
	memcpy(r.at.socket, socket, sizeof(r.at.socket));
	r.out = socket_reads::FightPosition(socket);
	return r;
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

TEST_CASE("the transforms the read came from are the build's own calls")
{
	const GameBuild* build = sfv::VerifiedBuild();
	for (const CodeSite* site : {&sfv::kMakeName, &sfv::kSocketTransform}) {
		CHECK(std::any_of(build->sites, build->sites + build->siteCount, [&](const CodeSite& listed) { return listed.rva == site->rva; }));
	}
	CHECK(sfv::kMakeName.rva == 0x11AC010);
	CHECK(sfv::kSocketTransform.rva == 0xE58F10);
}

TEST_CASE("a world translation becomes the fight's position as the game's read makes it")
{
	// The game's constants (rva 0x2C4ED38 and 0x2C4F9AC) are 0.01f and -0.01f.
	CHECK(std::bit_cast<uint32_t>(0.01f) == sfv::kCentimetresToFightBits);
	CHECK(std::bit_cast<uint32_t>(-0.01f) == sfv::kCentimetresToFightNegatedBits);
	const float socket[3] = {277.2f, -5.7f, 23.7f};
	const Position p = socket_reads::FightPosition(socket);
	// x, then z, then y negated: one multiply each.
	CHECK(std::bit_cast<uint32_t>(p.xyzw[0]) == std::bit_cast<uint32_t>(277.2f * 0.01f));
	CHECK(std::bit_cast<uint32_t>(p.xyzw[1]) == std::bit_cast<uint32_t>(23.7f * 0.01f));
	CHECK(std::bit_cast<uint32_t>(p.xyzw[2]) == std::bit_cast<uint32_t>(-5.7f * -0.01f));
	CHECK(p.xyzw[3] == 1.0f);
}

TEST_CASE("what decides the pose key, and what does not")
{
	using socket_reads::Run;
	std::vector<uint8_t> motion(sfv::fighter::kMotionRunSize, 0), motion2(sfv::fighter::kMotionRun2Size, 0);
	motion[8] = 26; // something in it, as in a real part
	const Run runs[2] = {{motion.data(), motion.size()}, {motion2.data(), motion2.size()}};
	const uint64_t key = socket_reads::MakePoseKey(runs, 0, false);
	CHECK(key != 0);
	CHECK(socket_reads::MakePoseKey(runs, 0, false) == key);
	// The bytes decide it, not where they are: a copy keys alike.
	std::vector<uint8_t> copy = motion;
	const Run copied[2] = {{copy.data(), copy.size()}, runs[1]};
	CHECK(socket_reads::MakePoseKey(copied, 0, false) == key);
	// The animation state, the facing and a Tiny pick each change it.
	copy[100] = 1;
	CHECK(socket_reads::MakePoseKey(copied, 0, false) != key);
	motion2[191] = 5;
	CHECK(socket_reads::MakePoseKey(runs, 0, false) != key);
	motion2[191] = 0;
	CHECK(socket_reads::MakePoseKey(runs, 1, false) != key);
	CHECK(socket_reads::MakePoseKey(runs, 0, true) != key);
	// A unit without the part keys the same each time, and unlike one with it.
	const Run none[2] = {};
	CHECK(socket_reads::MakePoseKey(none, 0, false) == socket_reads::MakePoseKey(none, 0, false));
	CHECK(socket_reads::MakePoseKey(none, 0, false) != key);
	// A byte cannot move from one run to the other unseen.
	std::vector<uint8_t> a = {1, 2, 3}, b = {4}, c = {1, 2}, d = {3, 4};
	const Run ab[2] = {{a.data(), a.size()}, {b.data(), b.size()}};
	const Run cd[2] = {{c.data(), c.size()}, {d.data(), d.size()}};
	CHECK(socket_reads::MakePoseKey(ab, 0, false) != socket_reads::MakePoseKey(cd, 0, false));
}

TEST_CASE("the pose key does not see a per-instance address in the motion part")
{
	using socket_reads::Run;
	// 8-byte aligned images (the rule aligns by the real address).
	std::vector<uint64_t> a(sfv::fighter::kMotionRunSize / 8 + 1, 0), b = a;
	std::vector<uint64_t> second(sfv::fighter::kMotionRun2Size / 8, 0);
	a[3] = 0x00007FF612345670ull; // a heap or code address of one process
	b[3] = 0x00007FF698765430ull; // the same state in another one
	const auto key = [&](const std::vector<uint64_t>& image) {
		const Run runs[2] = {{reinterpret_cast<const uint8_t*>(image.data()), sfv::fighter::kMotionRunSize},
			{reinterpret_cast<const uint8_t*>(second.data()), sfv::fighter::kMotionRun2Size}};
		return socket_reads::MakePoseKey(runs, 0, false);
	};
	CHECK(key(a) == key(b));
	// A value word that is not an address still decides it.
	std::vector<uint64_t> c = a, d = a;
	c[5] = 0x1234;
	d[5] = 0x1235;
	CHECK(key(c) != key(d));
	CHECK(key(c) != key(a));
	// An address and a plain value in the same word are not alike.
	b[3] = 0x1234;
	CHECK(key(a) != key(b));
}

TEST_CASE("what decides the root key: where the unit stands and its hitstop, not its bone 28")
{
	using socket_reads::Run;
	namespace f = sfv::fighter;
	std::vector<uint8_t> unit(f::kUnitRunSize, 0), place(f::kPlaceRunSize, 0);
	const auto key = [&] { return socket_reads::MakeRootKey({unit.data(), unit.size()}, {place.data(), place.size()}); };
	const uint64_t root = key();
	CHECK(root != 0);
	// Bone 28 in world space (CharaUnit+1984 = +0x764 run +92): the read's own
	// result, which the camera keeps.
	for (uint32_t at = f::kPlaceBoneFrom; at < f::kPlaceBoneTo; at++) {
		place[at] = 0x5A;
		CHECK(key() == root);
	}
	// Right before and right after it, and the position, count.
	place[f::kPlaceBoneFrom - 1] = 1;
	CHECK(key() != root);
	place[f::kPlaceBoneFrom - 1] = 0;
	place[f::kPlaceBoneTo] = 1;
	CHECK(key() != root);
	place[f::kPlaceBoneTo] = 0;
	// The hitstop elapsed, CharaUnit +0x104 (the unit run's +12).
	unit[sfv::fighter::kHitStopElapsed - f::kUnitRun] = 1;
	CHECK(key() != root);
}

TEST_CASE("a stale read takes the straight read of the same pose and root")
{
	using socket_reads::Found;
	using socket_reads::Recorded;
	Cache cache;
	const Key key{0x1234, socket_reads::NameKey(L"b_R_Foot"), 0};
	Position found;
	CHECK(cache.Find(key, 7, nullptr, &found) == Found::NoPose); // never read fresh: the live value stays
	CHECK(cache.Record(key, Raw(7, 2.772f, 0.237f, -0.057f)) == Recorded::New);
	REQUIRE(cache.Find(key, 7, nullptr, &found) == Found::Exact);
	CHECK(found == At(2.772f, 0.237f, -0.057f));
	// Another pose, socket or unit (a partner's model) is another read.
	CHECK(cache.Find(Key{0x1235, key.socket, 0}, 7, nullptr, &found) == Found::NoPose);
	CHECK(cache.Find(Key{0x1234, socket_reads::NameKey(L"b_L_Foot"), 0}, 7, nullptr, &found) == Found::NoPose);
	CHECK(cache.Find(Key{0x1234, key.socket, 2}, 7, nullptr, &found) == Found::NoPose);
	// The same pose at another root, with nothing to rebuild from: missed.
	CHECK(cache.Find(key, 8, nullptr, &found) == Found::OtherRoot);
	// The same pose and root again: no conflict with the same value; with
	// another one a conflict, and the newer is kept.
	CHECK(cache.Record(key, Raw(7, 2.772f, 0.237f, -0.057f)) == Recorded::Again);
	CHECK(cache.Record(key, Raw(7, 3.271f, 0.211f, -0.045f)) == Recorded::Conflicting);
	REQUIRE(cache.Find(key, 7, nullptr, &found) == Found::Exact);
	CHECK(found == At(3.271f, 0.211f, -0.045f));
	// A second root of the same pose is kept beside the first.
	CHECK(cache.Record(key, Raw(9, 1.0f, 2.0f, 3.0f)) == Recorded::New);
	REQUIRE(cache.Find(key, 9, nullptr, &found) == Found::Exact);
	CHECK(found == At(1.0f, 2.0f, 3.0f));
	REQUIRE(cache.Find(key, 7, nullptr, &found) == Found::Exact);
	CHECK(found == At(3.271f, 0.211f, -0.045f));
	CHECK(cache.Size() == 1);
	cache.Clear();
	CHECK(cache.Size() == 0);
	CHECK(cache.Find(key, 7, nullptr, &found) == Found::NoPose);
}

TEST_CASE("one axis of the engine's add, certain or not")
{
	float out = 0;
	// The same root: the recorded sum itself.
	REQUIRE(socket_reads::CertifiedSum(337.25f, 300.0f, 300.0f, &out));
	CHECK(out == 337.25f);
	// Another root in the same binade, an even number of its ulps away: every r
	// with fl(r + 300) == 337.25 gives fl(r + 302) == 339.25 (both ends of
	// that r are ties, and 337.25 and 339.25 have even significands).
	REQUIRE(socket_reads::CertifiedSum(337.25f, 300.0f, 302.0f, &out));
	CHECK(out == 339.25f);
	// A root in a coarser binade: r = 37.25 is the only float with
	// fl(r + 1) == 38.25 (r's ulp is the sum's), so any root is certain.
	REQUIRE(socket_reads::CertifiedSum(38.25f, 1.0f, 300.0f, &out));
	CHECK(out == 337.25f);
	// A root at 0 (y on the fight's plane): the sum is r itself.
	REQUIRE(socket_reads::CertifiedSum(12.5f, 0.0f, 0.0f, &out));
	CHECK(out == 12.5f);
	// Into a finer binade: r is known to a half ulp of 337.25, which spans
	// several floats near 37.25, so the new sum is not certain.
	out = -1.0f;
	CHECK_FALSE(socket_reads::CertifiedSum(337.25f, 300.0f, 0.0f, &out));
	CHECK(out == -1.0f);
	// An odd number of ulps away from an even recorded sum: the tie at the
	// end of r's run rounds the other way.
	CHECK_FALSE(socket_reads::CertifiedSum(337.25f, 300.0f, 300.0f + 1.0f / 32768.0f, &out));
	// No r gives the recorded sum: not the engine's add of that root.
	CHECK_FALSE(socket_reads::CertifiedSum(1.0f, 1.0e30f, 0.0f, &out));
	// Nothing that is not finite.
	CHECK_FALSE(socket_reads::CertifiedSum(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, &out));
	CHECK_FALSE(socket_reads::CertifiedSum(1.0f, std::numeric_limits<float>::infinity(), 0.0f, &out));
	CHECK_FALSE(socket_reads::CertifiedSum(1.0f, 0.0f, std::numeric_limits<float>::infinity(), &out));
}

TEST_CASE("a pose read at another root is rebuilt at this one when certain")
{
	using socket_reads::Found;
	using socket_reads::Recorded;
	Cache cache;
	const Key key{0x77, socket_reads::NameKey(L"g_X_Trajectory"), 1};
	const float socket[3] = {337.25f, 12.5f, 95.0f};
	CHECK(cache.Record(key, Modelled(1, Mesh(300.0f, 0.0f, 0.0f), socket)) == Recorded::New);
	Position found;
	// The mesh 2 cm further: x is certain, y and z on the same roots.
	ModelTransform now = Mesh(302.0f, 0.0f, 0.0f);
	REQUIRE(cache.Find(key, 2, &now, &found) == Found::Rebuilt);
	const float there[3] = {339.25f, 12.5f, 95.0f};
	CHECK(found == socket_reads::FightPosition(there));
	// Not while the models stand where a restored state left them.
	CHECK(cache.Find(key, 2, nullptr, &found) == Found::OtherRoot);
	// Not into a finer binade (the mesh at the stage's centre).
	ModelTransform centre = Mesh(0.0f, 0.0f, 0.0f);
	CHECK(cache.Find(key, 2, &centre, &found) == Found::OtherRoot);
	// Not with another rotation (the other facing) or scale.
	ModelTransform turned = now;
	turned.rotation[2] = 1.0f;
	turned.rotation[3] = 0.0f;
	CHECK(cache.Find(key, 2, &turned, &found) == Found::OtherRoot);
	ModelTransform scaled = now;
	scaled.scale[1] = 0.4f;
	CHECK(cache.Find(key, 2, &scaled, &found) == Found::OtherRoot);
	// The scale's unused fourth lane does not count.
	ModelTransform lane = now;
	lane.scale[3] = 7.0f;
	CHECK(cache.Find(key, 2, &lane, &found) == Found::Rebuilt);
	// A reading without its transforms is never rebuilt from.
	Cache raw;
	CHECK(raw.Record(key, Raw(1, 3.3725f, 0.95f, -0.125f)) == Recorded::New);
	CHECK(raw.Find(key, 2, &now, &found) == Found::OtherRoot);
	// The same root still takes the reading as it was.
	REQUIRE(cache.Find(key, 1, &now, &found) == Found::Exact);
	CHECK(found == socket_reads::FightPosition(socket));
}

TEST_CASE("a fresh read the rebuild gets wrong turns rebuilding off for that pose, not for the battle")
{
	using socket_reads::Found;
	using socket_reads::Recorded;
	Cache cache;
	const Key key{0x88, socket_reads::NameKey(L"b_R_Hand"), 0};
	const float socket[3] = {337.25f, 12.5f, 95.0f};
	CHECK(cache.Record(key, Modelled(1, Mesh(300.0f, 0.0f, 0.0f), socket)) == Recorded::New);
	// A fresh read where the rebuild would have said what the engine says.
	const float agreeing[3] = {339.25f, 12.5f, 95.0f};
	CHECK(cache.Record(key, Modelled(2, Mesh(302.0f, 0.0f, 0.0f), agreeing)) == Recorded::New);
	CHECK(cache.Rebuilding(key));
	// Another pose, which will stay rebuildable.
	const Key fine{0x89, socket_reads::NameKey(L"b_R_Hand"), 0};
	CHECK(cache.Record(fine, Modelled(1, Mesh(300.0f, 0.0f, 0.0f), socket)) == Recorded::New);
	// One where it would not.
	const float other[3] = {341.5f, 12.5f, 95.0f};
	CHECK(cache.Record(key, Modelled(3, Mesh(304.0f, 0.0f, 0.0f), other)) == Recorded::RebuildDisagrees);
	CHECK_FALSE(cache.Rebuilding(key));
	CHECK(cache.Rebuilding(fine));
	CHECK(cache.NeverRebuilt() == 1);
	Position found;
	ModelTransform now = Mesh(306.0f, 0.0f, 0.0f);
	CHECK(cache.Find(key, 4, &now, &found) == Found::OtherRoot);
	// The other root entries of that pose are gone; the reading at this root stays and is given.
	CHECK(cache.Find(key, 1, &now, &found) == Found::OtherRoot);
	CHECK(cache.Find(key, 2, &now, &found) == Found::OtherRoot);
	REQUIRE(cache.Find(key, 3, &now, &found) == Found::Exact);
	CHECK(found == socket_reads::FightPosition(other));
	// The other pose is still rebuilt.
	CHECK(cache.Find(fine, 2, &now, &found) == Found::Rebuilt);
	// The disagreement is still counted as a conflict by the caller (Recorded::RebuildDisagrees),
	// and a new battle rebuilds that pose again.
	cache.Clear();
	CHECK(cache.Rebuilding(key));
	CHECK(cache.NeverRebuilt() == 0);
}

TEST_CASE("two readings that rebuild to different values give neither")
{
	using socket_reads::Found;
	using socket_reads::Recorded;
	Cache cache;
	const Key key{0x99, socket_reads::NameKey(L"b_L_Foot"), 0};
	const float first[3] = {337.25f, 0.0f, 0.0f};
	CHECK(cache.Record(key, Modelled(1, Mesh(300.0f, 0.0f, 0.0f), first)) == Recorded::New);
	// Read fresh with the mesh at 0, where the first cannot be rebuilt with
	// certainty (a finer binade), so nothing is checked as it is recorded.
	const float second[3] = {37.5f, 0.0f, 0.0f};
	CHECK(cache.Record(key, Modelled(2, Mesh(0.0f, 0.0f, 0.0f), second)) == Recorded::New);
	CHECK(cache.Rebuilding(key));
	// At 302 both are certain, 339.25 and 339.5: the pose key does not hold
	// all the value depends on, and this pose is not rebuilt from here on.
	Position found;
	ModelTransform now = Mesh(302.0f, 0.0f, 0.0f);
	CHECK(cache.Find(key, 3, &now, &found) == Found::OtherRoot);
	CHECK_FALSE(cache.Rebuilding(key));
	CHECK(cache.NeverRebuilt() == 1);
}

TEST_CASE("past its capacity the record drops its oldest poses, and a pose its oldest root")
{
	using socket_reads::Found;
	Cache cache(3);
	for (uint64_t pose = 1; pose <= 5; pose++) cache.Record(Key{pose, 7, 0}, Raw(1, (float)pose, 0, 0));
	CHECK(cache.Size() == 3);
	Position found;
	CHECK(cache.Find(Key{1, 7, 0}, 1, nullptr, &found) == Found::NoPose);
	CHECK(cache.Find(Key{2, 7, 0}, 1, nullptr, &found) == Found::NoPose);
	for (uint64_t pose = 3; pose <= 5; pose++) {
		REQUIRE(cache.Find(Key{pose, 7, 0}, 1, nullptr, &found) == Found::Exact);
		CHECK(found.xyzw[0] == (float)pose);
	}
	// A pose recorded again keeps its place and takes the new value.
	cache.Record(Key{3, 7, 0}, Raw(1, 30, 0, 0));
	CHECK(cache.Size() == 3);
	REQUIRE(cache.Find(Key{3, 7, 0}, 1, nullptr, &found) == Found::Exact);
	CHECK(found.xyzw[0] == 30.0f);
	// Roots: kRootsPerPose of them, then the oldest replaced.
	const Key key{9, 9, 0};
	Cache roots;
	for (uint64_t root = 1; root <= Cache::kRootsPerPose + 1; root++) roots.Record(key, Raw(root, (float)root, 0, 0));
	CHECK(roots.Find(key, 1, nullptr, &found) == Found::OtherRoot);
	for (uint64_t root = 2; root <= Cache::kRootsPerPose + 1; root++) {
		REQUIRE(roots.Find(key, root, nullptr, &found) == Found::Exact);
		CHECK(found.xyzw[0] == (float)root);
	}
	Cache none(0);
	none.Record(Key{1, 1, 0}, Raw(1, 1, 1, 1));
	CHECK(none.Size() == 0);
}

TEST_CASE("the battle's counts, as the log and the synctest's check read them")
{
	socket_reads::Counts counts;
	counts.recorded = 120;
	counts.exact = 30;
	counts.rebuilt = 4;
	counts.noPose = 2;
	counts.otherRoot = 1;
	counts.conflicting = 2;
	counts.disagreeing = 1;
	const std::string text = socket_reads::CountsText(counts, 77, 2);
	CHECK(text.find("120 fresh read(s) recorded") != std::string::npos);
	CHECK(text.find("30 given the straight read") != std::string::npos);
	CHECK(text.find("4 rebuilt") != std::string::npos);
	CHECK(text.find("2 of a pose never read fresh") != std::string::npos);
	CHECK(text.find("1 of a pose read fresh at other roots only") != std::string::npos);
	// Both kinds count as conflicting for tools/synctest.ps1 -check.
	CHECK(text.find(" 3 conflicting (1 of them a rebuild's)") != std::string::npos);
	CHECK(text.find("77 pose(s) kept; 2 pose(s) never rebuilt") != std::string::npos);
	CHECK(socket_reads::CountsText({}, 0, 0).find("0 conflicting") != std::string::npos);
	CHECK(socket_reads::CountsText({}, 0, 0).find("never rebuilt") == std::string::npos);
}

TEST_CASE("every skeletal mesh of a fighter's actor is kept at LOD 0")
{
	FakeClass skeletal(sfv::mesh::kSkeletalMeshComponentFlag | 1);
	FakeComponent mesh(skeletal, 1), second(skeletal, 0), layer0(skeletal, 2), layer1(skeletal, 1);
	FakeActor actor;
	actor.Set(&mesh, &second, {&layer0, &layer1});
	std::vector<socket_reads::MeshLod> seen;
	CHECK(socket_reads::ForceLodZero(actor.bytes, &seen) == 4);
	for (const FakeComponent* c : {&mesh, &second, &layer0, &layer1}) CHECK(c->Forced() == sfv::mesh::kForceLodZero);
	// Each also evaluates its pose every frame, rendered or not (found as 2: only montages when not rendered).
	for (const FakeComponent* c : {&mesh, &second, &layer0, &layer1}) CHECK(c->Flag() == sfv::mesh::kAlwaysTickPoseAndRefreshBones);
	REQUIRE(seen.size() == 4);
	// What the engine had picked is reported, component by component.
	CHECK(seen[0].component == 0);
	CHECK(seen[0].predicted == 1);
	CHECK(seen[1].component == 1);
	CHECK(seen[2].component == 2);
	CHECK(seen[2].predicted == 2);
	CHECK(seen[3].component == 3);
	CHECK(seen[3].updateFlag == 2);
	CHECK(seen[0].written);
	CHECK(seen[0].flagWritten);
	// Already at LOD 0: nothing written again.
	seen.clear();
	CHECK(socket_reads::ForceLodZero(actor.bytes, &seen) == 0);
	REQUIRE(seen.size() == 4);
	CHECK_FALSE(seen[0].written);
	CHECK_FALSE(seen[0].flagWritten);
	CHECK(seen[0].forced == sfv::mesh::kForceLodZero);
}

TEST_CASE("what is no usable skeletal mesh component is left alone")
{
	FakeClass skeletal(sfv::mesh::kSkeletalMeshComponentFlag), other(0x10);
	FakeComponent mesh(skeletal, 1), notSkeletal(other, 1), unusable(skeletal, 1), layer(skeletal, 1);
	Put(unusable.bytes, sfv::mesh::kObjectFlags, int32_t(sfv::mesh::kUnusable));
	FakeActor actor;
	actor.Set(&notSkeletal, &unusable, {&layer});
	CHECK(socket_reads::ForceLodZero(actor.bytes, nullptr) == 1);
	CHECK(notSkeletal.Forced() == 0);
	CHECK(unusable.Forced() == 0);
	CHECK(layer.Forced() == sfv::mesh::kForceLodZero);
	// A layer count past the bound, or below 1, reads no layer.
	FakeComponent fresh(skeletal, 1);
	for (const int32_t count : {0, -1, sfv::visual::kMostLayers + 1}) {
		actor.Set(nullptr, nullptr, {&fresh}, count);
		CHECK(socket_reads::ForceLodZero(actor.bytes, nullptr) == 0);
		CHECK(fresh.Forced() == 0);
	}
	actor.Set(&mesh, nullptr, {}, 0);
	CHECK(socket_reads::ForceLodZero(actor.bytes, nullptr) == 1);
	CHECK(socket_reads::ForceLodZero(nullptr, nullptr) == 0);
}
