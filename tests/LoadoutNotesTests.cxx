// The loadout cards' notes (SfvLoadoutNotes), checked without the game: every
// fighter's V-Skills and V-Triggers have an English note, the catalogs hold no
// other keys, every note fits the card, every translation says something of
// its own, and a missing key falls back to English.

#include <doctest/doctest.h>

#include "common/Localization.hxx"
#include "ember/SfvLoadoutNotes.hxx"
#include "ember/SfvPickSpace.hxx"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace sfve::adapter;
using sfve::picks::LoadoutPart;
using sf4e::loc::Catalog;

namespace {

// The pick space the room host hosts (data/pick-space.json, SFVE_PICK_SPACE_PATH).
sfve::picks::PickSpace LoadPickSpace()
{
	std::ifstream file(SFVE_PICK_SPACE_PATH, std::ios::binary);
	REQUIRE(file);
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	sfve::picks::PickSpace space;
	std::string build, error;
	REQUIRE(sfve::picks::FromJson(text, space, build, error));
	return space;
}

// One V-Skill or V-Trigger choice of a fighter, with the catalog key it has.
struct Slot {
	std::string code;
	LoadoutPart part;
	int choice;
	std::string key;
};

// Every slot the pick space has: each fighter's V-Skills and V-Triggers, by
// the counts the pick space gives.
std::vector<Slot> SlotsOf(const sfve::picks::PickSpace& space)
{
	std::vector<Slot> slots;
	for (const sfve::picks::Fighter& fighter : space.fighters) {
		for (const LoadoutPart part : sfve::picks::kLoadoutParts) {
			for (int choice = 0; choice < sfve::picks::PartChoices(fighter, part); ++choice) {
				slots.push_back({fighter.code, part, choice, LoadoutNoteKey(fighter.code, part, choice)});
			}
		}
	}
	return slots;
}

// The keys every catalog should hold, from the pick space.
std::set<std::string> KeysOf(const std::vector<Slot>& slots)
{
	std::set<std::string> keys;
	for (const Slot& slot : slots) keys.insert(slot.key);
	return keys;
}

// The parsed embedded catalog for a tag; fails the test when it does not parse.
Catalog ParseEmbedded(std::string_view tag)
{
	const std::optional<std::string> text = LoadoutCatalogText(tag);
	REQUIRE(text.has_value());
	Catalog entries;
	std::string error;
	INFO(tag);
	REQUIRE(sf4e::loc::testing::ParsePo(*text, entries, error));
	return entries;
}

// The language tags the embedded catalogs cover: one per .po file in the
// locales/loadout folder the build embeds.
std::vector<std::string> LoadoutTags()
{
	std::vector<std::string> tags;
	for (const auto& entry : std::filesystem::directory_iterator(SFVE_LOADOUT_LOCALES_DIR)) {
		if (entry.path().extension().string() == ".po") tags.push_back(entry.path().stem().string());
	}
	return tags;
}

// UTF-8 code points of a string: every byte that is not a continuation byte.
std::size_t CodePoints(std::string_view text)
{
	std::size_t count = 0;
	for (const char byte : text) {
		if (((unsigned char)byte & 0xC0) != 0x80) ++count;
	}
	return count;
}

// A catalog that still holds only the step 0 placeholders (translations not
// yet written); its notes all start with "Stub note".
bool IsStub(const Catalog& catalog)
{
	for (const auto& entry : catalog) {
		if (entry.second.rfind("Stub note", 0) != 0) return false;
	}
	return true;
}

// Sets the active language for one scope and puts the previous one back.
class ActiveLanguage {
public:
	explicit ActiveLanguage(const std::string& tag) : previous_(sf4e::loc::Active())
	{
		sf4e::loc::SetActive(sf4e::loc::ResolveLocale(tag, "", {}));
	}
	~ActiveLanguage() { sf4e::loc::SetActive(previous_); }
	ActiveLanguage(const ActiveLanguage&) = delete;
	ActiveLanguage& operator=(const ActiveLanguage&) = delete;

private:
	sf4e::loc::Locale previous_;
};

} // namespace

TEST_CASE("every V-Skill and V-Trigger of every fighter has an English note")
{
	const sfve::picks::PickSpace space = LoadPickSpace();
	const Catalog english = ParseEmbedded("en");
	for (const Slot& slot : SlotsOf(space)) {
		INFO(slot.key);
		REQUIRE(english.count(slot.key) == 1);
		CHECK(!english.at(slot.key).empty());
	}
}

TEST_CASE("English has no keys beyond the pick space's V-Skills and V-Triggers")
{
	const sfve::picks::PickSpace space = LoadPickSpace();
	const std::vector<Slot> slots = SlotsOf(space);
	const Catalog english = ParseEmbedded("en");
	const std::set<std::string> expected = KeysOf(slots);
	CHECK(english.size() == expected.size());
	CHECK(slots.size() == expected.size());
	for (const auto& entry : english) {
		INFO(entry.first);
		CHECK(expected.count(entry.first) == 1);
	}
}

TEST_CASE("every note is at most 140 characters in English and 160 in a translation")
{
	for (const std::string& tag : LoadoutTags()) {
		const Catalog catalog = ParseEmbedded(tag);
		for (const auto& [key, text] : catalog) {
			INFO(tag << " " << key);
			CHECK(CodePoints(text) <= (std::size_t)(tag == "en" ? kMaxNoteLength : kMaxTranslatedNoteLength));
		}
	}
}

TEST_CASE("every translation says something of its own")
{
	const sfve::picks::PickSpace space = LoadPickSpace();
	const std::set<std::string> expected = KeysOf(SlotsOf(space));
	const Catalog english = ParseEmbedded("en");
	for (const std::string& tag : LoadoutTags()) {
		if (tag == "en") continue;
		const Catalog catalog = ParseEmbedded(tag);
		if (catalog.empty() || IsStub(catalog)) {
			std::cout << "LoadoutNotesTests: skipped " << tag << " (stub catalog, no translations yet)\n";
			continue;
		}
		INFO(tag);
		for (const std::string& key : expected) {
			INFO(key);
			REQUIRE(catalog.count(key) == 1);
			CHECK(!catalog.at(key).empty());
			CHECK(catalog.at(key) != english.at(key));
		}
	}
}

TEST_CASE("a missing key falls back to English")
{
	const sfve::picks::PickSpace space = LoadPickSpace();
	const std::vector<Slot> slots = SlotsOf(space);
	const Catalog english = ParseEmbedded("en");
	for (const std::string& tag : LoadoutTags()) {
		const Catalog catalog = ParseEmbedded(tag);
		const ActiveLanguage active(tag);
		INFO(tag);
		for (const Slot& slot : slots) {
			INFO(slot.key);
			const auto entry = catalog.find(slot.key);
			const std::string expected = entry != catalog.end() && !entry->second.empty() ? entry->second : english.at(slot.key);
			CHECK(LoadoutNoteFor(slot.code, slot.part, slot.choice) == expected);
		}
	}
	// A fighter the pick space does not have has no note in any language.
	const ActiveLanguage active("en");
	CHECK(LoadoutNoteFor("NOPE", LoadoutPart::VSkill, 0).empty());
}

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
