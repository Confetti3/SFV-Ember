// The game's rollback region table as the adapter's bind read it
// (mod/RegionTable): the same entries are the same table wherever the game
// keeps them, and a cleared table is no change yet; other entries, or another
// count of them, are reported and stay reported; a table whose memory is gone
// is reported, not a fault.

#include <doctest/doctest.h>

#include "mod/RegionTable.hxx"
#include "mod/SfvBattleLayouts.hxx"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#include <string>

using sfve::mod::RegionTable;
namespace manager = sfve::mod::sfv::manager;

namespace {

struct Entry {
	void* ptr;
	uint64_t size;
};
static_assert(sizeof(Entry) == 16);

// A rollback manager with only its region vector, listing `begin..end`.
struct Manager {
	alignas(8) uint8_t bytes[0x40] = {};
	void List(const Entry* begin, const Entry* end)
	{
		memcpy(bytes + manager::kRegionsBegin, &begin, sizeof(begin));
		memcpy(bytes + manager::kRegionsEnd, &end, sizeof(end));
	}
};

bool Says(const char* why, const char* text)
{
	return why && std::string(why).find(text) != std::string::npos;
}

} // namespace

TEST_CASE("the same entries are the bound table, wherever they are kept, and a cleared table is no change yet")
{
	uint8_t a[64], b[32];
	Entry entries[2] = {{a, sizeof(a)}, {b, sizeof(b)}};
	Entry elsewhere[2] = {{a, sizeof(a)}, {b, sizeof(b)}};
	Manager m;
	m.List(entries, entries + 2);
	RegionTable table;
	CHECK(table.Changed() == nullptr); // nothing bound
	REQUIRE(table.Bind(m.bytes));
	CHECK(table.Changed() == nullptr);
	m.List(entries, entries); // cleared (the game's message 15)
	CHECK(table.Changed() == nullptr);
	m.List(elsewhere, elsewhere + 2); // built again (message 1) in other storage
	CHECK(table.Changed() == nullptr);
	table.Forget();
	m.List(entries, entries + 1);
	CHECK(table.Changed() == nullptr); // forgotten: nothing to compare
}

TEST_CASE("other entries, or another count of them, are reported, and stay reported")
{
	uint8_t a[64], b[32];
	Entry entries[3] = {{a, sizeof(a)}, {b, sizeof(b)}, {a + 8, 8}};
	Manager m;
	m.List(entries, entries + 2);
	RegionTable table;
	REQUIRE(table.Bind(m.bytes));
	SUBCASE("a region resized")
	{
		entries[1].size = 16;
		CHECK(Says(table.Changed(), "no longer lists the 2 regions it did at the bind"));
		entries[1].size = sizeof(b); // as it was: the failure is latched
		CHECK(Says(table.Changed(), "no longer lists the 2 regions"));
	}
	SUBCASE("a region moved")
	{
		entries[0].ptr = a + 1;
		CHECK(Says(table.Changed(), "no longer lists the 2 regions"));
	}
	SUBCASE("a region added")
	{
		m.List(entries, entries + 3);
		CHECK(Says(table.Changed(), "lists 3 regions, 2 at the bind"));
		m.List(entries, entries + 2);
		CHECK(Says(table.Changed(), "lists 3 regions, 2 at the bind"));
	}
}

TEST_CASE("a table whose memory is gone is reported, not a fault")
{
	void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	REQUIRE(page != nullptr);
	uint8_t a[64];
	auto* entries = static_cast<Entry*>(page);
	entries[0] = {a, sizeof(a)};
	Manager m;
	m.List(entries, entries + 1);
	RegionTable table;
	REQUIRE(table.Bind(m.bytes));
	CHECK(table.Changed() == nullptr);
	REQUIRE(VirtualFree(page, 0, MEM_RELEASE));
	CHECK(Says(table.Changed(), "no longer lists the 1 regions it did at the bind"));
}

TEST_CASE("an empty or unreadable table binds nothing")
{
	Entry entries[1] = {};
	Manager m;
	m.List(entries, entries);
	RegionTable table;
	CHECK_FALSE(table.Bind(m.bytes));
	CHECK_FALSE(table.Bind(nullptr));
	CHECK(table.Changed() == nullptr);
}
