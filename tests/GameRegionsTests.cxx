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
