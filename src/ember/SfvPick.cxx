#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"

#include <mutex>
#include <optional>

namespace sfve::adapter {

namespace {

std::mutex s_mutex;
PlayerPick s_pick;
std::optional<PlayerPick> s_dummy; // none until the player picks one

// Random, or a stage this copy's player is offered (StageOffered, the one
// rule for their own stage offers); anything else becomes the first stage
// they are offered, never stage 0, which they may not be. With none offered
// it becomes Random, which then has nothing to roll and the Ready check
// refuses.
int FittedStage(int stage)
{
	if (stage == picks::kRandomStage || StageOffered(stage)) return stage;
	for (int id = 0; id < StageCount(); id++) {
		if (StageOffered(id)) return id;
	}
	return picks::kRandomStage;
}

// The pick under the catalog's rules and fitted to what this copy's player
// can ready (FitOwnPick), and its stage fitted (FittedStage); as it is until
// the catalog is filled.
PlayerPick Fitted(PlayerPick pick)
{
	if (!ember::Usable(ember::Catalog())) return pick;
	FitOwnPick(pick.pick);
	pick.stage = FittedStage(pick.stage);
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

} // namespace sfve::adapter
