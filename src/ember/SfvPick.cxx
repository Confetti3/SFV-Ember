#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"

#include <mutex>

namespace sfve::adapter {

namespace {

std::mutex s_mutex;
PlayerPick s_pick;

} // namespace

PlayerPick CurrentPick()
{
	std::lock_guard lock(s_mutex);
	return s_pick;
}

void SetPick(const PlayerPick& pick)
{
	std::lock_guard lock(s_mutex);
	s_pick = pick;
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
