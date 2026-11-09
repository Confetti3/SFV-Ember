#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"

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
	s_dummy->stage = 0;
	return *s_dummy;
}

void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command)
{
	const PlayerPick pick = CurrentPick();
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

std::optional<std::string> NameStage(const std::wstring& code, int& stage)
{
	if (code.empty()) return std::nullopt;
	if (code == L"RND") {
		stage = picks::kRandomStage;
		return std::nullopt;
	}
	const std::optional<int> id = StageId(code);
	if (!id) return "error that stage is not in this game's list";
	if (!StageInstalled(*id)) return "error that stage is not installed in this copy";
	if (!StageOffered(*id)) return "error that stage is not this player's to choose (the account's entitlement cache lacks it)";
	stage = *id;
	return std::nullopt;
}

} // namespace sfve::adapter
