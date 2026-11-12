// SFV's selection file paths (SfvSelectionAssetPath), checked without the
// game. Step 0 holds the path builders; stream B adds the rules' cases.

#include <doctest/doctest.h>

#include "ember/SfvSelectionAssetPath.hxx"

#include <string>

using namespace sfve::assets;
using sfve::picks::LoadoutPart;

TEST_CASE("the selection path builders give the package's own form")
{
	CHECK(ManifestPath() == L"assets\\selection\\manifest.json");
	CHECK(CutoutPath("RYU", 2, 13) == L"assets\\selection\\RYU\\costume-2\\color-13-cutout.png");
	CHECK(LoadoutPicturePath("RYU", LoadoutPart::VSkill, 0) == L"assets\\selection\\RYU\\vs1.jpg");
	CHECK(LoadoutPicturePath("RYU", LoadoutPart::VTrigger, 1) == L"assets\\selection\\RYU\\vt2.jpg");
	CHECK(CutoutPath("", 0, 0).empty());
	CHECK(CutoutPath("RYU", -1, 0).empty());
	CHECK(LoadoutPicturePath("RYU", LoadoutPart::VSkill, 2).empty());
}
