#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"

#include <algorithm>
#include <mutex>
#include <optional>

namespace sfve::adapter {

namespace {

std::mutex s_mutex;
PlayerPick s_pick;
std::optional<PlayerPick> s_dummy; // none until the player picks one

// The pick under the catalog's rules and fitted to what this copy's player
// can ready (FitOwnPick), and its stage fitted to the stage row's choices
// (FittedStage, over StageOffered, the one rule for their own stage offers);
// as it is until the catalog is filled.
PlayerPick Fitted(PlayerPick pick)
{
	if (!ember::Usable(ember::Catalog())) return pick;
	FitOwnPick(pick.pick);
	pick.stage = FittedStage(pick.stage, PlayableIds(StageOptions({})));
	return pick;
}

// Why the asked costume is not the stored one, for the log: what the
// catalog says of it for this copy's player.
const char* CostumeState(const sf4e::selection::Pick& asked)
{
	const std::vector<int> ready = CostumeIds(asked.fighter, true);
	if (std::find(ready.begin(), ready.end(), asked.costume) != ready.end()) return "ready here";
	const std::vector<int> all = CostumeIds(asked.fighter, false);
	if (std::find(all.begin(), all.end(), asked.costume) == all.end()) return "not in the game's spec";
	return CostumeNotOwned(asked.fighter, asked.costume) ? "not owned by this account" : "not installed here";
}

// A pick stored as another look than the one asked for (the fitting moved
// its fighter, costume or colour) is said in the log, with the catalog's
// view of the asked costume, so a player's report of a look that "changes
// by itself" can be read from their log.
void LogRefitted(const char* which, const PlayerPick& asked, const PlayerPick& stored)
{
	const sf4e::selection::Pick& a = asked.pick;
	const sf4e::selection::Pick& s = stored.pick;
	if (a.fighter == s.fighter && a.costume == s.costume && a.color == s.color) return;
	mod::Log("pick: %s %s costume %d colour %d (costume %s) stored as %s costume %d colour %d", which,
		FighterCode(a.fighter).c_str(), a.costume, a.color, CostumeState(a), FighterCode(s.fighter).c_str(), s.costume,
		s.color);
}

} // namespace

PlayerPick CurrentPick()
{
	std::lock_guard lock(s_mutex);
	return Fitted(s_pick);
}

PlayerPick SetPick(const PlayerPick& pick)
{
	std::lock_guard lock(s_mutex);
	s_pick = Fitted(pick);
	LogRefitted("asked for", pick, s_pick);
	return s_pick;
}

PlayerPick CurrentDummy()
{
	std::lock_guard lock(s_mutex);
	PlayerPick dummy = Fitted(s_dummy.value_or(s_pick));
	dummy.stage = 0;
	return dummy;
}

PlayerPick SetDummy(const PlayerPick& dummy)
{
	std::lock_guard lock(s_mutex);
	s_dummy = Fitted(dummy);
	LogRefitted("dummy asked for", dummy, *s_dummy);
	s_dummy->stage = 0;
	return *s_dummy;
}

void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command)
{
	PlayerPick pick = CurrentPick();
	// The player's own voice for the fighter goes with the pick, so the
	// opponent's copy plays it too (picks::VoiceIn, SfvCatalog's LaunchPicks).
	picks::SetVoice(pick.pick, OwnVoice(pick.pick.fighter));
	// The costume code the player chose, with the marker that says this copy
	// knows codes even for none (picks::LoadCodeIn, SfvCatalog's LaunchPicks).
	picks::SetLoadCode(pick.pick, picks::LoadCodeIn(pick.pick).value_or(0));
	sf4e::selection::ToNative(pick.pick, command.character);
	command.stage = pick.stage;
}

std::string PickText(const PlayerPick& pick)
{
	return FighterCode(pick.pick.fighter) + " costume " + std::to_string(pick.pick.costume) + " colour " +
		std::to_string(pick.pick.color) + " " + picks::LoadoutText(picks::LoadoutOf(pick.pick)) + " stage " +
		(pick.stage == picks::kRandomStage ? "random" : StageCode(pick.stage));
}

int FittedStage(int stage, const std::vector<int>& choices)
{
	if (std::find(choices.begin(), choices.end(), stage) != choices.end()) return stage;
	const auto first = std::find_if(choices.begin(), choices.end(), [](int id) { return id != picks::kRandomStage; });
	return first == choices.end() ? picks::kRandomStage : *first;
}

NamedStage NameStage(const std::wstring& code, int stage)
{
	if (code.empty()) return {stage};
	if (code == L"RND") return {picks::kRandomStage};
	const std::optional<int> id = StageId(code);
	if (!id) return {stage, StageRefusal::NotInGame};
	if (!StageInstalled(*id)) return {stage, StageRefusal::NotInstalled};
	if (!StageOffered(*id)) return {stage, StageRefusal::NotOffered};
	return {*id};
}

} // namespace sfve::adapter
