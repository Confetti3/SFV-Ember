// SFV's room host catalog (EMBER_LIBRARY.md section 24): the library's room
// host (server/roomhost) linked with this file in place of SF4's catalog. Its
// picks are judged by the pick space the game exported (`catalog-export`),
// embedded at build time (SfvHostPickSpace.cxx, generated from
// data/pick-space.json), under the same rules the game's own catalog applies
// (SfvPickSpace). Before the room opens, PrepareCatalog checks that the data
// is the configured build's: the build id is the pick space's fingerprint, so
// a host can only judge the room of the game data it was made from.
//
// The host is no player: no fighter is locked, every valid pick is available,
// and a pick is fitted by the pick space alone. Names are the game's codes.
#include "ember/SfvPickSpace.hxx"
#include "ember/SfvRoomBuild.hxx"

#include "common/GameCatalog.hxx"

#include "RoomHost.hxx" // the library's server/roomhost

#include <iterator>
#include <string>
#include <vector>

namespace sfve::roomhost {
// The exported pick space, as data/pick-space.json holds it.
extern const char* const kPickSpaceJson;
} // namespace sfve::roomhost

namespace {

sfve::picks::PickSpace s_space;
std::vector<int> s_rosterOrder;

bool ValidPick(const sf4e::selection::Pick& pick, bool)
{
	return sfve::picks::ValidPick(s_space, pick);
}

bool ValidStage(std::int64_t id)
{
	return sfve::picks::ValidStage(s_space, id);
}

bool FighterLocked(int fighter, const sf4e::selection::Availability&)
{
	return !sfve::picks::FighterAt(s_space, fighter);
}

bool AvailablePick(const sf4e::selection::Pick& pick, bool editionSelect, const sf4e::selection::Availability&,
	const sf4e::selection::InstalledContent*)
{
	return ValidPick(pick, editionSelect);
}

bool NormalizePick(sf4e::selection::Pick& pick, bool, const sf4e::selection::Availability*, const sf4e::selection::InstalledContent*)
{
	return sfve::picks::FitPick(s_space, pick);
}

// A host never resolves Random: P1's copy does, before the stage travels, so
// the stage a host sees is always one of the universe (or refused).
int ResolveStage(int choice, std::uint32_t, const sf4e::selection::RandomPool&, const sf4e::selection::InstalledContent*,
	const std::vector<std::int64_t>&)
{
	return ValidStage(choice) ? choice : 0;
}

const char* FighterName(int fighter)
{
	const sfve::picks::Fighter* content = sfve::picks::FighterAt(s_space, fighter);
	return content ? content->code.c_str() : "?";
}

int CostumeCount(int fighter)
{
	return sfve::picks::CostumeCount(s_space, fighter);
}

ember::GameCatalog s_catalog = {0, 0, 0, 1, &ValidPick, &ValidStage, {0, 1, 2, 3}, sfve::picks::kRandomStage, &FighterLocked, &AvailablePick,
	&NormalizePick, &ResolveStage, &FighterName, &CostumeCount, nullptr, sfve::picks::kRoundCounts,
	(int)std::size(sfve::picks::kRoundCounts), sfve::picks::kRoundTimes, (int)std::size(sfve::picks::kRoundTimes), false, true};

} // namespace

namespace ember {
const GameCatalog& Catalog() { return s_catalog; }
} // namespace ember

namespace sf4e { namespace roomhost {

bool PrepareCatalog(const Config& config, std::string& reason)
{
	if (config.game != "sfv") {
		reason = "other_game";
		return false;
	}
	sfve::picks::PickSpace space;
	std::string exported, error;
	if (!sfve::picks::FromJson(sfve::roomhost::kPickSpaceJson, space, exported, error) ||
		!sfve::picks::Problem(space).empty()) {
		reason = "catalog_invalid";
		return false;
	}
	// The data is what the game exported, and the room is for that build. Its
	// layout id is the export's: the host has no game code to work it out.
	const std::string build = sfve::room::RoomBuildId(sfve::picks::BuildId(space), sfve::room::LayoutIdOf(exported));
	if (build != exported || build != config.buildId) {
		reason = "other_build";
		return false;
	}
	s_space = std::move(space);
	for (int id = 0; id < (int)s_space.fighters.size(); id++) s_rosterOrder.push_back(id);
	s_catalog.rosterOrder = s_rosterOrder.data();
	s_catalog.costumeLimit = sfve::picks::CostumeLimit(s_space);
	s_catalog.colorLimit = sfve::picks::ColourLimit(s_space);
	s_catalog.fighterCount = (int)s_space.fighters.size();
	if (!ember::Usable(s_catalog)) {
		s_catalog.fighterCount = 0;
		reason = "catalog_invalid";
		return false;
	}
	return true;
}

// SFV Ember's players have no main (nothing in SFV Ember asks for one, so a
// profile's is fighter 0, RYU): the listing shows the pick each member shared
// in the room, else no fighter, as SFV Ember's own room does
// (Branding::roomFacesArePicks; EMBER_LIBRARY.md sections 20 and 28).
ListedFaces CatalogFaces() { return ListedFaces::Picks; }

} } // namespace sf4e::roomhost
