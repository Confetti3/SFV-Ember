// What Launcher.exe and Updater.exe link in place of the mod's pictures and
// selection files (src/ember/SfvSelection.cxx), which read the running
// game's textures and lists: outside the game there are none. The recovery
// window draws Ember's own art (its ground and button prompts) and no
// fighter. The selection files an update package may carry are the mod's
// rules (SfvSelectionAssetPath), the same library every program links.

#include "ember/SfvSelectionAssetPath.hxx"

#include "common/SelectionAssetPath.hxx"
#include "ui/SelectionArt.hxx"
#include "ui/SelectionLabels.hxx"

#include <string>

namespace sf4e::selection {

bool IsSelectionAssetPath(const std::wstring& path)
{
	return sfve::assets::IsSfvSelectionAssetPath(path);
}

} // namespace sf4e::selection

namespace sf4e::ui {

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
SelectionImage SelectionArt::UltraIcon(int) { return Missing(); }
SelectionImage SelectionArt::Stage(int) { return Missing(); }
SelectionImage SelectionArt::RankingEmblem(const std::string&) { return Missing(); }

// GameMenu's Ultra mark asks the game for its badge; the launcher draws no
// pick, so it has none.
const char* UltraBadge(int) { return ""; }

} // namespace sf4e::ui
