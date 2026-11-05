#include "ember/SfvCatalog.hxx"

#include "mod/HelperLink.hxx"

#include "common/GameCatalog.hxx"

#include <xxhash.h>

#include <cstdio>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Written once by FillCatalog, before the runtime's threads read the catalog.
mod::ContentLists s_lists;
std::string s_buildId = mod::kMatchBuild;

bool ValidPick(const sf4e::selection::Pick& pick, bool)
{
	return ember::ValidFighter(pick.fighter) && pick.costume >= 0 && pick.costume < kVariantLimit && pick.color >= 0 &&
		pick.color < kVariantLimit && pick.ultra == 0 && pick.handicap == 0;
}

bool ValidStage(std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)s_lists.stages.size();
}

// Empty until FillCatalog. SFV offers no public rooms yet, so the showcase
// faces are simply the first four fighters of the roster.
constinit ember::GameCatalog s_catalog = {0, kVariantLimit, kVariantLimit, 1, &ValidPick, &ValidStage, {0, 1, 2, 3}};

std::string CodeAt(const std::vector<std::string>& codes, std::int64_t id)
{
	return id >= 0 && id < (std::int64_t)codes.size() ? codes[(size_t)id] : std::string();
}

// The game's codes are ASCII capitals and digits.
std::wstring WideCode(const std::string& code)
{
	return std::wstring(code.begin(), code.end());
}

} // namespace

bool FillCatalog(const mod::ContentLists& lists)
{
	if (s_catalog.fighterCount > 0) return lists.fighters == s_lists.fighters && lists.stages == s_lists.stages;
	if (lists.fighters.empty() || lists.stages.empty() || lists.fighters.size() > (size_t)ember::MaxFighters) return false;
	s_lists = lists;
	s_buildId = std::string(mod::kMatchBuild) + "/" + ContentFingerprint(lists);
	s_catalog.fighterCount = (int)s_lists.fighters.size();
	return true;
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
