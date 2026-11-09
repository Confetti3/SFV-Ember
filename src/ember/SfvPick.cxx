#include "ember/SfvPick.hxx"

#include "ember/SfvCatalog.hxx"

#include "common/GameCatalog.hxx"
#include "common/StageCatalog.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <mutex>
#include <optional>

namespace sfve::adapter {

namespace {

std::mutex s_mutex;
PlayerPick s_pick;
std::optional<PlayerPick> s_dummy; // none until the player picks one

// Random, or a stage this copy has installed (StageInstalled, the per-copy
// rule); anything else becomes the first installed stage, never stage 0,
// which this copy may lack.
int FittedStage(int stage)
{
	if (stage == picks::kRandomStage || StageInstalled(stage)) return stage;
	for (int id = 0; id < StageCount(); id++) {
		if (StageInstalled(id)) return id;
	}
	return 0;
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
		std::to_string(pick.pick.color) + " stage " + (pick.stage == picks::kRandomStage ? "random" : StageCode(pick.stage));
}

std::string OfflineStageCode(const PlayerPick& pick)
{
	// The roll is the game state's (SfvGame's system generator), as P1's
	// Ready rolls a room's Random.
	const ember::GameCatalog& catalog = ember::Catalog();
	const int stage =
		catalog.resolveStage(pick.stage, ember::game::Game().state.Random(), sf4e::selection::RandomPool(), nullptr, MissingStages());
	return StageCode(stage);
}

} // namespace sfve::adapter
