// SFV's pick labels (the library's ui/SelectionLabels.hxx) and pictures
// (ui/SelectionArt.hxx), and the selection files its update packages may add
// (common/SelectionAssetPath.hxx): none, as SFV Ember installs no updates yet.
// The labels are the game's: a stage and a costume by the game's own names
// for them (their code or number where the game has none; SfvCatalog's
// StageName and CostumeName), a colour as the game's costume menu labels it
// ("01", "EX01"; ColourName). SFV has no Ultra, so its label is empty.
//
// The pictures are the game's own (SfvArt): a fighter's portrait is its
// select grid's face and a stage's its stage-select picture. The game has no
// picture of a fighter in a costume or colour (its select screens show the
// 3D model), no Ultra and no ranking emblems, so those report missing.

#include "ember/SfvArt.hxx"
#include "ember/SfvCatalog.hxx"

#include "common/Localization.hxx"
#include "common/SelectionAssetPath.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

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

const char* UltraLabel(int) { return ""; }
const char* ShortUltraLabel(int) { return ""; }
std::string UltraMove(int, int) { return {}; }

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
