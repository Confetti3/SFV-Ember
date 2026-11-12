// SFV's pick labels (the library's ui/SelectionLabels.hxx) and pictures
// (ui/SelectionArt.hxx), and the selection files its update packages may add
// (common/SelectionAssetPath.hxx, over SfvSelectionAssetPath's rules).
// The labels are the game's: a stage and a costume by the game's own names
// for them (their code or number where the game has none; SfvCatalog's
// StageName and CostumeName; Random as the shared "Random"), a colour as the game's costume menu labels it
// ("01", "EX01"; ColourName). The Ultra number is SFV's loadout, a V-Skill
// and a V-Trigger, named in the game's words (SfvLoadoutText,
// EMBER_LIBRARY.md section 40).
//
// The pictures are the game's own (SfvArt): a fighter's portrait is the
// square of the head in its select grid's slanted face (SfvFaceCrop), and a
// stage's its card (SfvPictureSpec's StageCard), Random's the game's own
// Random stage picture. The game has no picture of a fighter in a costume
// or colour (its select screens show the 3D model), so those are the
// package's captured renders (beta.8: assets/selection/<CODE>/costume-<N>/
// color-<M>-cutout.png) and, for a V-Skill or V-Trigger, its captured
// loadout pictures (<CODE>/vs<k>.jpg, vt<k>.jpg), loaded as SF4's are
// (Request over PackagedFiles) and missing where the package has none. The
// pick pane falls back to the game's own large figure, then its banner.
// There are no ranking emblems of SF4's, and no one picture of a V-Skill and
// V-Trigger together (the pane draws their marks itself). Where a shared
// screen draws an Ultra's badge (the Arcade cabinet), SFV's is the
// loadout's short form, "II/I" (UltraBadge; EMBER_LIBRARY.md section 43).

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"
#include "ember/SfvSelectionAssetPath.hxx"
#include "ember/SfvSelection.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/SelectionAssetPath.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sf4e::selection {

bool IsSelectionAssetPath(const std::wstring& path)
{
	return sfve::assets::IsSfvSelectionAssetPath(path);
}

} // namespace sf4e::selection

namespace sf4e::ui {

std::string StageLabel(int stageId, const selection::InstalledContent*)
{
	if (stageId == sfve::picks::kRandomStage) return loc::T("selection.random");
	const std::string name = sfve::adapter::StageName(stageId);
	return name.empty() ? std::to_string(stageId) : name;
}

std::string CostumeLabel(const selection::Pick& pick, const selection::InstalledContent*)
{
	return sfve::adapter::CostumeName(pick.fighter, pick.costume);
}

std::string ShortCostumeLabel(const selection::Pick& pick, const selection::InstalledContent* installed)
{
	return CostumeLabel(pick, installed);
}

std::string ShortAppearanceLabel(const selection::Pick& pick, const selection::InstalledContent* installed)
{
	return loc::Tf("home.selection_appearance", CostumeLabel(pick, installed), sfve::adapter::ColourName(pick.color));
}

// The Ultra number is the loadout (picks::LoadoutAt): its label and its moves.
const char* UltraLabel(int ultra) { return sfve::adapter::LoadoutLabel(ultra); }
const char* ShortUltraLabel(int ultra) { return sfve::adapter::ShortLoadoutLabel(ultra); }
const char* UltraBadge(int ultra) { return sfve::adapter::ShortLoadoutLabel(ultra); }
std::string UltraMove(int fighter, int ultra)
{
	const std::optional<sfve::picks::Loadout> loadout = sfve::picks::LoadoutAt(ultra);
	return loadout && ember::ValidFighter(fighter) ? sfve::adapter::LoadoutMoves(fighter, *loadout) : std::string();
}

namespace {
SelectionImage Missing()
{
	SelectionImage image;
	image.missing = true;
	return image;
}
} // namespace

SelectionImage SelectionArt::Portrait(int fighter, bool) { return sfve::adapter::FighterImage(fighter, sfve::adapter::FighterPicture::Face); }
// The fighter's whole figure in a costume and colour: the look's captured
// cutout, then the costume's first colour, then the fighter's first costume in
// its first colour (the catalog's numbers and order), cropped to the figure.
// A look the catalog does not have is not asked for. Missing for a fighter
// the catalog does not have, or one with no costume; where the package has
// none of the files the pane falls back to the game's own figure and banner,
// so an absent file is not reported.
SelectionImage SelectionArt::Character(int fighter, int costume, int colour, bool large)
{
	const std::string code = sfve::adapter::FighterCode(fighter);
	if (code.empty()) return Missing();
	if (costume < 0 || colour < 0) costume = colour = -1;
	const auto files = [&] {
		std::vector<std::pair<int, int>> looks;
		const auto add = [&](int outfit, int palette) {
			const std::pair<int, int> look{outfit, palette};
			if (std::find(looks.begin(), looks.end(), look) == looks.end()) looks.push_back(look);
		};
		const std::vector<int> costumes = sfve::adapter::CostumeIds(fighter, false);
		const std::vector<int> own = sfve::adapter::ColourIds(fighter, costume);
		if (std::find(own.begin(), own.end(), colour) != own.end()) add(costume, colour);
		if (!own.empty()) add(costume, own.front());
		if (!costumes.empty()) {
			const std::vector<int> first = sfve::adapter::ColourIds(fighter, costumes.front());
			if (!first.empty()) add(costumes.front(), first.front());
		}
		std::vector<std::wstring> list;
		for (const auto& [outfit, palette] : looks) {
			const auto more = PackagedFiles(code + "/costume-" + std::to_string(outfit) + "/color-" + std::to_string(palette));
			list.insert(list.end(), more.begin(), more.end());
		}
		return list;
	};
	Policy figure;
	figure.cropToContent = true;
	const std::string& key = requestKey_.assign(code)
		.append("/costume-").append(std::to_string(costume))
		.append("/color-").append(std::to_string(colour))
		.append(large ? "/figure-large" : "/figure-thumb");
	return Request(key, files, large ? MaximumImageSide : ThumbnailSide, figure);
}
// The exact look's captured cutout and nothing else: a costume or colour the
// catalog does not have, or no capture of it, is missing, and the pane falls
// back to the fighter's figure (Character) and then the banner.
SelectionImage SelectionArt::Appearance(int fighter, int costume, int colour)
{
	const std::string code = sfve::adapter::FighterCode(fighter);
	if (code.empty()) return Missing();
	const std::vector<int> colours = sfve::adapter::ColourIds(fighter, costume);
	if (std::find(colours.begin(), colours.end(), colour) == colours.end()) return Missing();
	const std::string& key = imageKey_.assign(code)
		.append("/costume-").append(std::to_string(costume))
		.append("/color-").append(std::to_string(colour));
	return Request(key, [&] { return PackagedFiles(key); }, MaximumImageSide);
}
// A loadout picture (SfvSelection.hxx's LoadoutPicture): `ultra` is part * 2 +
// choice, 0 to 3, and the picture is <CODE>/vs<k>.jpg or vt<k>.jpg of the
// package (k is choice + 1). Missing for a number outside 0 to 3 and for a
// choice the fighter lacks.
SelectionImage SelectionArt::Ultra(int fighter, int ultra)
{
	const std::string code = sfve::adapter::FighterCode(fighter);
	if (code.empty() || ultra < 0 || ultra >= sfve::picks::kLoadoutChoices) return Missing();
	const sfve::picks::LoadoutPart part = (sfve::picks::LoadoutPart)(ultra / sfve::picks::kPartChoices);
	const int choice = ultra % sfve::picks::kPartChoices;
	if (choice >= sfve::adapter::PartChoices(fighter, part)) return Missing();
	const std::string& key =
		imageKey_.assign(code).append(part == sfve::picks::LoadoutPart::VSkill ? "/vs" : "/vt").append(std::to_string(choice + 1));
	return Request(key, [&] { return PackagedFiles(key); }, MaximumImageSide);
}
SelectionImage SelectionArt::UltraIcon(int) { return Missing(); }
SelectionImage SelectionArt::Stage(int stage) { return sfve::adapter::StageImage(stage); }
// SFV's league icons, for the ranks SfvProduct names (section 28).
SelectionImage SelectionArt::RankingEmblem(const std::string& entry) { return sfve::adapter::LeagueImage(entry); }

} // namespace sf4e::ui

namespace sfve::adapter {

sf4e::ui::SelectionImage LoadoutPicture(sf4e::ui::SelectionArt* art, int fighter, picks::LoadoutPart part, int choice)
{
	const int number = LoadoutPictureNumber(part, choice);
	if (!art || number < 0) {
		sf4e::ui::SelectionImage missing;
		missing.missing = true;
		return missing;
	}
	return art->Ultra(fighter, number);
}

} // namespace sfve::adapter
