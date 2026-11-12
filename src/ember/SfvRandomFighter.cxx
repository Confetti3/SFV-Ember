#include "ember/SfvRandomFighter.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "mod/Log.hxx"

#include "common/GameCatalog.hxx"
#include "common/MatchWireTypes.hxx"

#include <array>
#include <atomic>
#include <cstdint>

namespace sfve::adapter {

namespace {

// The last roll taken (RuntimeSnapshot::randomRoll); 0 is none, as the
// runtime counts rolls from 1.
std::atomic<std::uint64_t> s_taken{0};

} // namespace

void NoteRandomFighter(bool random)
{
	// The wire's picks, as a Ready sends them (SfvPick's ToWire): the stored
	// pick, and each fighter's remembered look, or the fighter alone for one
	// not stored this run, which the runtime fits as it rolls
	// (selection::RolledPick).
	const auto runtime = sf4e::NetplayFacade::GetRuntimeSnapshotShared();
	const bool tiny = runtime && runtime->Room().tinyFighters;
	sf4e::wire::ConfirmedCharaConditions lobby{};
	ToWire(CurrentPick().pick, tiny, lobby);
	std::array<sf4e::wire::ConfirmedCharaConditions, ember::MaxFighters> remembered{};
	const int fighters = ember::Catalog().fighterCount;
	for (int fighter = 0; fighter < fighters && fighter < ember::MaxFighters; fighter++) {
		sf4e::selection::Pick look;
		look.fighter = fighter;
		look = RememberedLook(fighter).value_or(look);
		ToWire(look, tiny, remembered[(size_t)fighter]);
	}
	sf4e::NetplayFacade::NoteRandomFighter(random, lobby, remembered);
}

void FollowRandomFighter(const sf4e::NetplayFacade::RuntimeSnapshot& snapshot)
{
	if (!PickRandom()) return;
	const int fighter = snapshot.randomFighter;
	// Each roll once: a look edited since on the rolled fighter stays.
	if (!ember::ValidFighter(fighter) || snapshot.randomRoll == s_taken.load()) return;
	s_taken = snapshot.randomRoll;
	PlayerPick pick = CurrentPick();
	if (pick.pick.fighter == fighter) return;
	pick.pick = LookFor(pick.pick, fighter, true);
	SetPick(pick);
	mod::Log("Fighter select: Random rolled %s", FighterCode(fighter).c_str());
}

void RandomFighterPlayed()
{
	sf4e::NetplayFacade::NoteRandomFighterPlayed();
}

} // namespace sfve::adapter
