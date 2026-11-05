// SFV's pick labels (the library's ui/SelectionLabels.hxx) and pictures
// (ui/SelectionArt.hxx), and the selection files its update packages may add
// (common/SelectionAssetPath.hxx): none, as SFV Ember installs no updates yet.
// The labels are plain: a stage by its game code from the catalog, a costume
// and a colour by the game's own numbers (as the launch setup and the logs
// give them). SFV has no Ultra, so its label is empty. No SFV picture exists
// yet, so every picture of the game reports itself missing and screens say so
// instead of waiting for one.

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
	const std::string code = sfve::adapter::StageCode(stageId);
	return code.empty() ? std::to_string(stageId) : code;
}

std::string CostumeLabel(const selection::Pick& pick, const selection::InstalledContent*)
{
	return std::string(loc::T("selection.costume")) + " " + std::to_string(pick.costume);
}

std::string ShortCostumeLabel(const selection::Pick& pick, const selection::InstalledContent* installed)
{
	return CostumeLabel(pick, installed);
}

std::string ShortAppearanceLabel(const selection::Pick& pick, const selection::InstalledContent* installed)
{
	return loc::Tf("home.selection_appearance", CostumeLabel(pick, installed), pick.color);
}

const char* UltraLabel(int) { return ""; }
const char* ShortUltraLabel(int) { return ""; }

namespace {
SelectionImage Missing()
{
	SelectionImage image;
	image.missing = true;
	return image;
}
} // namespace

SelectionImage SelectionArt::Portrait(int, bool) { return Missing(); }
SelectionImage SelectionArt::Character(int, int, int, bool) { return Missing(); }
SelectionImage SelectionArt::Appearance(int, int, int) { return Missing(); }
SelectionImage SelectionArt::Ultra(int, int) { return Missing(); }
SelectionImage SelectionArt::Stage(int) { return Missing(); }
SelectionImage SelectionArt::RankingEmblem(const std::string&) { return Missing(); }

} // namespace sf4e::ui
