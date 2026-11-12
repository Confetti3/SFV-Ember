// SFV's selection file paths (SfvSelectionAssetPath), checked without the
// game: the builders, the rules over a pick space (a hand-made one for the
// shapes, the embedded one for every real look) and that the mod and the
// launcher judge a path alike.

#include <doctest/doctest.h>

#include "ember/SfvSelectionAssetPath.hxx"

#include "common/SelectionAssetPath.hxx"

#include <string>
#include <vector>

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

namespace {

// A small pick space with the shapes the rules care about: costume numbers
// with gaps (0, 2, 5), colours with gaps and an EX colour (0, 1, 100), a
// fighter with one V-Skill and two V-Triggers, and a second fighter.
sfve::picks::PickSpace SmallSpace()
{
	sfve::picks::PickSpace space;
	sfve::picks::Fighter abc;
	abc.code = "ABC";
	abc.costumes = {sfve::picks::Costume{0, {0, 1, 100}}, sfve::picks::Costume{2, {0, 5}}, sfve::picks::Costume{5, {7}}};
	abc.vSkills = 1;
	abc.vTriggers = 2;
	sfve::picks::Fighter xyz;
	xyz.code = "XYZ";
	xyz.costumes = {sfve::picks::Costume{0, {0}}};
	xyz.vSkills = 2;
	xyz.vTriggers = 1;
	space.fighters = {abc, xyz};
	return space;
}

bool Accepts(const std::wstring& path)
{
	return IsSfvSelectionAssetPath(path, SmallSpace());
}

} // namespace

TEST_CASE("the manifest and the existing looks of a fighter are selection asset paths")
{
	CHECK(Accepts(L"assets\\selection\\manifest.json"));
	CHECK(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.png"));
	CHECK(Accepts(L"assets\\selection\\ABC\\costume-0\\color-100-cutout.png")); // an EX colour
	CHECK(Accepts(L"assets\\selection\\ABC\\costume-2\\color-5-cutout.png"));   // numbers have gaps
	CHECK(Accepts(L"assets\\selection\\ABC\\costume-5\\color-7-cutout.png"));
	CHECK(Accepts(L"assets\\selection\\XYZ\\costume-0\\color-0-cutout.png"));
	// Exactly the builders' form.
	CHECK(Accepts(CutoutPath("ABC", 2, 5)));
	CHECK(Accepts(ManifestPath()));
}

TEST_CASE("a costume or colour the fighter lacks, or another fighter's, has no path")
{
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-1\\color-0-cutout.png")); // a gap
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-3\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-6\\color-7-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-2-cutout.png")); // costume 0 has no colour 2
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-2\\color-1-cutout.png")); // 1 is costume 0's
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-5\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-101-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\XYZ\\costume-2\\color-0-cutout.png")); // ABC's costume
	CHECK_FALSE(Accepts(L"assets\\selection\\QRS\\costume-0\\color-0-cutout.png")); // no such fighter
	CHECK_FALSE(Accepts(L"assets\\selection\\ABCD\\costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\AB\\costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\\\costume-0\\color-0-cutout.png"));
}

TEST_CASE("numbers are written as the builders write them")
{
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-00\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-02\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-00-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-01-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0100-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume--0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-+0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color--cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0x0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume- 0\\color-0-cutout.png"));
	// A number past any int is no number of the pick space.
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-99999999999999999999\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-4294967296\\color-0-cutout.png"));
}

TEST_CASE("only the loadout choices a fighter has are pictured")
{
	// ABC has one V-Skill and two V-Triggers.
	CHECK(Accepts(L"assets\\selection\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs2.jpg"));
	CHECK(Accepts(L"assets\\selection\\ABC\\vt1.jpg"));
	CHECK(Accepts(L"assets\\selection\\ABC\\vt2.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vt3.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs0.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs01.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vx1.jpg"));
	CHECK(Accepts(L"assets\\selection\\XYZ\\vs2.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\XYZ\\vt2.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\QRS\\vs1.jpg"));
	CHECK(Accepts(LoadoutPicturePath("ABC", LoadoutPart::VTrigger, 1)));
	CHECK_FALSE(Accepts(LoadoutPicturePath("ABC", LoadoutPart::VSkill, 1)));
}

TEST_CASE("no other name, extension, case, separator or trick is a selection asset path")
{
	// Extensions and suffixes.
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.PNG"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.png "));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.png.exe"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.png:stream"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0-cutout.png\\"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs1.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs1.JPG"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs1.jpg.exe"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\vs1.jpg:x"));
	CHECK_FALSE(Accepts(L"assets\\selection\\manifest.JSON"));
	CHECK_FALSE(Accepts(L"assets\\selection\\manifest.json."));
	CHECK_FALSE(Accepts(L"assets\\selection\\manifest.json:x"));
	CHECK_FALSE(Accepts(L"assets\\selection\\manifest.json\\"));
	CHECK_FALSE(Accepts(L"assets\\selection\\manifest.jsonx"));
	CHECK_FALSE(Accepts(L"assets\\selection\\README.md"));
	CHECK_FALSE(Accepts(L"assets\\selection\\sources.json")); // SF4's
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\portrait.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\color-0.jpg"));
	// Case: the code and the names are exact.
	CHECK_FALSE(Accepts(L"assets\\selection\\abc\\costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\Abc\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\Costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\Color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\Selection\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"Assets\\selection\\ABC\\vs1.jpg"));
	// Separators: the package's own backslash only.
	CHECK_FALSE(Accepts(L"assets/selection/manifest.json"));
	CHECK_FALSE(Accepts(L"assets/selection/ABC/costume-0/color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC/costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0/color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC/vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\\\selection\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"\\assets\\selection\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L".\\assets\\selection\\ABC\\vs1.jpg"));
	// Traversal and other roots.
	CHECK_FALSE(Accepts(L"assets\\selection\\..\\selection\\manifest.json"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\..\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\..\\costume-0\\color-0-cutout.png"));
	CHECK_FALSE(Accepts(L"assets\\selection\\..\\..\\Launcher.exe"));
	CHECK_FALSE(Accepts(L"assets\\selection\\.\\ABC\\vs1.jpg"));
	CHECK_FALSE(Accepts(L"..\\assets\\selection\\manifest.json"));
	CHECK_FALSE(Accepts(L"assets\\other\\manifest.json"));
	CHECK_FALSE(Accepts(L"C:\\assets\\selection\\manifest.json"));
	CHECK_FALSE(Accepts(L"manifest.json"));
	// Nothing, the root, a directory, an embedded NUL.
	CHECK_FALSE(Accepts(L""));
	CHECK_FALSE(Accepts(L"assets\\selection"));
	CHECK_FALSE(Accepts(L"assets\\selection\\"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0"));
	CHECK_FALSE(Accepts(L"assets\\selection\\ABC\\costume-0\\"));
	CHECK_FALSE(Accepts(std::wstring(L"assets\\selection\\ABC\\vs1.jpg") + std::wstring(1, L'\0') + L"x"));
	CHECK_FALSE(Accepts(ManifestPath() + std::wstring(1, L'\0')));
}

TEST_CASE("an empty pick space has only the manifest")
{
	const sfve::picks::PickSpace empty{};
	CHECK(IsSfvSelectionAssetPath(ManifestPath(), empty));
	CHECK_FALSE(IsSfvSelectionAssetPath(CutoutPath("RYU", 0, 0), empty));
	CHECK_FALSE(IsSfvSelectionAssetPath(LoadoutPicturePath("RYU", LoadoutPart::VSkill, 0), empty));
}

TEST_CASE("every look and loadout picture of the embedded pick space is a path, and only those")
{
	const sfve::picks::PickSpace& space = EmbeddedPickSpace();
	REQUIRE_FALSE(space.fighters.empty());
	size_t looks = 0;
	for (size_t at = 0; at < space.fighters.size(); at++) {
		const sfve::picks::Fighter& fighter = space.fighters[at];
		CAPTURE(fighter.code);
		for (const sfve::picks::Costume& costume : fighter.costumes) {
			CAPTURE(costume.id);
			for (int colour : costume.colours) {
				CAPTURE(colour);
				CHECK(IsSfvSelectionAssetPath(CutoutPath(fighter.code, costume.id, colour)));
				looks++;
			}
			// A neighbour that is not a colour of the costume is refused.
			for (int probe : {0, 1, 99, 100, 128, 129}) {
				CAPTURE(probe);
				CHECK(IsSfvSelectionAssetPath(CutoutPath(fighter.code, costume.id, probe)) ==
					sfve::picks::HasColour(costume, probe));
			}
		}
		// A costume number the fighter lacks is refused, whatever the colour.
		for (int costume = 0; costume < sfve::picks::CostumeCount(space, (int)at) + 3; costume++) {
			if (sfve::picks::CostumeAt(space, (int)at, costume)) continue;
			CAPTURE(costume);
			CHECK_FALSE(IsSfvSelectionAssetPath(CutoutPath(fighter.code, costume, 0)));
		}
		for (LoadoutPart part : sfve::picks::kLoadoutParts) {
			for (int choice = 0; choice < sfve::picks::kPartChoices; choice++) {
				CAPTURE(choice);
				CHECK(IsSfvSelectionAssetPath(LoadoutPicturePath(fighter.code, part, choice)) ==
					(choice < sfve::picks::PartChoices(fighter, part)));
			}
		}
	}
	CHECK(looks > 0);
	CHECK(IsSfvSelectionAssetPath(ManifestPath()));
	CHECK_FALSE(IsSfvSelectionAssetPath(CutoutPath(space.fighters[0].code, 1000, 0)));
	CHECK_FALSE(IsSfvSelectionAssetPath(CutoutPath("QQQ", 0, 0)));
}

TEST_CASE("the mod's, the launcher's and the updater's rules are this one function")
{
	// sf4e::selection::IsSelectionAssetPath is the library's hook: SfvSelection.cxx
	// (this test links it) and LauncherSelection.cxx (Launcher, Updater and
	// sfve_package_check) each define it as one call to IsSfvSelectionAssetPath,
	// so what the mod would load is what the updater unpacks and the package
	// check accepts.
	const sfve::picks::PickSpace& space = EmbeddedPickSpace();
	std::vector<std::wstring> paths = {ManifestPath(), L"assets\\selection\\README.md", L"assets\\selection\\..\\x",
		L"assets/selection/manifest.json", L"Launcher.exe", L"", CutoutPath("RYU", 0, 0), CutoutPath("RYU", 0, 9999),
		CutoutPath("QQQ", 0, 0), LoadoutPicturePath("RYU", LoadoutPart::VSkill, 1), L"assets\\selection\\RYU\\vs3.jpg"};
	for (const sfve::picks::Fighter& fighter : space.fighters) {
		paths.push_back(CutoutPath(fighter.code, fighter.costumes.front().id, fighter.costumes.front().colours.front()));
		paths.push_back(LoadoutPicturePath(fighter.code, LoadoutPart::VTrigger, 1));
	}
	for (const std::wstring& path : paths)
		CHECK(sf4e::selection::IsSelectionAssetPath(path) == IsSfvSelectionAssetPath(path, space));
	CHECK(sf4e::selection::IsSelectionAssetPath(CutoutPath("RYU", 0, 0)));
	CHECK_FALSE(sf4e::selection::IsSelectionAssetPath(L"assets\\selection\\README.md"));
}
