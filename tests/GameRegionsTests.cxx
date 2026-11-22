// The game's runs as the rollback rules take them (mod/GameRegions,
// sfv::manager::kRunRules): Balrog's claw and mask records are their part's
// own run, whose padding (each process's own stack bytes) is left out of the
// peers' hash and nothing else (Bugsink EMBER-139/140), and the start state's
// repair follows the same marks.

#include <doctest/doctest.h>

#include "core/StartAgreement.hxx"
#include "core/StateRegions.hxx"
#include "mod/GameRegions.hxx"
#include "mod/SfvLayoutRules.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/SfvRegionNames.hxx"

#include <stdint.h>
#include <string.h>

#include <iterator>
#include <string>
#include <vector>

using sfve::HashScope;
using sfve::StateLayout;
using sfve::StateRegion;
using sfve::mod::AddGameRun;
using sfve::mod::NamedRun;

namespace {

namespace manager = sfve::mod::sfv::manager;

constexpr size_t kRunSize = 208;
// The run-relative dwords left out of the peers' hash: each record's padding
// before its first vector (+0x18, +0x1C) and after its timer (+0x54..+0x5C),
// in the claw's record at +0x00 and the mask's at +0x60.
constexpr size_t kPadding[] = {0x18, 0x1C, 0x54, 0x58, 0x5C, 0x78, 0x7C, 0xB4, 0xB8, 0xBC};

// The value rule (mod/GameMemory.hxx's LooksLikeAddress with no ranges
// published): a dword-aligned value from 1 TB to the top of user space.
bool ValueRule(uint64_t value)
{
	return (value & 3) == 0 && value >= (1ull << 40) && value <= 0x00007FFFFFFFFFFFull;
}

NamedRun BalrogRun()
{
	return {sfve::mod::RunName("CharaUnit[1].CharaUniquePart_008", 0xF0), "CharaUniquePart_008", 0xF0};
}

bool Bit(const std::vector<uint64_t>& marks, size_t i)
{
	return i / 64 < marks.size() && ((marks[i / 64] >> (i % 64)) & 1);
}

bool IsPadding(size_t at)
{
	for (const size_t pad : kPadding) {
		if (pad == at) return true;
	}
	return false;
}

// A copy's run, 16-byte aligned as the game's allocator gives the part (the
// run starts at +240), so the address test reads the words the game's do.
struct Run {
	alignas(16) uint8_t bytes[kRunSize] = {};
	void Put(size_t at, uint64_t value) { memcpy(bytes + at, &value, sizeof(value)); }
	uint32_t Dword(size_t at) const
	{
		uint32_t value;
		memcpy(&value, bytes + at, sizeof(value));
		return value;
	}
};

// The run as both copies held it at the first checkpoint of the 0.1.1 reports
// (EMBER-140, P1 on Windows 19045, and EMBER-139, P2 on 26200), the words the
// reports list; the mask's +0xB4 is set by each test.
Run ReportedRun()
{
	Run r;
	r.Put(0x00, 0x100000002ull);           // claw: state 2, phase 1
	r.Put(0x08, 0x3200000FFFFFFFFull);     // -1, endurance 800.0 (16.16)
	r.Put(0x10, 0x3C000003200000ull);      // endurance 800.0, 60.0
	r.Put(0x50, 0xFFFFFFFF00000000ull);    // timer 0, padding -1
	r.Put(0x60, 0x100000002ull);           // mask: state 2, phase 1
	r.Put(0x68, 0x3840000FFFFFFFFull);     // -1, endurance 900.0
	r.Put(0x70, 0x3C000003840000ull);      // endurance 900.0, 60.0
	r.Put(0x78, 0xFFFFFFFF00000000ull);    // padding
	r.Put(0xB8, 0xE528ACEAC0ull);          // padding
	r.Put(0xC0, 0xFFFFFFFFFFFFFFFFull);    // event, -1
	r.Put(0xC8, 9);                        // flags
	return r;
}

// One copy's layout of the run: as the rules name it, or unowned (game.N) as
// before Balrog's part was known.
StateLayout LayoutOf(Run& run, bool owned)
{
	StateLayout layout;
	layout.SetAddressTest(ValueRule);
	AddGameRun(layout, owned ? BalrogRun() : NamedRun{}, 224, run.bytes, kRunSize, nullptr);
	return layout;
}

} // namespace

TEST_CASE("Balrog's own part is a known owner, and the rules' text names its run and its peers' dwords")
{
	const std::string owners = sfve::mod::RegionOwnersText();
	CHECK(owners.find(", CharaVisualPart 0x3A64AD0 +1752/392, CharaUniquePart_008 0x3A67370 +240/208, AttackDataPart ") !=
		std::string::npos);
	const std::string& text = sfve::mod::LayoutRulesText();
	CHECK(text.find(" CharaUniquePart_008+240[24,32)/96/peers CharaUniquePart_008+240[84,96)/96/peers; ") != std::string::npos);
	// The rows before them kept their text (no "/peers").
	CHECK(text.find(" ProjectileGroupUnit+1280[144,160)/176 ") != std::string::npos);
}

TEST_CASE("Balrog's claw and mask run leaves each record's padding out of the peers' hash, and nothing else")
{
	Run run;
	int applied[std::size(manager::kRunRules)] = {};
	StateLayout layout;
	const size_t index = AddGameRun(layout, BalrogRun(), 224, run.bytes, kRunSize, applied);
	const StateRegion& r = layout.Regions().at(index);
	CHECK(r.name == "CharaUnit[1].CharaUniquePart_008+0xF0");
	CHECK(r.checked);
	CHECK(r.comparedWithPeers);
	for (size_t at = 0; at < kRunSize; at += 4) {
		CAPTURE(at);
		CHECK(Bit(r.peerIgnore, at / 4) == IsPadding(at));
		CHECK_FALSE(layout.IsIgnored(index, at)); // the synctest still checks every byte
	}
	// Both of its rows matched, and only they.
	int rows = 0;
	for (size_t k = 0; k < std::size(manager::kRunRules); k++) {
		CAPTURE(manager::kRunRules[k].owner);
		const bool balrog = strcmp(manager::kRunRules[k].owner, "CharaUniquePart_008") == 0;
		CHECK(applied[k] == (balrog ? 1 : 0));
		rows += balrog;
	}
	CHECK(rows == 2);
}

TEST_CASE("the 0.1.1 pair: the mask's padding no longer decides the peers' hash")
{
	// P1's +0xB4 held -1, so its word at +0xB0 was hashed; P2's held its heap's
	// top half, so the same word read as an address and was zeroed.
	Run p1 = ReportedRun(), p2 = ReportedRun();
	p1.Put(0xB0, 0xFFFFFFFF00000000ull);
	p2.Put(0xB0, 0x0000020600000000ull);
	// As 0.1.1 hashed them: the desync both reports name.
	CHECK(LayoutOf(p1, false).HashLive(HashScope::Peers) != LayoutOf(p2, false).HashLive(HashScope::Peers));
	// With the rows: equal.
	const StateLayout a = LayoutOf(p1, true), b = LayoutOf(p2, true);
	CHECK(a.LayoutId() == b.LayoutId());
	CHECK(a.HashLive(HashScope::Peers) == b.HashLive(HashScope::Peers));
	// Each process's own hash still reads them.
	CHECK(a.HashLive(HashScope::Local) != b.HashLive(HashScope::Local));

	// The rest of the padding, as other copies held it: a heap pointer's
	// copy at +0x18, -1 and a heap pointer at +0xB4 and +0xB8.
	p1.Put(0x18, 0x0000024D8EF6F000ull);
	p2.Put(0x18, 0xFFFFFFFF00000000ull);
	p1.Put(0xB8, 0x000001C9000B0000ull);
	p2.Put(0x58, 0x0000000100000001ull);
	CHECK(a.HashLive(HashScope::Peers) == b.HashLive(HashScope::Peers));
}

TEST_CASE("the mask's timer beside the padding is compared on every copy")
{
	// Two copies whose +0xB4 both held their heap's top half: the word at
	// +0xB0 read as an address on both, so a timer difference was never seen.
	Run p1 = ReportedRun(), p2 = ReportedRun();
	p1.Put(0xB0, 0x0000024D00370000ull); // timer 55.0
	p2.Put(0xB0, 0x000001C8000B0000ull); // timer 11.0
	CHECK(LayoutOf(p1, false).HashLive(HashScope::Peers) == LayoutOf(p2, false).HashLive(HashScope::Peers));
	CHECK(LayoutOf(p1, true).HashLive(HashScope::Peers) != LayoutOf(p2, true).HashLive(HashScope::Peers));
	// The claw's timer too.
	Run c1 = ReportedRun(), c2 = ReportedRun();
	c1.Put(0x50, 0xFFFFFFFF00B40000ull); // 180.0
	c2.Put(0x50, 0xFFFFFFFF00100000ull); // 16.0
	CHECK(LayoutOf(c1, true).HashLive(HashScope::Peers) != LayoutOf(c2, true).HashLive(HashScope::Peers));
	// Nothing in the run reads as an address once the padding is left out.
	std::vector<uint64_t> units;
	const StateLayout layout = LayoutOf(p1, true);
	layout.AddressUnits(0, p1.bytes, units);
	for (size_t unit = 0; unit < kRunSize / 4; unit++) CHECK_FALSE(Bit(units, unit));
}

TEST_CASE("the start state's repair takes P1's timer and leaves each copy's padding")
{
	Run p1 = ReportedRun(), p2 = ReportedRun();
	p1.Put(0xB0, 0xFFFFFFFF00370000ull); // timer 55.0, padding -1
	p1.Put(0xB8, 0xE528ACEAC0ull);
	p2.Put(0xB0, 0x0000020600000000ull); // timer 0, padding the heap's top half
	p2.Put(0xB8, 0);
	const StateLayout reference = LayoutOf(p1, true);
	const StateLayout follower = LayoutOf(p2, true);
	REQUIRE(reference.HashLive(HashScope::Peers) != follower.HashLive(HashScope::Peers));
	std::vector<uint64_t> referenceUnits;
	reference.AddressUnits(0, p1.bytes, referenceUnits);
	size_t kept = 0;
	const size_t written = sfve::start_state::ApplyRegion(follower, 0, p1.bytes, referenceUnits, &kept);
	CHECK(written == 1); // the timer
	CHECK(kept == 3);    // +0xB4, +0xB8 and +0xBC, each copy's own
	CHECK(p2.Dword(0xB0) == 0x00370000);
	CHECK(p2.Dword(0xB4) == 0x206);
	CHECK(p2.Dword(0xB8) == 0);
	CHECK(reference.HashLive(HashScope::Peers) == follower.HashLive(HashScope::Peers));
}

TEST_CASE("every other run keeps its rows: masked words, presentation, and nothing for a run no row names")
{
	alignas(16) uint8_t bytes[528] = {};
	StateLayout layout;
	// CharaUnit+0x764: the bone words [88, 112), in both hashes.
	const size_t chara = AddGameRun(layout, {"CharaUnit[0]+0x764", "CharaUnit", 0x764}, 3, bytes, 300, nullptr);
	for (size_t at = 0; at < 300; at += 4) {
		CAPTURE(at);
		CHECK(layout.IsIgnored(chara, at) == (at >= 88 && at < 112));
		CHECK_FALSE(Bit(layout.Regions()[chara].peerIgnore, at / 4));
	}
	// The camera, whatever its run: restored, not hashed.
	const size_t camera = AddGameRun(layout, {"CameraUnit[0]+0x100", "CameraUnit", 0x100}, 4, bytes, 528, nullptr);
	CHECK(layout.Regions()[camera].name == "CameraUnit[0]+0x100.presentation");
	CHECK_FALSE(layout.Regions()[camera].checked);
	// Balrog's part at another offset, and an unowned run: no marks.
	const size_t other = AddGameRun(layout, {"CharaUnit[1].CharaUniquePart_008+0xE8", "CharaUniquePart_008", 0xE8}, 5, bytes,
		kRunSize, nullptr);
	const size_t unowned = AddGameRun(layout, NamedRun{}, 6, bytes, kRunSize, nullptr);
	CHECK(layout.Regions()[unowned].name == "game.6");
	for (const size_t index : {other, unowned}) {
		const StateRegion& r = layout.Regions()[index];
		CHECK(r.checked);
		for (size_t at = 0; at < kRunSize; at += 4) {
			CHECK_FALSE(Bit(r.peerIgnore, at / 4));
			CHECK_FALSE(layout.IsIgnored(index, at));
		}
	}
}

namespace {

// A projectile group's run at +0x280: three blocks of 200 bytes, each 24
// projectile pointers and a count (SFV_REGION_MAP.md section 2).
constexpr size_t kGroupRunSize = 600;
constexpr size_t kGroupBlock = 200;
constexpr size_t kGroupPointers = 24;

NamedRun GroupRun()
{
	return {sfve::mod::RunName("ProjectileGroupUnit[0]", 0x280), "ProjectileGroupUnit", 0x280};
}

struct Group {
	alignas(16) uint8_t bytes[kGroupRunSize] = {};
	void Put(size_t at, uint64_t value) { memcpy(bytes + at, &value, sizeof(value)); }
	uint32_t Dword(size_t at) const
	{
		uint32_t value;
		memcpy(&value, bytes + at, sizeof(value));
		return value;
	}
};

// Group 0's first block in EMBER-147 (Z41 v KEN, 0.1.2) at checkpoint 20, as
// each copy's word lines print it; the other two blocks were zero.
constexpr uint64_t kP1Pointers[kGroupPointers] = {0x12FE3400, 0x12FE2800, 0x12FE1000, 0x7A81EC00, 0x7A81D400, 0x7A81C800,
	0x7A81C400, 0x7A81B800, 0x9E199000, 0xA1FEAC00, 0xA1FEDC00, 0xA1FED800, 0xA1FEF800, 0xA1FEF400, 0xA1FEF000, 0xA1FEEC00,
	0xA1FEE800, 0xA1FED400, 0xA1FED000, 0xA1FECC00, 0xA1FEC800, 0xA1FEC400, 0xA1FEC000, 0xA1FE9C00};
constexpr uint64_t kP2Pointers[kGroupPointers] = {0x1FD87345800, 0x1FD87342000, 0x1FD87347400, 0x1FD8734E800, 0x1FD8734E400,
	0x1FD87343400, 0x1FD87346400, 0x1FD0A101400, 0x1FD8C330C00, 0x1FD8C332800, 0x1FD8C332400, 0x1FD8C33D000, 0x1FD8C33B800,
	0x1FD8C33A800, 0x1FD8C33AC00, 0x1FD8C339800, 0x1FD8C337C00, 0x1FD8C338000, 0x1FD8C337800, 0x1FD8C337400, 0x1FD8C335800,
	0x1FD8C333400, 0x1FD8C333800, 0x1FD8C330400};

Group ReportedGroup(const uint64_t (&pointers)[kGroupPointers], uint64_t count = 0x18)
{
	Group r;
	for (size_t i = 0; i < kGroupPointers; i++) r.Put(8 * i, pointers[i]);
	r.Put(0xC0, count);
	return r;
}

// P1's 7 arenas (its heap below 1 TB, the low-heap rule), which the start
// agreement gave P2 too: after it both copies test a word by the value rule
// and these ranges.
constexpr uint64_t kP1Arenas[][2] = {{0x19850000, 0x1CFB0000}, {0x2B450000, 0x2C0A1000}, {0x32E90000, 0x36C31000},
	{0x4A990000, 0x4D140000}, {0x699C0000, 0x79D60000}, {0x7A1C0000, 0x7BE90000}, {0x8E910000, 0xCC640000}};

bool AgreedRule(uint64_t value)
{
	if (ValueRule(value)) return true;
	if ((value & 3) != 0) return false;
	for (const auto& range : kP1Arenas) {
		if (value >= range[0] && value < range[1]) return true;
	}
	return false;
}

// One copy's layout of the run: as the rules name it, or unowned (game.N),
// which hashes it as 0.1.2 did (no row named +0x280 then).
StateLayout GroupLayoutOf(Group& run, bool owned, bool (*test)(uint64_t))
{
	StateLayout layout;
	layout.SetAddressTest(test);
	AddGameRun(layout, owned ? GroupRun() : NamedRun{}, 300, run.bytes, kGroupRunSize, nullptr);
	return layout;
}

} // namespace

TEST_CASE("a projectile group's run leaves each block's 24 pointers out of the peers' hash and compares its counts")
{
	CHECK(sfve::mod::LayoutRulesText().find(" ProjectileGroupUnit+640[0,192)/200/peers ProjectileGroupUnit+1280[144,160)/176 ") !=
		std::string::npos);
	Group run;
	int applied[std::size(manager::kRunRules)] = {};
	StateLayout layout;
	const size_t index = AddGameRun(layout, GroupRun(), 300, run.bytes, kGroupRunSize, applied);
	const StateRegion& r = layout.Regions().at(index);
	CHECK(r.name == "ProjectileGroupUnit[0]+0x280");
	CHECK(r.checked);
	CHECK(r.comparedWithPeers);
	size_t left = 0;
	for (size_t at = 0; at < kGroupRunSize; at += 4) {
		CAPTURE(at);
		// The pointers of blocks 0, 1 and 2 (+0..+191, +200..+391, +400..+591);
		// the counts at +192, +392 and +592 are compared.
		const bool pointer = at % kGroupBlock < kGroupPointers * 8;
		CHECK(Bit(r.peerIgnore, at / 4) == pointer);
		CHECK_FALSE(layout.IsIgnored(index, at)); // the synctest still checks every byte
		left += pointer ? 1 : 0;
	}
	CHECK(left == 3 * kGroupPointers * 2);
	for (size_t k = 0; k < std::size(manager::kRunRules); k++) {
		CAPTURE(manager::kRunRules[k].owner);
		const bool row = strcmp(manager::kRunRules[k].owner, "ProjectileGroupUnit") == 0 && manager::kRunRules[k].offset == 0x280;
		CHECK(applied[k] == (row ? 1 : 0));
	}
	// The group's other run keeps its own row only: nothing left out for peers.
	alignas(16) uint8_t requests[1408] = {};
	StateLayout other;
	const size_t spawn =
		AddGameRun(other, {"ProjectileGroupUnit[0]+0x500", "ProjectileGroupUnit", 0x500}, 301, requests, 1408, nullptr);
	for (size_t at = 0; at < 1408; at += 4) CHECK_FALSE(Bit(other.Regions()[spawn].peerIgnore, at / 4));
}

TEST_CASE("the EMBER-147 pair: a low-heap P1 and a value-rule P2 hash the group's run alike")
{
	Group p1 = ReportedGroup(kP1Pointers), p2 = ReportedGroup(kP2Pointers);
	// As 0.1.2 hashed them, after the start agreement: P1's first three
	// pointers lie in none of the agreed ranges and were hashed as numbers;
	// P2's are all above 1 TB and zeroed. The desync both reports name.
	CHECK(GroupLayoutOf(p1, false, AgreedRule).HashLive(HashScope::Peers) !=
		  GroupLayoutOf(p2, false, AgreedRule).HashLive(HashScope::Peers));
	// With the row: equal, whatever rule each copy has.
	const uint64_t agreed = GroupLayoutOf(p1, true, AgreedRule).HashLive(HashScope::Peers);
	CHECK(GroupLayoutOf(p2, true, AgreedRule).HashLive(HashScope::Peers) == agreed);
	CHECK(GroupLayoutOf(p1, true, ValueRule).HashLive(HashScope::Peers) == agreed);
	CHECK(GroupLayoutOf(p2, true, ValueRule).HashLive(HashScope::Peers) == agreed);
	CHECK(GroupLayoutOf(p1, true, nullptr).HashLive(HashScope::Peers) == agreed); // no address test at all
	// Each process's own hash still reads the pointers.
	CHECK(GroupLayoutOf(p1, true, AgreedRule).HashLive(HashScope::Local) !=
		  GroupLayoutOf(p2, true, AgreedRule).HashLive(HashScope::Local));
	// A pointer in another block is left out too.
	p1.Put(kGroupBlock + 8, 0x12FE4C00);
	p2.Put(kGroupBlock + 8, 0x1FD8C339C00);
	CHECK(GroupLayoutOf(p1, true, AgreedRule).HashLive(HashScope::Peers) ==
		  GroupLayoutOf(p2, true, AgreedRule).HashLive(HashScope::Peers));
}

TEST_CASE("a projectile group's counts are compared on every copy")
{
	Group p1 = ReportedGroup(kP1Pointers), p2 = ReportedGroup(kP2Pointers, 0x17);
	CHECK(GroupLayoutOf(p1, true, AgreedRule).HashLive(HashScope::Peers) !=
		  GroupLayoutOf(p2, true, AgreedRule).HashLive(HashScope::Peers));
	Group q1 = ReportedGroup(kP1Pointers), q2 = ReportedGroup(kP2Pointers);
	q2.Put(2 * kGroupBlock + 0xC0, 1); // the third block's count
	CHECK(GroupLayoutOf(q1, true, ValueRule).HashLive(HashScope::Peers) !=
		  GroupLayoutOf(q2, true, ValueRule).HashLive(HashScope::Peers));
}

TEST_CASE("the start state's repair of the group's run takes P1's count and leaves each copy's pointers")
{
	Group p1 = ReportedGroup(kP1Pointers), p2 = ReportedGroup(kP2Pointers, 0x17);
	const StateLayout reference = GroupLayoutOf(p1, true, AgreedRule);
	const StateLayout follower = GroupLayoutOf(p2, true, AgreedRule);
	REQUIRE(reference.HashLive(HashScope::Peers) != follower.HashLive(HashScope::Peers));
	std::vector<uint64_t> referenceUnits;
	reference.AddressUnits(0, p1.bytes, referenceUnits);
	size_t kept = 0;
	const size_t written = sfve::start_state::ApplyRegion(follower, 0, p1.bytes, referenceUnits, &kept);
	CHECK(written == 1);               // the count
	CHECK(kept == kGroupPointers * 2); // the 48 dwords of the 24 pointers, each copy's own
	CHECK(p2.Dword(0xC0) == 0x18);
	CHECK(p2.Dword(0x00) == 0x87345800); // P2's own first pointer, untouched
	CHECK(reference.HashLive(HashScope::Peers) == follower.HashLive(HashScope::Peers));
}

namespace {

// A projectile's action part's run at +0xD8 (15808 bytes, RegionNames).
constexpr size_t kActionRunSize = 15808;
constexpr size_t kLane3 = 0xD4; // part +0x1AC: controller +236, w of its vector at +224

NamedRun ProjectileActionRun()
{
	return {sfve::mod::RunName("ProjectileUnit[23].ProjectileActionPart", 0xD8), "ProjectileActionPart", 0xD8};
}

struct ActionRun {
	alignas(16) uint8_t bytes[kActionRunSize] = {};
	void Put(size_t at, uint64_t value) { memcpy(bytes + at, &value, sizeof(value)); }
};

// ProjectileUnit[23]'s run in EMBER-147 match 0000000000000001.0.4 (0.1.2,
// 2026-11-22), as each copy's word lines print it: P1 on Windows 22000, P2 on
// 26200. Words both copies zero as addresses are each copy's own; every other
// word is equal but the dword at +0xD4.
ActionRun ReportedActionRun(bool p1)
{
	ActionRun r;
	r.Put(0x0000, p1 ? 0x1AAC3F7EC40ull : 0x180E76B1C20ull);
	r.Put(0x0028, 0xFFFFFFFFull);
	r.Put(0x00D0, p1 ? 0x4000000000ull : 0x3A00000000ull);
	r.Put(0x0118, p1 ? 0x7FF68445D7B0ull : 0x7FF721D9D7B0ull);
	r.Put(0x0120, 0xBE19999ABE19999Aull);
	r.Put(0x0128, 0x3E99999A00000000ull);
	r.Put(0x0130, 0x3E99999Aull);
	r.Put(0x0138, 0x1900190019ull);
	r.Put(0x0140, 0x48);
	r.Put(0x0148, 0x100000046ull);
	r.Put(0x0150, 0xA00000001ull);
	r.Put(0x0160, 0x1E);
	r.Put(0x0170, 0x370000);
	r.Put(0x0178, p1 ? 0x7FF68494B890ull : 0x7FF72228B890ull);
	r.Put(0x0180, 0x4001CD1E409FF1A1ull);
	r.Put(0x1098, 0x200000);
	r.Put(0x3DB8, 0x100000001ull);
	return r;
}

StateLayout ActionLayoutOf(ActionRun& run, bool owned)
{
	StateLayout layout;
	layout.SetAddressTest(ValueRule);
	AddGameRun(layout, owned ? ProjectileActionRun() : NamedRun{}, 120, run.bytes, kActionRunSize, nullptr);
	return layout;
}

} // namespace

TEST_CASE("a projectile's action run leaves its controller's fourth lane out of the peers' hash, and nothing else")
{
	ActionRun run = ReportedActionRun(true);
	int applied[std::size(manager::kRunRules)] = {};
	StateLayout layout;
	const size_t index = AddGameRun(layout, ProjectileActionRun(), 120, run.bytes, kActionRunSize, applied);
	const StateRegion& r = layout.Regions().at(index);
	CHECK(r.name == "ProjectileUnit[23].ProjectileActionPart+0xD8");
	CHECK(r.checked);
	CHECK(r.comparedWithPeers);
	for (size_t at = 0; at < kActionRunSize; at += 4) {
		CAPTURE(at);
		CHECK(Bit(r.peerIgnore, at / 4) == (at == kLane3));
		CHECK_FALSE(layout.IsIgnored(index, at)); // the synctest still checks every byte
	}
	for (size_t k = 0; k < std::size(manager::kRunRules); k++) {
		CAPTURE(manager::kRunRules[k].owner);
		CHECK(applied[k] == (strcmp(manager::kRunRules[k].owner, "ProjectileActionPart") == 0 ? 1 : 0));
	}
	CHECK(sfve::mod::LayoutRulesText().find(" ProjectileActionPart+216[212,216)/peers") != std::string::npos);
}

TEST_CASE("the EMBER-147 pair of 11-22: the controller's fourth lane no longer decides the peers' hash")
{
	ActionRun p1 = ReportedActionRun(true), p2 = ReportedActionRun(false);
	// As 0.1.2 hashed them: the desync both reports name.
	CHECK(ActionLayoutOf(p1, false).HashLive(HashScope::Peers) != ActionLayoutOf(p2, false).HashLive(HashScope::Peers));
	// With the row: equal.
	const StateLayout a = ActionLayoutOf(p1, true), b = ActionLayoutOf(p2, true);
	CHECK(a.LayoutId() == b.LayoutId());
	CHECK(a.HashLive(HashScope::Peers) == b.HashLive(HashScope::Peers));
	// Each process's own hash still reads it.
	CHECK(a.HashLive(HashScope::Local) != b.HashLive(HashScope::Local));
	// The vector's other lanes are compared: a difference in z is a desync.
	p2.Put(0x00D0, 0x3A00000001ull);
	CHECK(ActionLayoutOf(p1, true).HashLive(HashScope::Peers) != ActionLayoutOf(p2, true).HashLive(HashScope::Peers));
}
