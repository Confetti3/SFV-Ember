// What Launcher.exe and Updater.exe link in place of the mod's pictures and
// selection files (src/ember/SfvSelection.cxx), which read the running
// game's textures and lists: outside the game there are none. The recovery
// window draws Ember's own art (its ground and button prompts) and no
// fighter; an update package may carry no selection files, as the mod's
// definition says too.

#include "common/SelectionAssetPath.hxx"
#include "ui/SelectionArt.hxx"

#include <string>

namespace sf4e::selection {

bool IsSelectionAssetPath(const std::wstring&)
{
	return false;
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

} // namespace sf4e::ui
