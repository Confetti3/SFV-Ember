#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"

#include <mutex>

namespace sfve::adapter {

namespace {

std::mutex s_mutex;
PlayerPick s_pick;

// The pick under the catalog's rules (its NormalizePick, and a stage of the
// list); as it is until the catalog is filled.
PlayerPick Fitted(PlayerPick pick)
{
	const ember::GameCatalog& catalog = ember::Catalog();
	if (!ember::Usable(catalog)) return pick;
	catalog.normalizePick(pick.pick, false, nullptr, nullptr);
	if (!catalog.validStage(pick.stage)) pick.stage = 0;
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

void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command)
{
	const PlayerPick pick = CurrentPick();
	sf4e::selection::ToNative(pick.pick, command.character);
	command.stage = pick.stage;
}

std::string PickText(const PlayerPick& pick)
{
	return FighterCode(pick.pick.fighter) + " costume " + std::to_string(pick.pick.costume) + " colour " +
		std::to_string(pick.pick.color) + " stage " + StageCode(pick.stage);
}

} // namespace sfve::adapter
