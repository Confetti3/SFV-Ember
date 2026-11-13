// The start state's agreement (core/StartAgreement.hxx): a copy whose state
// differs from P1's before frame 0 takes P1's bytes where it may, and the
// two then play the same match.

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "core/StartAgreement.hxx"

#include <windows.h>

#include <string.h>

#include <algorithm>
#include <deque>

using namespace sfve;
using namespace sfve::testbed;
using namespace sfve::start_state;

namespace {

// MockFighter's world (MockFighter.hxx, 96 bytes without padding): its frame
// and generator, then two fighters of eight int32 {x, y, vx, vy, health,
// attackFrames, hitstun, facing}, its heap pointer (masked), the round
// timer, the KO frame, a clock each game reads its own of (left out of the
// peers' hash) and the skip frame.
constexpr size_t kWorldRng = 4;
constexpr size_t kP2Health = 8 + 32 + 16;
constexpr size_t kOwner = 72;
constexpr size_t kClock = 88;

uint8_t* World(MockFighter& game)
{
	const StateRegion* r = game.Layout().Find("world");
	REQUIRE(r != nullptr);
	return r->ptr;
}

int32_t& Int(MockFighter& game, size_t offset)
{
	return *reinterpret_cast<int32_t*>(World(game) + offset);
}

// One process's datagram paths between a reference (its peers 0..n-1) and
// its followers, in memory; every `dropEvery`th datagram is lost.
class Exchange {
public:
	explicit Exchange(size_t followers, int dropEvery = 0) : toFollower_(followers), dropEvery_(dropEvery) {}

	class End final : public StartChannel {
	public:
		End(Exchange& x, int follower) : x_(x), follower_(follower) {}
		bool Send(size_t peer, const uint8_t* data, size_t size) override
		{
			if (x_.dropEvery_ && ++x_.sent_ % x_.dropEvery_ == 0) return true;
			std::vector<uint8_t> bytes(data, data + size);
			if (follower_ < 0) {
				if (peer >= x_.toFollower_.size()) return false;
				x_.toFollower_[peer].push_back(std::move(bytes));
			} else {
				x_.toReference_.push_back({(size_t)follower_, std::move(bytes)});
			}
			return true;
		}
		bool Receive(size_t* peer, std::vector<uint8_t>* data) override
		{
			if (follower_ < 0) {
				if (x_.toReference_.empty()) return false;
				*peer = x_.toReference_.front().first;
				*data = std::move(x_.toReference_.front().second);
				x_.toReference_.pop_front();
				return true;
			}
			auto& queue = x_.toFollower_[(size_t)follower_];
			if (queue.empty()) return false;
			*peer = 0;
			*data = std::move(queue.front());
			queue.pop_front();
			return true;
		}

	private:
		Exchange& x_;
		int follower_; // -1: the reference
	};

private:
	std::vector<std::deque<std::vector<uint8_t>>> toFollower_;
	std::deque<std::pair<size_t, std::vector<uint8_t>>> toReference_;
	int dropEvery_;
	int sent_ = 0;
};

StartAgreement::Config Role(StartAgreement::Role role, size_t requiredPeers = 1)
{
	StartAgreement::Config c;
	c.role = role;
	c.token = 0x5F0E5EED;
	c.requiredPeers = requiredPeers;
	return c;
}

// Pumps every agreement (begun at `from`) on a clock of 5 ms a step until all
// have finished or `limitMs` has passed; the time it took.
uint64_t Run(std::vector<StartAgreement*> all, uint64_t from, uint64_t limitMs = 20000)
{
	uint64_t now = from;
	for (;;) {
		bool finished = true;
		for (StartAgreement* a : all) finished = a->Pump(now) && finished;
		if (finished || now - from > limitMs) return now - from;
		now += 5;
	}
}

std::vector<uint64_t> PeerHashesOverFrames(MockFighter& game, int frames)
{
	std::vector<uint64_t> hashes{game.Layout().HashLive(HashScope::Peers)};
	for (int f = 0; f < frames; f++) {
		game.Advance(ScriptedFrame(f, kLoopbackSeed), false);
		hashes.push_back(game.Layout().HashLive(HashScope::Peers));
	}
	return hashes;
}

} // namespace

TEST_CASE("a follower whose start state differs takes P1's bytes, and the two then play alike")
{
	MockFighter p1, p2;
	Int(p2, kP2Health) -= 7;
	Int(p2, kWorldRng) += 1;
	const int32_t ownClock = Int(p2, kClock);
	const uint64_t ownOwner = *reinterpret_cast<uint64_t*>(World(p2) + kOwner);
	REQUIRE(ownClock != Int(p1, kClock));
	REQUIRE(p1.Layout().HashLive(HashScope::Peers) != p2.Layout().HashLive(HashScope::Peers));

	Exchange x(1);
	Exchange::End referenceEnd(x, -1), followerEnd(x, 0);
	StartAgreement reference(p1, referenceEnd, Role(StartAgreement::Role::Reference));
	StartAgreement follower(p2, followerEnd, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 1);
	follower.Begin(1000);
	Run({&reference, &follower}, 1000);

	CHECK(follower.GetReport().result == StartAgreement::Result::Repaired);
	CHECK(reference.GetReport().result == StartAgreement::Result::Repaired);
	CHECK(reference.GetReport().peers.at(0) == StartAgreement::Result::Repaired);
	REQUIRE(follower.GetReport().regions.size() == 1);
	const StartAgreement::RegionOutcome& world = follower.GetReport().regions[0];
	CHECK(world.name == "world");
	CHECK(world.unitsWritten == 2); // the generator and P2's health
	CHECK(world.unitsKept >= 1);    // the clock (and the heap pointer, when its bytes differ)
	CHECK(world.after == world.reference);
	CHECK(follower.GetReport().localAfter == reference.GetReport().localBefore);
	CHECK(p2.Layout().HashLive(HashScope::Peers) == p1.Layout().HashLive(HashScope::Peers));
	// Each process's own: never taken from the other.
	CHECK(Int(p2, kClock) == ownClock);
	CHECK(*reinterpret_cast<uint64_t*>(World(p2) + kOwner) == ownOwner);
	CHECK(follower.GetReport().stateBefore.size() == p2.Layout().TotalSize());

	CHECK(PeerHashesOverFrames(p1, 600) == PeerHashesOverFrames(p2, 600));
}

TEST_CASE("an equal start state is agreed on without a byte written")
{
	MockFighter p1, p2;
	const std::vector<uint8_t> before(World(p2), World(p2) + 96);
	Exchange x(1);
	Exchange::End referenceEnd(x, -1), followerEnd(x, 0);
	StartAgreement reference(p1, referenceEnd, Role(StartAgreement::Role::Reference));
	StartAgreement follower(p2, followerEnd, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 1);
	follower.Begin(1000);
	Run({&reference, &follower}, 1000);
	CHECK(follower.GetReport().result == StartAgreement::Result::Agreed);
	CHECK(reference.GetReport().result == StartAgreement::Result::Agreed);
	CHECK(follower.GetReport().regions.empty());
	CHECK(memcmp(before.data(), World(p2), 96) == 0);
}

TEST_CASE("lost datagrams are asked for again")
{
	MockFighter p1, p2;
	Int(p2, kP2Health) -= 3;
	Exchange x(1, 3);
	Exchange::End referenceEnd(x, -1), followerEnd(x, 0);
	StartAgreement reference(p1, referenceEnd, Role(StartAgreement::Role::Reference));
	StartAgreement follower(p2, followerEnd, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 1);
	follower.Begin(1000);
	Run({&reference, &follower}, 1000);
	CHECK(follower.GetReport().result == StartAgreement::Result::Repaired);
	CHECK(p2.Layout().HashLive(HashScope::Peers) == p1.Layout().HashLive(HashScope::Peers));
}

TEST_CASE("a watcher takes P1's start state as P2 does, and P1 waits for both")
{
	MockFighter p1, p2, watcher;
	Int(p2, kP2Health) -= 5;
	Int(watcher, kWorldRng) ^= 0x55;
	Exchange x(2);
	Exchange::End referenceEnd(x, -1), p2End(x, 0), watcherEnd(x, 1);
	StartAgreement reference(p1, referenceEnd, Role(StartAgreement::Role::Reference, 1));
	StartAgreement second(p2, p2End, Role(StartAgreement::Role::Follower));
	StartAgreement watching(watcher, watcherEnd, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 2);
	second.Begin(1000);
	watching.Begin(1000);
	Run({&reference, &second, &watching}, 1000);
	CHECK(second.GetReport().result == StartAgreement::Result::Repaired);
	CHECK(watching.GetReport().result == StartAgreement::Result::Repaired);
	CHECK(reference.GetReport().peers.at(1) == StartAgreement::Result::Repaired);
	const uint64_t hash = p1.Layout().HashLive(HashScope::Peers);
	CHECK(p2.Layout().HashLive(HashScope::Peers) == hash);
	CHECK(watcher.Layout().HashLive(HashScope::Peers) == hash);
}

TEST_CASE("P1 waits for a watcher only a short grace after P2")
{
	MockFighter p1, p2;
	Exchange x(2);
	Exchange::End referenceEnd(x, -1), p2End(x, 0);
	StartAgreement::Config c = Role(StartAgreement::Role::Reference, 1);
	c.watcherGraceMs = 300;
	StartAgreement reference(p1, referenceEnd, c);
	StartAgreement second(p2, p2End, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 2); // the watcher (peer 1) never answers
	second.Begin(1000);
	const uint64_t took = Run({&reference, &second}, 1000);
	CHECK(reference.GetReport().result == StartAgreement::Result::Agreed);
	CHECK(reference.GetReport().peers.at(1) == StartAgreement::Result::Running);
	CHECK(took >= 300);
	CHECK(took < c.timeLimitMs);
}

TEST_CASE("with nobody answering, both give up at the time limit")
{
	MockFighter p1, p2;
	Exchange x(1);
	Exchange::End followerEnd(x, 0);
	StartAgreement follower(p2, followerEnd, Role(StartAgreement::Role::Follower));
	follower.Begin(1000);
	Run({&follower}, 1000);
	CHECK(follower.GetReport().result == StartAgreement::Result::TimedOut);
	CHECK(follower.GetReport().elapsedMs >= 8000);
	CHECK(follower.GetReport().reference == 0);

	Exchange y(1);
	Exchange::End alone(y, -1);
	StartAgreement reference(p1, alone, Role(StartAgreement::Role::Reference));
	reference.Begin(1000, 1);
	Run({&reference}, 1000);
	CHECK(reference.GetReport().result == StartAgreement::Result::TimedOut);
}

TEST_CASE("another layout is compared no further")
{
	MockFighter p1, p2(kBugUnmaskedClock); // the clock is compared: another layout
	Int(p2, kP2Health) -= 1;
	Exchange x(1);
	Exchange::End referenceEnd(x, -1), followerEnd(x, 0);
	StartAgreement reference(p1, referenceEnd, Role(StartAgreement::Role::Reference));
	StartAgreement follower(p2, followerEnd, Role(StartAgreement::Role::Follower));
	reference.Begin(1000, 1);
	follower.Begin(1000);
	Run({&reference, &follower}, 1000);
	CHECK(follower.GetReport().result == StartAgreement::Result::LayoutsDiffer);
	CHECK(follower.GetReport().regions.empty());
}

TEST_CASE("regions pair by stable name, else by hash, else by a size only one of them has")
{
	const auto entry = [](uint64_t id, uint64_t hash, uint32_t size, uint32_t index) { return Entry{id, hash, size, index}; };
	// Stable: 11 equal, 12 differs. Unstable (id 0), listed in another order:
	// 24 bytes equal, 40 bytes differs (the only 40 on each side), two of 16
	// bytes differ (which is which cannot be told).
	const std::vector<Entry> local = {entry(11, 1, 8, 0), entry(12, 2, 8, 1), entry(0, 3, 24, 2), entry(0, 4, 40, 3),
		entry(0, 5, 16, 4), entry(0, 6, 16, 5), entry(77, 9, 4, 6)};
	const std::vector<Entry> reference = {entry(0, 50, 16, 0), entry(0, 51, 16, 1), entry(0, 41, 40, 2), entry(0, 3, 24, 3),
		entry(12, 22, 8, 4), entry(11, 1, 8, 5)};
	const Pairing p = PairRegions(local, reference);
	CHECK(p.equal == 2);
	REQUIRE(p.differing.size() == 2);
	bool stable = false, bySize = false;
	for (const Pairing::Pair& pair : p.differing) {
		stable |= pair.local == 1 && pair.reference == 4;
		bySize |= pair.local == 3 && pair.reference == 2 && pair.size == 40;
	}
	CHECK(stable);
	CHECK(bySize);
	CHECK(p.unpairedLocal.size() == 3);     // the two 16s and the one P1 lacks
	CHECK(p.unpairedReference.size() == 2); // its two 16s
}

TEST_CASE("the repair leaves addresses, masked words and the peers' own dwords alone")
{
	alignas(8) uint8_t live[48] = {};
	alignas(8) uint8_t theirs[48] = {};
	StateLayout layout;
	// An "address": a value whose high half is 0x7FF0 (a test's own rule).
	layout.SetAddressTest([](uint64_t v) { return (v >> 32) == 0x7FF0; });
	const size_t r = layout.Add("r", live, sizeof(live));
	layout.IgnoreWord(r, 24);     // bytes 24..31: masked
	layout.IgnoreForPeers(r, 32); // bytes 32..35: each process's own
	const auto put = [](uint8_t* bytes, size_t at, uint64_t v) { memcpy(bytes + at, &v, 8); };
	put(live, 8, 0x00007FF000001000ull);   // this copy's address
	put(theirs, 8, 0x00007FF000002000ull); // the reference's
	put(live, 16, 5);
	put(theirs, 16, 6);                    // plain state: taken
	put(live, 24, 1);
	put(theirs, 24, 2);                    // masked: kept
	put(live, 32, 3);
	put(theirs, 32, 4);                    // the peers' own: kept
	put(theirs, 40, 0x00007FF000003000ull); // an address only the reference holds: kept
	std::vector<uint64_t> theirUnits;
	layout.AddressUnits(r, theirs, theirUnits);
	size_t kept = 0;
	const size_t written = ApplyRegion(layout, r, theirs, theirUnits, &kept);
	CHECK(written == 1);
	CHECK(live[16] == 6);
	CHECK(live[24] == 1);
	CHECK(live[32] == 3);
	uint64_t at8 = 0, at40 = 0;
	memcpy(&at8, live + 8, 8);
	memcpy(&at40, live + 40, 8);
	CHECK(at8 == 0x00007FF000001000ull);
	CHECK(at40 == 0);
	// The addresses' differing low dwords (their high halves agree), the masked
	// word, the peers' dword, and both dwords of the address P1 alone holds.
	CHECK(kept == 5);
}

TEST_CASE("the repair writes a pair of values 4 bytes off a boundary that reads like an address")
{
	// Two dwords of fight state side by side (280, then 293 on this copy and
	// 292 on P1's), 4 bytes off an 8-byte boundary: together they pass the
	// mod's value test (GameMemory.hxx LooksLikeAddress). The address rule
	// tested every 4-byte boundary once, took them for an address on both
	// sides and left the differing value as it was; only words on 8-byte
	// boundaries are tested now, so the repair takes P1's value.
	const auto heapLike = [](uint64_t v) { return v >= (1ull << 40) && v <= 0x00007FFFFFFFFFFFull && (v & 3) == 0; };
	alignas(8) uint8_t live[32] = {};
	alignas(8) uint8_t theirs[32] = {};
	StateLayout layout;
	layout.SetAddressTest(heapLike);
	const size_t r = layout.Add("r", live, sizeof(live));
	const uint32_t mine[] = {1, 280, 293}, reference[] = {1, 280, 292};
	memcpy(live + 8, mine, sizeof(mine));
	memcpy(theirs + 8, reference, sizeof(reference));
	REQUIRE(heapLike(0x0000012500000118ull));
	REQUIRE(heapLike(0x0000012400000118ull));
	// An address of each copy's own on the next boundary is still left alone.
	const uint64_t ownAddress = 0x00000204F5F982A0ull, theirAddress = 0x00000246994182A0ull;
	memcpy(live + 24, &ownAddress, 8);
	memcpy(theirs + 24, &theirAddress, 8);
	std::vector<uint64_t> theirUnits;
	layout.AddressUnits(r, theirs, theirUnits);
	size_t kept = 0;
	const size_t written = ApplyRegion(layout, r, theirs, theirUnits, &kept);
	CHECK(written == 1);
	uint32_t value = 0;
	memcpy(&value, live + 16, 4);
	CHECK(value == 292);
	uint64_t address = 0;
	memcpy(&address, live + 24, 8);
	CHECK(address == ownAddress);
	CHECK(kept == 2); // the address's two dwords
}

TEST_CASE("datagrams round-trip, and anything else is refused")
{
	Message m;
	m.type = Type::Bytes;
	m.token = 42;
	m.region = 7;
	m.offset = (uint32_t)kChunkBytes;
	m.regionSize = (uint32_t)kChunkBytes + 10;
	m.bytes.assign(10, 0xAB);
	m.addressBits.assign(1, 0x05);
	const std::vector<uint8_t> bytes = Encode(m);
	CHECK(bytes.size() <= kMaxDatagram);
	Message back;
	REQUIRE(Decode(bytes.data(), bytes.size(), 42, &back));
	CHECK(back.region == 7);
	CHECK(back.bytes == m.bytes);
	CHECK(back.addressBits == m.addressBits);
	CHECK_FALSE(Decode(bytes.data(), bytes.size(), 43, &back)); // another match's
	std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
	CHECK_FALSE(Decode(cut.data(), cut.size(), 42, &back));

	Message entries;
	entries.type = Type::Entries;
	entries.token = 42;
	entries.entries.assign(kEntriesPerPart, Entry{1, 2, 3, 4});
	CHECK(Encode(entries).size() <= kMaxDatagram);

	// A GGPO datagram (magic, sequence, type 1..8) is never one of these, and
	// each of these carries a type GGPO does not know where GGPO reads its own.
	const uint8_t ggpo[] = {0x45, 0x53, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	CHECK_FALSE(Decode(ggpo, sizeof(ggpo), 0, &back));
	for (const Type t : {Type::Ask, Type::Summary, Type::Entries, Type::Bytes, Type::Done, Type::DoneAck}) {
		Message any;
		any.type = t;
		CHECK(Encode(any)[4] > 8);
	}
}

TEST_CASE("two loopback peers with one region perturbed before frame 0 agree over UDP, then play the same match")
{
	StartAgreement::Report followerReport;
	const auto agree = [&](MockFighter& a, MockFighter& b, uint16_t portA, uint16_t portB) {
		Int(b, kP2Health) -= 9;
		UdpStartChannel channelA, channelB;
		REQUIRE(channelA.Open(portA, "127.0.0.1", {portB}));
		REQUIRE(channelB.Open(portB, "127.0.0.1", {portA}));
		StartAgreement reference(a, channelA, Role(StartAgreement::Role::Reference));
		StartAgreement follower(b, channelB, Role(StartAgreement::Role::Follower));
		const ULONGLONG start = GetTickCount64();
		reference.Begin(start, 1);
		follower.Begin(start);
		for (;;) {
			const ULONGLONG now = GetTickCount64();
			const bool referenceDone = reference.Pump(now);
			const bool followerDone = follower.Pump(now);
			if ((referenceDone && followerDone) || now - start > 15000) break;
			Sleep(1);
		}
		followerReport = follower.GetReport();
		// Both channels close here, before the sessions bind the ports.
	};
	LoopbackPair pair(kBugNone, 0, false, agree);
	CHECK(followerReport.result == StartAgreement::Result::Repaired);
	REQUIRE(pair.RunTo(400));
	pair.RunTo(420);
	const int confirmed = std::min(pair.a.LastConfirmedFrame(), pair.b.LastConfirmedFrame());
	REQUIRE(confirmed >= 300);
	int compared = 0;
	for (int f = 0; f <= confirmed; f += RollbackSession::kCheckpointInterval) {
		uint64_t ha = 0, hb = 0;
		if (!pair.a.HashAt(f, &ha) || !pair.b.HashAt(f, &hb)) continue;
		INFO("frame " << f);
		CHECK(ha == hb);
		compared++;
	}
	CHECK(compared >= 3);
}
