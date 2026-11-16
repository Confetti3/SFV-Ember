// What each copy's battle computes with (core/MatchData.hxx): two copies'
// facts compare unit by unit, only what both know counts, and the wire form
// round-trips and refuses anything else.

#include <doctest/doctest.h>

#include "core/MatchData.hxx"

using namespace sfve::match_data;

namespace {

Fighter Unit(const char* code, int8_t side, int32_t version, std::optional<uint64_t> data = std::nullopt)
{
	Fighter f;
	f.code = code;
	f.side = side;
	f.version = version;
	f.dataKnown = data.has_value();
	f.data = data.value_or(0);
	return f;
}

Facts AkiraMirror()
{
	return {{Unit("Z41", 0, 61, 0x1111), Unit("Z25", 1, 60, 0x2222), Unit("Z41", 0, 61, 0x3333), Unit("Z41", 1, 61, 0x3333)}};
}

} // namespace

TEST_CASE("equal facts agree; the first differing unit is named with what differs")
{
	const Facts here = AkiraMirror();
	CHECK_FALSE(Compare(here, here).has_value());

	Facts older = here;
	older.fighters[0].version = 43; // that copy's list ended at an older season
	const auto version = Compare(here, older);
	REQUIRE(version.has_value());
	CHECK(version->what == Difference::What::Version);
	CHECK(version->unit == 0);
	CHECK(version->local.version == 61);
	CHECK(version->reference.version == 43);
	CHECK(Describe(*version) == "game data differs (fighter Z41): balance version 61 here, 43 at the other copy");
	CHECK(Notice(*version).find("(fighter Z41)") != std::string::npos);

	Facts altered = here;
	altered.fighters[1].data = 0x9999; // a mod's move data at the same version
	const auto data = Compare(here, altered);
	REQUIRE(data.has_value());
	CHECK(data->what == Difference::What::Data);
	CHECK(data->unit == 1);

	Facts swapped = here;
	swapped.fighters[0].side = 1;
	const auto side = Compare(here, swapped);
	REQUIRE(side.has_value());
	CHECK(side->what == Difference::What::Fighter);

	Facts fewer = here;
	fewer.fighters.pop_back();
	const auto units = Compare(here, fewer);
	REQUIRE(units.has_value());
	CHECK(units->what == Difference::What::Units);
	CHECK(units->unit == 3);
	CHECK(units->local.code == "Z41");
}

TEST_CASE("what either copy could not read is not compared")
{
	const Facts here = AkiraMirror();
	Facts unread = here;
	unread.fighters[0].version = kUnknownVersion;
	unread.fighters[1].dataKnown = false;
	unread.fighters[1].data = 0;
	CHECK_FALSE(Compare(here, unread).has_value());
	CHECK_FALSE(Compare(unread, here).has_value());
	// The fighter itself is always compared.
	unread.fighters[2].code = "Z25";
	CHECK(Compare(here, unread).has_value());
}

TEST_CASE("the side's version is its own fighter's, before any partner")
{
	const Facts facts = AkiraMirror();
	CHECK(SideVersion(facts, 0) == 61);
	CHECK(SideVersion(facts, 1) == 60);
	CHECK(SideVersion(facts, 2) == kUnknownVersion);
	CHECK(SideVersion(Facts{}, 0) == kUnknownVersion);
}

TEST_CASE("facts round-trip on the wire, and anything else is refused")
{
	const Facts facts = AkiraMirror();
	std::vector<uint8_t> bytes;
	Encode(facts, &bytes);
	CHECK(bytes.size() == EncodedSize(facts));
	Facts back;
	REQUIRE(Decode(bytes.data(), bytes.size(), &back));
	CHECK(back == facts);

	std::vector<uint8_t> empty;
	Encode(Facts{}, &empty);
	REQUIRE(Decode(empty.data(), empty.size(), &back));
	CHECK(back.fighters.empty());

	std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
	CHECK_FALSE(Decode(cut.data(), cut.size(), &back));
	std::vector<uint8_t> tooMany = bytes;
	tooMany[0] = (uint8_t)(kMostFighters + 1);
	CHECK_FALSE(Decode(tooMany.data(), tooMany.size(), &back));
	std::vector<uint8_t> control = bytes;
	control[1] = 0x07; // a code byte that is not printable
	CHECK_FALSE(Decode(control.data(), control.size(), &back));
	CHECK_FALSE(Decode(nullptr, 0, &back));
}

TEST_CASE("the facts' text names each unit's fighter, version and data")
{
	Facts facts{{Unit("Z41", 0, 61, 0x1a2b), Unit("", 1, kUnknownVersion)}};
	CHECK(Text(facts) == "Z41 v61 data 0000000000001a2b, ? v? data unknown");
	CHECK(Text(Facts{}) == "no fighter read");
}
