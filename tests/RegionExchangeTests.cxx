// After a mismatch the two copies trade their region lists (core/
// RegionExchange.hxx): each ends with the other's whole list, over a lossy
// path too, and gives up at its time limit when nobody answers.

#include <doctest/doctest.h>

#include "core/RegionExchange.hxx"

#include <deque>
#include <vector>

using namespace sfve;
using sfve::start_state::Entry;

namespace {

// Two ends of one path in memory; about one datagram in `dropOneIn` is lost,
// picked by a fixed pseudo-random sequence (a fixed period would take the same
// parts of the two lists every round, since both send the same count).
class Link {
public:
	explicit Link(int dropOneIn = 0) : dropOneIn_(dropOneIn) {}

	class End final : public StartChannel {
	public:
		End(Link& link, int side) : link_(link), side_(side) {}
		bool Send(size_t peer, const uint8_t* data, size_t size) override
		{
			if (peer != 0) return false;
			if (link_.Drops()) return true;
			link_.queues_[1 - side_].emplace_back(data, data + size);
			return true;
		}
		bool Receive(size_t* peer, std::vector<uint8_t>* data) override
		{
			auto& queue = link_.queues_[side_];
			if (queue.empty()) return false;
			*peer = 0;
			*data = std::move(queue.front());
			queue.pop_front();
			return true;
		}

	private:
		Link& link_;
		int side_;
	};

private:
	bool Drops()
	{
		if (!dropOneIn_) return false;
		random_ = random_ * 1103515245u + 12345u;
		return (random_ >> 16) % (uint32_t)dropOneIn_ == 0;
	}

	std::deque<std::vector<uint8_t>> queues_[2];
	int dropOneIn_;
	uint32_t random_ = 1;
};

std::vector<Entry> List(size_t count, uint64_t salt)
{
	std::vector<Entry> list;
	for (size_t i = 0; i < count; i++) list.push_back({i % 7 ? 0x1000 + i : 0, (i * 2654435761u) ^ salt, (uint32_t)(4 + i), (uint32_t)i});
	return list;
}

uint64_t Run(RegionExchange& a, RegionExchange& b, uint64_t from)
{
	uint64_t now = from;
	while (!(a.Pump(now) & b.Pump(now)) && now - from < 10000) now += 5;
	return now - from;
}

} // namespace

TEST_CASE("each copy ends with the other's whole list")
{
	Link link;
	Link::End one(link, 0), two(link, 1);
	const std::vector<Entry> mine = List(550, 1), theirs = List(521, 2);
	RegionExchange a(one, mine, {77}), b(two, theirs, {77});
	a.Begin(1000);
	b.Begin(1000);
	const uint64_t took = Run(a, b, 1000);
	REQUIRE(a.Theirs().has_value());
	REQUIRE(b.Theirs().has_value());
	CHECK(a.Theirs()->size() == theirs.size());
	CHECK((*a.Theirs())[520].hash == theirs[520].hash);
	CHECK((*b.Theirs())[549].index == 549);
	CHECK(took < 1000);
}

TEST_CASE("lost datagrams are sent again until both have the other's list")
{
	Link link(3);
	Link::End one(link, 0), two(link, 1);
	RegionExchange a(one, List(200, 1), {77}), b(two, List(200, 2), {77});
	a.Begin(1000);
	b.Begin(1000);
	Run(a, b, 1000);
	CHECK(a.Theirs().has_value());
	CHECK(b.Theirs().has_value());
}

TEST_CASE("with nobody answering, or another mismatch's token, it gives up at its time limit")
{
	Link link;
	Link::End one(link, 0), two(link, 1);
	RegionExchange a(one, List(10, 1), {77, 600}), b(two, List(10, 2), {78, 600});
	a.Begin(1000);
	b.Begin(1000);
	const uint64_t took = Run(a, b, 1000);
	CHECK_FALSE(a.Theirs().has_value());
	CHECK_FALSE(b.Theirs().has_value());
	CHECK(took >= 600);
	CHECK(took < 1000);
}

TEST_CASE("parts round-trip, and anything else is refused")
{
	region_exchange::Part p;
	p.token = 5;
	p.part = 2;
	p.parts = 3;
	p.haveYours = true;
	p.entries = List(region_exchange::kEntriesPerPart, 9);
	const std::vector<uint8_t> bytes = region_exchange::Encode(p);
	CHECK(bytes.size() <= start_state::kMaxDatagram);
	region_exchange::Part back;
	REQUIRE(region_exchange::Decode(bytes.data(), bytes.size(), 5, &back));
	CHECK(back.part == 2);
	CHECK(back.haveYours);
	CHECK(back.entries.size() == region_exchange::kEntriesPerPart);
	CHECK(back.entries[3].hash == p.entries[3].hash);
	CHECK_FALSE(region_exchange::Decode(bytes.data(), bytes.size(), 6, &back));
	std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
	CHECK_FALSE(region_exchange::Decode(cut.data(), cut.size(), 5, &back));
	p.part = 3; // past `parts`
	const std::vector<uint8_t> past = region_exchange::Encode(p);
	CHECK_FALSE(region_exchange::Decode(past.data(), past.size(), 5, &back));
	// GGPO's own type byte (1..8) and the start agreement's (0xE1..0xE6) are never this.
	CHECK(bytes[4] == region_exchange::kType);
}
