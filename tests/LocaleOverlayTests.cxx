// SFV Ember's own words in every language (locales/<tag>.po, embedded as the
// product's locale overlays; EMBER_LIBRARY.md section 30): one catalog for
// each language the Ember library declares, each with English's msgids, its
// placeholders and line breaks, no word of SF4's, translated, and drawable by
// the fonts the interface ships.

#include <doctest/doctest.h>

#include "common/Localization.hxx"
#include "common/ProductIdentity.hxx"
#include "ui/Theme.hxx"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

// ImGui's own copy, private to this test, to read the fonts' character maps.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <imstb_truetype.h>

namespace {

namespace loc = sf4e::loc;

std::string ReadFile(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file.good());
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string LibraryPath(const char* relative)
{
	return std::string(SFVE_EMBER_SOURCE_ROOT) + "/" + relative;
}

// The product's catalogs by tag, parsed.
std::map<std::string, loc::Catalog> Overlays()
{
	std::map<std::string, loc::Catalog> result;
	for (const ember::LocaleOverlay* overlay = ember::Product().localeOverlays; overlay && overlay->tag; ++overlay) {
		loc::Catalog entries;
		std::string error;
		INFO(overlay->tag << ": " << error);
		REQUIRE(loc::testing::ParsePo(overlay->po, entries, error));
		entries.erase(""); // the header
		result[overlay->tag] = std::move(entries);
	}
	return result;
}

std::multiset<std::string> Placeholders(const std::string& text)
{
	static const std::regex placeholder(R"(\{[0-9]+(:[^}]*)?\})");
	std::multiset<std::string> found;
	for (auto it = std::sregex_iterator(text.begin(), text.end(), placeholder); it != std::sregex_iterator(); ++it) {
		found.insert(it->str());
	}
	return found;
}

std::vector<unsigned> Codepoints(const std::string& text)
{
	std::vector<unsigned> result;
	for (std::size_t i = 0; i < text.size();) {
		const unsigned char first = static_cast<unsigned char>(text[i++]);
		unsigned value = first;
		int more = 0;
		if ((first & 0xE0) == 0xC0) { value = first & 0x1F; more = 1; }
		else if ((first & 0xF0) == 0xE0) { value = first & 0x0F; more = 2; }
		else if ((first & 0xF8) == 0xF0) { value = first & 0x07; more = 3; }
		for (int n = 0; n < more && i < text.size(); ++n) value = (value << 6) | (static_cast<unsigned char>(text[i++]) & 0x3F);
		result.push_back(value);
	}
	return result;
}

class Font {
public:
	explicit Font(const std::string& path) : bytes_(ReadFile(path))
	{
		const auto* data = reinterpret_cast<const unsigned char*>(bytes_.data());
		REQUIRE(stbtt_InitFont(&info_, data, stbtt_GetFontOffsetForIndex(data, 0)));
	}
	bool Has(unsigned codepoint) const { return stbtt_FindGlyphIndex(&info_, static_cast<int>(codepoint)) != 0; }

private:
	std::string bytes_;
	stbtt_fontinfo info_{};
};

} // namespace

TEST_CASE("SFV Ember's words are in every language the library declares")
{
	const std::map<std::string, loc::Catalog> overlays = Overlays();
	const std::size_t count = static_cast<std::size_t>(loc::Locale::Count);
	CHECK(overlays.size() == count);
	REQUIRE(overlays.count("en") == 1);
	const loc::Catalog& english = overlays.at("en");
	CHECK(english.size() >= 50);
	for (std::size_t i = 0; i < count; ++i) {
		const std::string tag = loc::Tag(static_cast<loc::Locale>(i));
		INFO(tag);
		REQUIRE(overlays.count(tag) == 1);
		const loc::Catalog& catalog = overlays.at(tag);
		CHECK(catalog.size() == english.size());
		int translated = 0;
		for (const auto& [id, source] : english) {
			INFO(id);
			REQUIRE(catalog.count(id) == 1);
			const std::string& text = catalog.at(id);
			CHECK_FALSE(text.empty());
			CHECK(Placeholders(text) == Placeholders(source));
			CHECK(std::count(text.begin(), text.end(), '\n') == std::count(source.begin(), source.end(), '\n'));
			// sf4e is named once, as Ember's origin; SF4's own names never.
			std::string scrubbed = text;
			if (id == "about.ember_detail") scrubbed = std::regex_replace(scrubbed, std::regex("sf4e"), "");
			for (const char* sf4 : {"SF4", "sf4e", "SSFIV", "USF4", "Ultra Street Fighter IV", "Battle Log", "Sidecar"}) {
				CHECK(scrubbed.find(sf4) == std::string::npos);
			}
			translated += text != source;
		}
		// Names (SFV Ember, SFV) stay as they are; nearly everything else differs.
		if (tag != "en") CHECK(translated >= static_cast<int>(english.size()) - 6);
	}
}

TEST_CASE("each language takes SFV Ember's words over the shared ones")
{
	const std::map<std::string, loc::Catalog> overlays = Overlays();
	std::string error;
	REQUIRE(loc::testing::LayerOverlays(ember::Product().localeOverlays, error));
	for (std::size_t i = 0; i < static_cast<std::size_t>(loc::Locale::Count); ++i) {
		const loc::Locale locale = static_cast<loc::Locale>(i);
		INFO(loc::Tag(locale));
		REQUIRE(overlays.count(loc::Tag(locale)) == 1);
		const loc::Catalog& catalog = overlays.at(loc::Tag(locale));
		loc::SetActive(locale);
		for (const char* id : {"app.name", "launcher.game_not_found", "selection.not_owned", "home.training_detail"}) {
			CHECK(std::string(loc::T(id)) == catalog.at(id));
		}
	}
	loc::SetActive(loc::Locale::En);
}

TEST_CASE("the interface's fonts draw every character of SFV Ember's words")
{
	// Latin and Cyrillic: Inter, both weights, in the ranges the atlas bakes.
	// The CJK scripts: the font locales.json names for the locale's script.
	const Font body(LibraryPath("src/ui/fonts/Inter-Regular.ttf"));
	const Font heading(LibraryPath("src/ui/fonts/Inter-SemiBold.ttf"));
	const nlohmann::json registry = nlohmann::json::parse(ReadFile(LibraryPath("locales/locales.json")));
	std::map<std::string, std::string> fontOfScript;
	for (const auto& script : registry.at("scripts")) {
		if (script.at("font").is_string()) fontOfScript[script.at("id").get<std::string>()] = script.at("font").get<std::string>();
	}
	const std::map<std::string, loc::Catalog> overlays = Overlays();
	for (const auto& locale : registry.at("locales")) {
		const std::string tag = locale.at("tag").get<std::string>();
		const std::string script = locale.at("script").get<std::string>();
		INFO(tag);
		std::unique_ptr<Font> scriptFont;
		if (fontOfScript.count(script)) scriptFont = std::make_unique<Font>(LibraryPath(("src/ui/fonts/" + fontOfScript[script]).c_str()));
		REQUIRE(overlays.count(tag) == 1);
		for (const auto& [id, text] : overlays.at(tag)) {
			for (const unsigned codepoint : Codepoints(text)) {
				if (codepoint < 0x20) continue;
				bool baked = false;
				for (const ImWchar* range = sf4e::ui::UiGlyphRanges; range[0] && range[1]; range += 2) {
					baked = baked || (codepoint >= range[0] && codepoint <= range[1]);
				}
				const bool drawn = (baked && body.Has(codepoint) && heading.Has(codepoint)) || (scriptFont && scriptFont->Has(codepoint));
				INFO(id << " U+" << std::hex << codepoint);
				CHECK(drawn);
			}
		}
	}
}
