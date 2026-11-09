#pragma once

// Which of SFV's own pictures Ember draws, and how each is cut for its frame:
// every asset path SfvArt loads is built here, and nothing else. No game
// code and no ImGui, so the tests check every path and crop. SfvArt loads a
// Spec and makes its view; the pick screen's pane (SfvPickPreview) draws it.
// EMBER_LIBRARY.md sections 15, 37 and 43.
//
// A path is the game's object path, "/Game/<folder>/<name>.<name>". An empty
// code gives an empty Spec, which SfvArt reports as missing without asking
// the game.
//
// Two choices wait for the laptop's probe (the `art` pipe command,
// SfvArtProbe) and are data here, one line each: which picture is a stage's
// card (kStageCard), and whether a costume shows the shop's picture of it
// (kCostumePicture).

#include "ember/SfvPickSpace.hxx"

#include <string>
#include <string_view>

namespace sfve::pictures {

// How a picture is cut for the frame it fills.
enum class Framing {
	Whole,      // as it is
	FaceSquare, // the square of the head in a select-grid face (SfvFaceCrop), found from its alpha
	Centre,     // the largest centred rect of the Spec's aspect (CentreCrop)
};

struct Spec {
	std::wstring path; // empty: no picture, reported missing
	Framing framing = Framing::Whole;
	float aspect = 0; // Centre's width over height
};

// A stage's card on the pick screen: the stage select's large card
// (StageSelectIcon_l), drawn whole, or the stage select's 2048x512
// background (StageSelectBG) cut at its centre to the banner's shape. The
// probe says whether StageSelectIcon_l is a landscape card that loads whole;
// until it does, the background, which is known to load, is the card.
enum class StageCardPicture { LargeIcon, BackgroundCentre };
inline constexpr StageCardPicture kStageCard = StageCardPicture::BackgroundCentre;

// What the pane shows for a costume: the fighter's banner, or the shop's
// picture of the costume where the shop sells it (420x190), with the banner
// where it has none. Shop pictures wait for the probe to show that
// shp_<CODE>_<NN>_00 is costume NN and carries no price or baked text.
enum class CostumePicture { Banner, ShopWhereSold };
inline constexpr CostumePicture kCostumePicture = CostumePicture::Banner;

// The Random stage's code: the game has a stage folder for it, with the same
// five UI pictures every stage has. The catalog's StageCode stays empty for
// Random; SfvArt's StageImage is the one place Random gets this code.
inline constexpr std::string_view kRandomStageCode = "RND";

// The fighter banner's shape (CharaSelectIcon_l, 380x258), which a stage's
// cut card keeps, and the shop costume picture's (420x190).
inline constexpr float kBannerAspect = 380.f / 258.f;
inline constexpr float kShopCostumeAspect = 420.f / 190.f;

// "/Game/<folder>/<name>.<name>"; empty for an empty folder or name.
std::wstring ObjectPath(std::wstring_view folder, std::wstring_view name);

// /Game/Chara/<code>/Hud/CharaSelectIconLeft, its face square.
Spec FighterFace(std::string_view code);
// /Game/Chara/<code>/Hud/CharaSelectIcon_l, whole.
Spec FighterBanner(std::string_view code);
// /Game/Stage/<code>/UI/<card>, by kStageCard.
Spec StageCard(std::string_view code);
// The game's generic V-Skill and V-Trigger marks, I and II, from the
// Fighter Profile's replay log: /Game/WinSysAsset/FighterProfileReplayLog/
// img_Vskill_1 and _2, img_Vtrigger_1 and _2. Empty for a choice outside 0
// and 1.
Spec LoadoutMark(picks::LoadoutPart part, int choice);
// The shop's picture of a costume, /Game/WinSysAsset/Shop/ShopItemImage/
// Costume/shp_<code>_<NN>_00 with the number in two decimal digits. Empty for
// an empty code or a number outside 0-99.
Spec ShopCostume(std::string_view code, int costume);
// A league's icon, /Game/CommonAsset/LeagueIcon/<name>. SfvArt asks only for
// the names SFV Ember's ranks use.
Spec LeagueIcon(std::string_view name);

// A rectangle of a picture's pixels.
struct PixelRect {
	int x = 0, y = 0, width = 0, height = 0;
	bool operator==(const PixelRect&) const = default;
};
// The largest rect of `aspect` (width over height) centred in a picture of
// `width` by `height`: the full height of a wider picture, the full width of
// a narrower one. The whole picture for a size or aspect that is not
// positive.
PixelRect CentreCrop(int width, int height, float aspect);

} // namespace sfve::pictures
