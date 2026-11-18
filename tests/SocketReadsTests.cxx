// The fight's reads of a fighter's model (mod/SocketReads.hxx): the one rule
// a read is decided by (which call sites are the fight's, which are anchored
// and where from), the anchor's arithmetic and counts, and the meshes kept at
// LOD 0.

#include <doctest/doctest.h>

#include "mod/SfvOffsets.hxx"
#include "mod/SocketReads.hxx"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace sfve::mod;
using socket_reads::Position;
using socket_reads::ReadRule;
using socket_reads::Site;

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

// The call sites that are not the fight's (the camera, the interface, the
// visual tasks), by return rva.
constexpr uint32_t kNotTheFights[] = {0x37D48Eu, 0x31B10Eu, 0x31B1DEu, 0x32FA5Fu, 0x33C41Fu, 0x3E3EF0u, 0x3E3F57u, 0x38F401u,
	0x38F69Bu, 0x38F6B7u, 0x39D8BBu, 0x404B2Du, 0x434FB5u, 0x43986Cu, 0x439882u};
// The four that feed fight state.
constexpr uint32_t kKnownSites[] = {0x3BDB02u, 0x3BDF3Bu, 0x3BC294u, 0x3A4B79u};
// The spawn bone resolver: drawn only.
constexpr uint32_t kSpawnBoneResolver = 0x4CFC99u;

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

TEST_CASE("every call site of the socket read is classified by its return rva")
{
	// Bone 28 in Chara PreInput, the camera's handlers, CharaManager's, and the
	// head for Chara PostVisual (after the tick), CharaUniquePart_010's effect
	// at the head, the cursor's and the tutorial's interface reads.
	for (const uint32_t other : kNotTheFights) CHECK(socket_reads::SiteOf(other) == Site::NotTheFights);
	// The spawn bone resolver (any script's spawn with a bone, through rva
	// 0x32BAA0's jump, so from rva 0x4CFB70): its position only places an effect.
	CHECK(socket_reads::SiteOf(kSpawnBoneResolver) == Site::DrawOnly);
	for (const uint32_t site : kKnownSites) CHECK(socket_reads::SiteOf(site) == Site::Known);
	// Anything else, the function's own rva and a near miss included, is not classified.
	for (const uint32_t unknown : {0u, 0x3919D0u, 0x3BDB03u, 0x4CFC98u, 0xFFFFFFFFu}) CHECK(socket_reads::SiteOf(unknown) == Site::Unknown);
	for (const Site site : {Site::NotTheFights, Site::DrawOnly, Site::Known, Site::Unknown}) {
		CHECK(std::string(socket_reads::SiteName(site)) != "?");
	}
}

TEST_CASE("in a battle a session plays, only the reads that stay out of fight state are Live")
{
	// Not the fight's, and drawn only: the engine's value.
	for (const uint32_t other : kNotTheFights) {
		const ReadRule rule = socket_reads::Policy(other, L"b_C_Head", true);
		CHECK_FALSE(rule.anchor);
		CHECK(rule.site == Site::NotTheFights);
	}
	const ReadRule drawn = socket_reads::Policy(kSpawnBoneResolver, L"b_R_Hand", true);
	CHECK_FALSE(drawn.anchor);
	CHECK(drawn.site == Site::DrawOnly);
	// The four known sites: anchored with their own offsets.
	ReadRule item = socket_reads::Policy(0x3BDB02, L"Sock_Item_R", true);
	CHECK(item.anchor);
	CHECK(item.site == Site::Known);
	CHECK(item.offset.forward == 0.20f);
	CHECK(item.offset.up == 1.00f);
	item = socket_reads::Policy(0x3BDF3B, L"Sock_Item_L", true);
	CHECK(item.anchor);
	CHECK(item.offset.forward == 0.20f);
	const ReadRule prop = socket_reads::Policy(0x3BC294, L"b_C_Head", true);
	CHECK(prop.anchor);
	CHECK(prop.offset.forward == 0.00f);
	CHECK(prop.offset.up == 1.60f);
	const ReadRule claw = socket_reads::Policy(0x3A4B79, L"b_L_Hand", true);
	CHECK(claw.anchor);
	CHECK(claw.offset.forward == 0.30f);
	CHECK(claw.offset.up == 1.20f);
	CHECK_FALSE(claw.otherName);
	// The claw's site asked a socket it has no offset for: the head's, and said so.
	const ReadRule foot = socket_reads::Policy(0x3A4B79, L"b_R_Foot", true);
	CHECK(foot.anchor);
	CHECK(foot.offset.forward == 0.05f);
	CHECK(foot.offset.up == 1.60f);
	CHECK(foot.otherName);
}

TEST_CASE("a call site no one has classified is anchored with the head's offset, whatever the socket")
{
	for (const uint32_t unknown : {0x3BDB03u, 0x500000u, 0u}) {
		for (const wchar_t* name : {L"b_R_Hand", L"Sock_Item_R", L"b_C_Head", L""}) {
			const ReadRule rule = socket_reads::Policy(unknown, name, true);
			CHECK(rule.anchor);
			CHECK(rule.site == Site::Unknown);
			CHECK(rule.offset.forward == socket_reads::kHeadOffset.forward);
			CHECK(rule.offset.up == socket_reads::kHeadOffset.up);
			CHECK_FALSE(rule.otherName);
		}
	}
	CHECK(socket_reads::kHeadOffset.forward == 0.00f);
	CHECK(socket_reads::kHeadOffset.up == 1.60f);
}

TEST_CASE("outside a battle a session plays, every read is Live")
{
	std::vector<uint32_t> all(std::begin(kNotTheFights), std::end(kNotTheFights));
	all.insert(all.end(), std::begin(kKnownSites), std::end(kKnownSites));
	all.push_back(kSpawnBoneResolver);
	all.push_back(0x500000u); // not classified
	for (const uint32_t site : all) {
		const ReadRule rule = socket_reads::Policy(site, L"b_C_Head", false);
		CHECK_FALSE(rule.anchor);
		// The site is still named, for the log.
		CHECK(rule.site == socket_reads::SiteOf(site));
	}
}

TEST_CASE("the anchor rules' version is part of the room's build and moved with beta.10's rule")
{
	CHECK(socket_reads::kAnchorVersion == 2);
}

TEST_CASE("the anchor rules' text names every call site's class and every known site's offsets, for the layout id")
{
	const std::string text = socket_reads::AnchorRulesText();
	CHECK(text.rfind("2; not the fight's 0x37D48E 0x31B10E 0x31B1DE ", 0) == 0);
	for (const uint32_t site : kNotTheFights) {
		char rva[16];
		snprintf(rva, sizeof(rva), " 0x%X", site);
		CHECK(text.find(rva) < text.find("; drawn only"));
	}
	CHECK(text.find("; drawn only 0x4CFC99; known 0x3BDB02 b_R_Hand 0.200000003,1 b_L_Hand 0.200000003,1 b_C_Head "
					"0.200000003,1 other 0.200000003,1 0x3BDF3B ") != std::string::npos);
	CHECK(text.find(" 0x3BC294 b_R_Hand 0,1.60000002 b_L_Hand 0,1.60000002 b_C_Head 0,1.60000002 other 0,1.60000002 ") !=
		std::string::npos);
	CHECK(text.find(" 0x3A4B79 b_R_Hand 0.300000012,1.20000005 b_L_Hand 0.300000012,1.20000005 b_C_Head 0.0500000007,1.60000002 "
					"other 0.0500000007,1.60000002; head 0,1.60000002") != std::string::npos);
	CHECK(text.size() == text.rfind("; head 0,1.60000002") + std::string("; head 0,1.60000002").size());
}

TEST_CASE("the anchor's offsets by call site and socket name")
{
	using socket_reads::AnchorOffsetFor;
	bool other = true;
	// A hand-held item, whichever hand.
	for (const uint32_t site : {0x3BDB02u, 0x3BDF3Bu}) {
		for (const wchar_t* name : {L"Sock_Item_R", L"Sock_Item_L"}) {
			const auto offset = AnchorOffsetFor(site, name, &other);
			CHECK(offset.forward == 0.20f);
			CHECK(offset.up == 1.00f);
			CHECK_FALSE(other);
		}
	}
	// The dropped prop, at the head.
	auto offset = AnchorOffsetFor(0x3BC294, L"b_C_Head", &other);
	CHECK(offset.forward == 0.00f);
	CHECK(offset.up == 1.60f);
	CHECK_FALSE(other);
	// The claw's site: a hand, the head, any other name.
	for (const wchar_t* hand : {L"b_R_Hand", L"b_L_Hand"}) {
		offset = AnchorOffsetFor(0x3A4B79, hand, &other);
		CHECK(offset.forward == 0.30f);
		CHECK(offset.up == 1.20f);
		CHECK_FALSE(other);
	}
	offset = AnchorOffsetFor(0x3A4B79, L"b_C_Head", &other);
	CHECK(offset.forward == 0.05f);
	CHECK(offset.up == 1.60f);
	CHECK_FALSE(other);
	offset = AnchorOffsetFor(0x3A4B79, L"b_R_Foot", &other);
	CHECK(offset.forward == 0.05f);
	CHECK(offset.up == 1.60f);
	CHECK(other);
	// A name is its characters, not a prefix of them.
	AnchorOffsetFor(0x3A4B79, L"b_R_Hand_IK", &other);
	CHECK(other);
	// Any other site takes the head's offset.
	offset = AnchorOffsetFor(0x500000, L"b_R_Hand", &other);
	CHECK(offset.forward == 0.00f);
	CHECK(offset.up == 1.60f);
	CHECK_FALSE(other);
}

TEST_CASE("the anchor's value: the fight's position plus the offset, mirrored by facing, on the fight plane")
{
	using socket_reads::AnchorOffset;
	using socket_reads::AnchorPosition;
	const float base[4] = {3.25f, 0.5f, 7.0f, 9.0f}; // the position's z and w are not used
	const AnchorOffset offset{0.30f, 1.20f};
	const Position right = AnchorPosition(base, false, offset);
	CHECK(right.xyzw[0] == 3.25f + 0.30f);
	CHECK(right.xyzw[1] == 0.5f + 1.20f);
	CHECK(right.xyzw[2] == 0.0f);
	CHECK(right.xyzw[3] == 1.0f);
	const Position left = AnchorPosition(base, true, offset);
	CHECK(left.xyzw[0] == 3.25f - 0.30f);
	CHECK(left.xyzw[1] == right.xyzw[1]);
	CHECK(left.xyzw[2] == 0.0f);
	CHECK(left.xyzw[3] == 1.0f);
	// A zero forward offset (the head's) is the same either way.
	const float away[4] = {-4.0f, 0.0f, 0.0f, 1.0f};
	const Position head = AnchorPosition(away, true, {0.00f, 1.60f});
	CHECK(head.xyzw[0] == -4.0f);
	CHECK(head.xyzw[1] == 1.60f);
	// Only the position, facing and offset decide it: the same inputs give the same bits.
	CHECK(AnchorPosition(base, true, offset) == left);
}

TEST_CASE("the counts text has every count")
{
	socket_reads::Counts counts;
	counts.anchored = 14;
	counts.unknownSites = 3;
	counts.anchorUnresolved = 2;
	CHECK(socket_reads::CountsText(counts) == "14 given from the fight's position (3 at a site not classified), 2 anchor unresolved");
	// Its longest, every count at 20 digits, fits the desync report's allowance (DesyncReportTests: 160).
	counts.anchored = counts.unknownSites = counts.anchorUnresolved = UINT64_MAX;
	CHECK(socket_reads::CountsText(counts).size() < 160);
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
