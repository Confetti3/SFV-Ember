#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"

#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

// Written once by FillCatalog, before the runtime's threads read the catalog.
mod::ContentLists s_lists;

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

// "CODE:costume:colour", a player's part of a setup.
std::string PlayerSetup(const sf4e::selection::Pick& pick)
{
	return FighterCode(pick.fighter) + ":" + std::to_string(pick.costume) + ":" + std::to_string(pick.color);
}

} // namespace

bool FillCatalog(const mod::ContentLists& lists)
{
	if (s_catalog.fighterCount > 0) return lists.fighters == s_lists.fighters && lists.stages == s_lists.stages;
	if (lists.fighters.empty() || lists.stages.empty() || lists.fighters.size() > (size_t)ember::MaxFighters) return false;
	s_lists = lists;
	s_catalog.fighterCount = (int)s_lists.fighters.size();
	return true;
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
	// P1's setup names the match's rules; of P2's only the fighter counts.
	const std::string rules = " " + StageCode(match.stage) + " " + std::to_string(match.rounds) + " " +
		std::to_string(match.roundSeconds);
	mod::MatchPicks picks;
	mod::MatchRules ignored;
	if (!mod::ParseSetup(PlayerSetup(match.picks[0]) + rules, &picks.players[0], &picks.rules) ||
		!mod::ParseSetup(PlayerSetup(match.picks[1]), &picks.players[1], &ignored)) {
		return std::nullopt;
	}
	return picks;
}

} // namespace sfve::adapter

namespace ember {
const GameCatalog& Catalog() { return sfve::adapter::s_catalog; }
} // namespace ember
