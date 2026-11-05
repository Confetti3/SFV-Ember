#include "ember/SfvCatalog.hxx"

#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"

#include <xxhash.h>

#include <cstdio>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Written once by FillCatalog, before the runtime's threads read the catalog.
mod::ContentLists s_lists;
std::string s_buildId = kMatchBuild;
// The runtime has started (SealCatalog): nothing writes the catalog again.
bool s_sealed = false;
// The roster in the order of the game's list, which is its ids' order.
std::vector<int> s_rosterOrder;

bool ValidPick(const sf4e::selection::Pick& pick, bool)
{
	return ember::ValidFighter(pick.fighter) && pick.costume >= 0 && pick.costume < kVariantLimit && pick.color >= 0 &&
		pick.color < kVariantLimit && pick.ultra == 0 && pick.handicap == 0;
}

bool ValidStage(std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)s_lists.stages.size();
}

// No reader of the player's SFV content yet (SfvGame's FighterAvailability
// reports everything owned): no fighter is locked and every valid pick is
// owned. The game's own ownership check judges the content as the battle
// launches (VersusLaunch's checkOwnership).
bool FighterLocked(int, const sf4e::selection::Availability&)
{
	return false;
}

bool AvailablePick(const sf4e::selection::Pick& pick, bool editionSelect, const sf4e::selection::Availability&,
	const sf4e::selection::InstalledContent*)
{
	return ValidPick(pick, editionSelect);
}

// Each part SFV refuses becomes the first one: fighter 0, costume and colour
// 0, no Ultra and no handicap.
bool NormalizePick(sf4e::selection::Pick& pick, bool, const sf4e::selection::Availability*,
	const sf4e::selection::InstalledContent*)
{
	const sf4e::selection::Pick before = pick;
	if (!ember::ValidFighter(pick.fighter)) pick.fighter = 0;
	if (pick.costume < 0 || pick.costume >= kVariantLimit) pick.costume = 0;
	if (pick.color < 0 || pick.color >= kVariantLimit) pick.color = 0;
	pick.ultra = 0;
	pick.handicap = 0;
	return pick.fighter != before.fighter || pick.costume != before.costume || pick.color != before.color ||
		pick.ultra != before.ultra || pick.handicap != before.handicap;
}

// SFV has no Random stage: a choice is a stage of the list, else the first.
int ResolveStage(int choice, std::uint32_t, const sf4e::selection::RandomPool&, const sf4e::selection::InstalledContent*)
{
	return ValidStage(choice) ? choice : 0;
}

// The game's code ("RYU") until SFV Ember has a table of fighters' names.
// The shell asks for fighter 0 even when the catalog is empty (its lists
// were not filled or were refused), so an id outside the roster reads "?".
const char* FighterName(int fighter)
{
	if (fighter < 0 || fighter >= (int)s_lists.fighters.size()) return "?";
	return s_lists.fighters[(size_t)fighter].c_str();
}

// Every costume number below the limit is the game's own: SFV has no custom
// costumes.
int CostumeCount(int)
{
	return kVariantLimit;
}

// Empty until FillCatalog. SFV offers no public rooms yet, so the showcase
// faces are simply the first four fighters of the roster.
constinit ember::GameCatalog s_catalog = {0, kVariantLimit, kVariantLimit, 1, &ValidPick, &ValidStage, {0, 1, 2, 3},
	-1, &FighterLocked, &AvailablePick, &NormalizePick, &ResolveStage, &FighterName, &CostumeCount, nullptr};

std::string CodeAt(const std::vector<std::string>& codes, std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)codes.size() ? codes[(size_t)id] : std::string();
}

// The game's codes are ASCII capitals and digits.
std::wstring WideCode(const std::string& code)
{
	return std::wstring(code.begin(), code.end());
}

std::optional<int> IdOf(const std::vector<std::string>& codes, const std::wstring& code)
{
	for (size_t id = 0; id < codes.size(); id++) {
		if (WideCode(codes[id]) == code) return (int)id;
	}
	return std::nullopt;
}

} // namespace

bool FillCatalog(const mod::ContentLists& lists)
{
	if (s_sealed) {
		mod::Log("catalog: refused a fill after Ember's runtime started (%zu fighters, %zu stages)", lists.fighters.size(),
			lists.stages.size());
		return false;
	}
	if (s_catalog.fighterCount > 0) return lists.fighters == s_lists.fighters && lists.stages == s_lists.stages;
	if (lists.fighters.empty() || lists.stages.empty() || lists.fighters.size() > (size_t)ember::MaxFighters) return false;
	s_lists = lists;
	s_buildId = std::string(kMatchBuild) + "/" + ContentFingerprint(lists);
	for (int id = 0; id < (int)s_lists.fighters.size(); id++) s_rosterOrder.push_back(id);
	s_catalog.rosterOrder = s_rosterOrder.data();
	s_catalog.fighterCount = (int)s_lists.fighters.size();
	return true;
}

void SealCatalog()
{
	s_sealed = true;
}

const std::string& MatchBuildId()
{
	return s_buildId;
}

std::string ContentFingerprint(const mod::ContentLists& lists)
{
	// Each list by its length, each code by its length, so no two lists
	// read alike.
	std::string text;
	for (const std::vector<std::string>* codes : {&lists.fighters, &lists.stages}) {
		text += std::to_string(codes->size()) + ";";
		for (const std::string& code : *codes) text += std::to_string(code.size()) + ":" + code;
	}
	char hex[17];
	std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)XXH3_64bits(text.data(), text.size()));
	return hex;
}

std::string FighterCode(std::int64_t fighter)
{
	return CodeAt(s_lists.fighters, fighter);
}

std::string StageCode(std::int64_t stage)
{
	return CodeAt(s_lists.stages, stage);
}

std::optional<int> FighterId(const std::wstring& code)
{
	return IdOf(s_lists.fighters, code);
}

std::optional<int> StageId(const std::wstring& code)
{
	return IdOf(s_lists.stages, code);
}

std::optional<mod::MatchPicks> LaunchPicks(const RoomMatch& match)
{
	if (!ValidPick(match.picks[0], false) || !ValidPick(match.picks[1], false) || !ValidStage(match.stage)) {
		return std::nullopt;
	}
	const std::optional<int> clock = mod::RoundClockFrames(match.roundSeconds);
	if (!mod::PlayableRounds(match.rounds) || !clock) return std::nullopt;
	mod::MatchPicks picks;
	for (int side = 0; side < 2; side++) {
		const sf4e::selection::Pick& pick = match.picks[side];
		picks.players[side] = {WideCode(FighterCode(pick.fighter)), pick.costume, pick.color};
	}
	picks.rules.stage = WideCode(StageCode(match.stage));
	picks.rules.rounds = match.rounds;
	picks.rules.timeFrames = *clock;
	return picks;
}

} // namespace sfve::adapter

namespace ember {
const GameCatalog& Catalog() { return sfve::adapter::s_catalog; }
} // namespace ember
