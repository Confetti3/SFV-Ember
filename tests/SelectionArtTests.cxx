// SFV's pick labels and pictures (SfvSelection.cxx, SfvArt), checked without
// the game: the labels are the catalog's names, and the pictures wait for the
// game's textures or report missing.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"

#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <memory>
#include <string>

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
	CHECK(CostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(CostumeLabel(PickOf(0, 1)) == "1"); // the game gave no name
	CHECK(ShortCostumeLabel(PickOf(0, 4)) == "Nostalgia");
	CHECK(ShortAppearanceLabel(PickOf(0, 4, 100)) == "Nostalgia, color EX01");
	CHECK(ColourName(0) == "01");
	CHECK(ColourName(9) == "10");
	CHECK(ColourName(100) == "EX01");
	CHECK(ColourName(127) == "EX28");
	CHECK(std::string(UltraLabel(0)).empty());
	CHECK(std::string(ShortUltraLabel(0)).empty());
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
	CHECK(art.Stage(3).missing);
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
	CHECK_FALSE(Swatch(0, 4, 100).has_value()); // read by the game thread
}
