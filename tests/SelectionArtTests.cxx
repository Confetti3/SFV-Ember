// SFV's pick labels and pictures (SfvSelection.cxx, SfvArt), checked without
// the game: the labels are the catalog's names, and the pictures wait for the
// game's textures or report missing.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvPictureSpec.hxx"

#include "common/Localization.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace sfve::adapter;
using namespace sfve::catalog_fixture;

TEST_CASE("the pick labels are the game's names, from the catalog")
{
	FillTestCatalog();
	using namespace sf4e::ui;
	CHECK(StageLabel(0) == "Stage One");
	CHECK(StageLabel(1) == "S02"); // the game gave no name
	CHECK(StageLabel(2) == "Stage Twenty-five");
	CHECK(StageLabel(9) == "9");
	CHECK(StageLabel(sfve::picks::kRandomStage) == sf4e::loc::T("selection.random"));
	CHECK(CostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(CostumeLabel(PickOf(0, 1)) == "1"); // the game gave no name
	CHECK(ShortCostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(ShortAppearanceLabel(PickOf(0, 4, 100)) == "Nostalgia, color EX01");
	CHECK(ColourName(0) == "01");
	CHECK(ColourName(9) == "10");
	CHECK(ColourName(100) == "EX01");
	CHECK(ColourName(127) == "EX28");
	// The Ultra number is the loadout (section 40), named in the game's words
	// once it gives them, in the game's English until then (no game runs in a test).
	CHECK(std::string(UltraLabel(0)) == "V-Skill I \xC2\xB7 V-Trigger I");
	CHECK(std::string(UltraLabel(2)) == "V-Skill II \xC2\xB7 V-Trigger I");
	CHECK(std::string(ShortUltraLabel(1)) == "I/II");
	CHECK(std::string(UltraLabel(4)).empty());
	CHECK(std::string(ShortUltraLabel(-1)).empty());
	CHECK(UltraMove(0, 3) == "Skill Two \xC2\xB7 Trigger Two");
	CHECK(UltraMove(4, 1) == "V-Skill I \xC2\xB7 V-Trigger II"); // BSN's are unnamed
	CHECK(UltraMove(0, 4).empty());
	CHECK(UltraMove(5, 0).empty()); // not a fighter of the catalog
}

TEST_CASE("the game's pictures are its fighters' faces and its stages', and the rest report missing")
{
	FillTestCatalog();
	struct NoTextures final : sf4e::ui::TextureUploader {
		ImTextureID Create(unsigned, unsigned, const unsigned char*, std::string& failure) override
		{
			failure = "no renderer in a test";
			return 0;
		}
		void Release(ImTextureID) override {}
	};
	sf4e::ui::SelectionArt art(std::make_unique<NoTextures>(), L"", L"");
	// Asked for, a fighter's face and a stage's picture wait for the game,
	// which loads them at its main menu (none runs in a test).
	CHECK_FALSE(art.Portrait(0).missing);
	CHECK(art.Portrait(0).texture == 0);
	CHECK_FALSE(art.Stage(2).missing);
	CHECK(art.Portrait(5).missing); // not a fighter of the catalog
	CHECK_FALSE(art.Stage(3).missing); // the universe's, whether or not this copy has it
	CHECK(art.Stage(4).missing);
	// Random has the game's own Random stage picture (section 43).
	CHECK_FALSE(art.Stage(sfve::picks::kRandomStage).missing);
	CHECK(art.Stage(sfve::picks::kRandomStage).texture == 0);
	// The game has no picture of a fighter in a costume or colour.
	CHECK(art.Character(0, 0, 0).missing);
	CHECK(art.Appearance(0, 0, 0).missing);
	CHECK(art.Ultra(0, 0).missing);
	// A class is one of SFV's league icons, which also waits for the game; a
	// name that is not one of SFV Ember's ranks (USF4's own emblems among
	// them) loads nothing and reports missing.
	CHECK_FALSE(art.RankingEmblem("LI_gold").missing);
	CHECK(art.RankingEmblem("LI_gold").texture == 0);
	for (const char* other : {"class_B.dds", "LI_dummy", "LI_super_gold", "", "LI_gold.LI_gold", "../LI_gold"})
		CHECK(art.RankingEmblem(other).missing);
	// A costume's palette: its colours at once, each with whether this player
	// can ready it (RYU's EX colours of costume 4 are not theirs), and the
	// swatches once the game thread has read them (none runs in a test).
	const CostumePalette palette = CostumeSwatches(0, 4);
	CHECK_FALSE(palette.read);
	CHECK(palette.colours == std::vector<StripColour>{{0, true, std::nullopt}, {1, true, std::nullopt},
		{100, false, std::nullopt}, {101, false, std::nullopt}});
	CHECK(CostumeSwatches(5, 0).colours.empty()); // not a fighter of the catalog
	// The pick screen pane's pictures wait for the game too, and a costume
	// picture is missing while the shop's are not shown.
	CHECK_FALSE(LoadoutImage(sfve::picks::LoadoutPart::VSkill, 1).missing);
	CHECK(LoadoutImage(sfve::picks::LoadoutPart::VTrigger, 2).missing); // no third choice
	if constexpr (sfve::pictures::kCostumePicture == sfve::pictures::CostumePicture::Banner) CHECK(CostumeImage(0, 4).missing);
	// The Arcade cabinet's badge is the loadout's short form.
	CHECK(std::string(sf4e::ui::UltraBadge(2)) == "II/I");
	CHECK(std::string(sf4e::ui::UltraBadge(4)).empty());
}
