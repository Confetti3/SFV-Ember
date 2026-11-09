// SFV's picture paths and crops (SfvPictureSpec), checked without the game:
// every path is the game's object path, an empty code asks for nothing, and
// a centre crop keeps the frame's shape.

#include <doctest/doctest.h>

#include "ember/SfvPictureSpec.hxx"

#include <string>

using namespace sfve::pictures;
using sfve::picks::LoadoutPart;

TEST_CASE("a fighter's pictures are its HUD's, by the game's object path")
{
	const Spec face = FighterFace("RYU");
	CHECK(face.path == L"/Game/Chara/RYU/Hud/CharaSelectIconLeft.CharaSelectIconLeft");
	CHECK(face.framing == Framing::FaceSquare);
	const Spec banner = FighterBanner("RYU");
	CHECK(banner.path == L"/Game/Chara/RYU/Hud/CharaSelectIcon_l.CharaSelectIcon_l");
	CHECK(banner.framing == Framing::Whole);
	// No code, no picture: nothing is asked of the game.
	CHECK(FighterFace("").path.empty());
	CHECK(FighterBanner("").path.empty());
	CHECK(ObjectPath(L"", L"X").empty());
	CHECK(ObjectPath(L"Chara", L"").empty());
}

TEST_CASE("a stage's card is the picture kStageCard names, and Random has its own")
{
	const Spec s01 = StageCard("S01");
	const Spec random = StageCard(kRandomStageCode);
	if constexpr (kStageCard == StageCardPicture::BackgroundCentre) {
		CHECK(s01.path == L"/Game/Stage/S01/UI/StageSelectBG.StageSelectBG");
		CHECK(s01.framing == Framing::Centre);
		CHECK(s01.aspect == doctest::Approx(380.0 / 258.0));
		CHECK(random.path == L"/Game/Stage/RND/UI/StageSelectBG.StageSelectBG");
	} else {
		CHECK(s01.path == L"/Game/Stage/S01/UI/StageSelectIcon_l.StageSelectIcon_l");
		CHECK(s01.framing == Framing::Whole);
		CHECK(random.path == L"/Game/Stage/RND/UI/StageSelectIcon_l.StageSelectIcon_l");
	}
	CHECK(random.framing == s01.framing);
	CHECK(StageCard("").path.empty());
}

TEST_CASE("the V-Skill and V-Trigger marks are the replay log's I and II")
{
	CHECK(LoadoutMark(LoadoutPart::VSkill, 0).path == L"/Game/WinSysAsset/FighterProfileReplayLog/img_Vskill_1.img_Vskill_1");
	CHECK(LoadoutMark(LoadoutPart::VSkill, 1).path == L"/Game/WinSysAsset/FighterProfileReplayLog/img_Vskill_2.img_Vskill_2");
	CHECK(LoadoutMark(LoadoutPart::VTrigger, 0).path ==
		L"/Game/WinSysAsset/FighterProfileReplayLog/img_Vtrigger_1.img_Vtrigger_1");
	CHECK(LoadoutMark(LoadoutPart::VTrigger, 1).path ==
		L"/Game/WinSysAsset/FighterProfileReplayLog/img_Vtrigger_2.img_Vtrigger_2");
	CHECK(LoadoutMark(LoadoutPart::VSkill, 0).framing == Framing::Whole);
	// No third choice, and no mark for none.
	CHECK(LoadoutMark(LoadoutPart::VSkill, 2).path.empty());
	CHECK(LoadoutMark(LoadoutPart::VTrigger, -1).path.empty());
}

TEST_CASE("a shop costume picture numbers the costume in two decimal digits")
{
	CHECK(ShopCostume("RYU", 7).path == L"/Game/WinSysAsset/Shop/ShopItemImage/Costume/shp_RYU_07_00.shp_RYU_07_00");
	CHECK(ShopCostume("RYU", 16).path == L"/Game/WinSysAsset/Shop/ShopItemImage/Costume/shp_RYU_16_00.shp_RYU_16_00");
	CHECK(ShopCostume("CNL", 0).path == L"/Game/WinSysAsset/Shop/ShopItemImage/Costume/shp_CNL_00_00.shp_CNL_00_00");
	CHECK(ShopCostume("RYU", -1).path.empty());
	CHECK(ShopCostume("RYU", 100).path.empty());
	CHECK(ShopCostume("", 3).path.empty());
}

TEST_CASE("a league's icon is in the game's league folder")
{
	CHECK(LeagueIcon("LI_gold").path == L"/Game/CommonAsset/LeagueIcon/LI_gold.LI_gold");
	CHECK(LeagueIcon("").path.empty());
}

TEST_CASE("a centre crop is the largest rect of the frame's shape at the picture's middle")
{
	// The stage background, 2048x512, to the banner's 380x258: 754x512 at its centre.
	const PixelRect stage = CentreCrop(2048, 512, 380.f / 258.f);
	CHECK(stage == PixelRect{647, 0, 754, 512});
	CHECK((float)stage.x / 2048 == doctest::Approx(0.3159).epsilon(0.001));
	CHECK((float)(stage.x + stage.width) / 2048 == doctest::Approx(0.6841).epsilon(0.001));
	// A picture narrower than the shape is cut top and bottom.
	CHECK(CentreCrop(380, 400, 380.f / 258.f) == PixelRect{0, 71, 380, 258});
	// Already the shape: the whole picture.
	CHECK(CentreCrop(380, 258, 380.f / 258.f) == PixelRect{0, 0, 380, 258});
	// Nothing to cut by: the whole picture.
	CHECK(CentreCrop(0, 512, 1.5f) == PixelRect{0, 0, 0, 512});
	CHECK(CentreCrop(2048, 0, 1.5f) == PixelRect{0, 0, 2048, 0});
	CHECK(CentreCrop(2048, 512, 0) == PixelRect{0, 0, 2048, 512});
	CHECK(CentreCrop(2048, 512, -2) == PixelRect{0, 0, 2048, 512});
}
