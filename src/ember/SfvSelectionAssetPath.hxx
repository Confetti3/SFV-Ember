#pragma once

// The selection files an SFV Ember package may carry, and where each one lives
// (beta.8 pack, docs/design/beta8/interfaces.md): the captured renders of
// every fighter, costume and colour, and the loadout pictures, under
// `assets\selection\` in the package folder. One set of rules for every
// program that reads or judges a path: the mod's pictures (SfvSelection), the
// launcher, the updater (through the library's IsSelectionAssetPath) and
// sfve_package_check all link the library these live in (CMakeLists.txt,
// sfve_selection_assets), so they cannot disagree.
//
// The rules are over the pick space the game exported (data/pick-space.json,
// embedded in that library as bytes): a costume or colour the game does not
// have has no path. Paths are the package's own form: relative to the package
// folder, backslash separated, the fighter's code in capitals.
//
//   assets\selection\manifest.json
//   assets\selection\<CODE>\costume-<N>\color-<M>-cutout.png   (costume N has colour M)
//   assets\selection\<CODE>\vs<k>.jpg                          (V-Skill choice k, 1 or 2)
//   assets\selection\<CODE>\vt<k>.jpg                          (V-Trigger choice k, 1 or 2)
//
// Nothing else is a selection asset path. N and M are the game's own numbers
// (a costume's `id`, a colour's number: SFV's have gaps), written in decimal
// with no leading zeros, as picks::Costume and picks::Fighter hold them.
//
// No game code, no ImGui. EMBER_LIBRARY.md section 15.
//
// Stream B fills SfvSelectionAssetPath.cxx. The builders and EmbeddedPickSpace
// are final; IsSfvSelectionAssetPath is a stub that accepts nothing until then.

#include "ember/SfvPickSpace.hxx"

#include <string>
#include <string_view>

namespace sfve::assets {

// The package folder's selection root, with its closing backslash.
inline constexpr wchar_t kSelectionRoot[] = L"assets\\selection\\";

// The pick space embedded from data/pick-space.json: parsed once, on first
// use, by any thread. Empty (no fighters) if the embedded text does not
// parse, in which case no path but the manifest's is a selection asset path.
const picks::PickSpace& EmbeddedPickSpace();

// The path is one of the four forms above, over `space`: the manifest; a
// cutout of a costume and colour the fighter has; a loadout picture of a
// choice the fighter has (a V-Skill or V-Trigger number up to
// picks::PartChoices). Exact match only: no other suffix, extension, case,
// separator, `..`, stream (`:`) or executable name is accepted.
bool IsSfvSelectionAssetPath(const std::wstring& path, const picks::PickSpace& space);
// The same over EmbeddedPickSpace().
bool IsSfvSelectionAssetPath(const std::wstring& path);

// The path builders. Each gives the package-form path of the file, whether or
// not the pick space has that costume or colour; an empty code, a negative
// number, or a loadout choice outside 0 (I) and 1 (II) gives an empty string.
// `choice` is the game's choice number, 0 for I, so the file is `vs<choice+1>`.
std::wstring ManifestPath();
std::wstring CutoutPath(std::string_view fighterCode, int costume, int colour);
std::wstring LoadoutPicturePath(std::string_view fighterCode, picks::LoadoutPart part, int choice);

} // namespace sfve::assets
