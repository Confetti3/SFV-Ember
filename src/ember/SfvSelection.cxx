// SFV's pick labels (the library's ui/SelectionLabels.hxx) and pictures
// (ui/SelectionArt.hxx), and the selection files its update packages may add
// (common/SelectionAssetPath.hxx): none, as SFV Ember installs no updates yet.
// The labels are the game's: a stage and a costume by the game's own names
// for them (their code or number where the game has none; SfvCatalog's
// StageName and CostumeName; Random as the shared "Random"), a colour as the game's costume menu labels it
// ("01", "EX01"; ColourName). The Ultra number is SFV's loadout, a V-Skill
// and a V-Trigger, named in the game's words (SfvLoadoutText,
// EMBER_LIBRARY.md section 40).
//
// The pictures are the game's own (SfvArt): a fighter's portrait is the
// square of the head in its select grid's slanted face (SfvFaceCrop), and a
// stage's its stage-select picture. The game has no
// picture of a fighter in a costume or colour (its select screens show the
// 3D model), none Ember draws of a V-Skill or V-Trigger, and no ranking
// emblems, so those report missing.

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvLoadoutText.hxx"

#include "common/GameCatalog.hxx"
#include "common/Localization.hxx"
#include "common/SelectionAssetPath.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <optional>
#include <string>

namespace sf4e::selection {

bool IsSelectionAssetPath(const std::wstring&)
{
	return false;
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
SelectionImage SelectionArt::Character(int, int, int, bool) { return Missing(); }
SelectionImage SelectionArt::Appearance(int, int, int) { return Missing(); }
SelectionImage SelectionArt::Ultra(int, int) { return Missing(); }
SelectionImage SelectionArt::UltraIcon(int) { return Missing(); }
SelectionImage SelectionArt::Stage(int stage) { return sfve::adapter::StageImage(stage); }
// SFV's league icons, for the ranks SfvProduct names (section 28).
SelectionImage SelectionArt::RankingEmblem(const std::string& entry) { return sfve::adapter::LeagueImage(entry); }

} // namespace sf4e::ui
