// The loadout cards' notes (SfvLoadoutNotes), checked without the game. Step 0
// holds the key form and the embedded catalogs; stream D adds the per-fighter,
// per-language cases.

#include <doctest/doctest.h>

#include "ember/SfvLoadoutNotes.hxx"

#include <optional>
#include <string>

using namespace sfve::adapter;
using sfve::picks::LoadoutPart;

TEST_CASE("a loadout note's key names the fighter, the part and the choice")
{
	CHECK(LoadoutNoteKey("RYU", LoadoutPart::VSkill, 0) == "RYU.vs1");
	CHECK(LoadoutNoteKey("Z29", LoadoutPart::VTrigger, 1) == "Z29.vt2");
	CHECK(LoadoutNoteKey("", LoadoutPart::VSkill, 0).empty());
	CHECK(LoadoutNoteKey("RYU", LoadoutPart::VSkill, 2).empty());
}

TEST_CASE("the English loadout catalog is embedded")
{
	const std::optional<std::string> text = LoadoutCatalogText("en");
	REQUIRE(text.has_value());
	CHECK(text->find("RYU.vs1") != std::string::npos);
	CHECK(!LoadoutCatalogText("xx").has_value());
}
