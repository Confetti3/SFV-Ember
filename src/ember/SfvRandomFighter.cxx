#include "ember/SfvRandomFighter.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvEleven.hxx"
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
// What that roll was taken as: Random's (1) or Eleven's (2); 0 for none. A
// roll already taken is taken again when the pick changes from one to the
// other, so the stored look follows the pick.
std::atomic<int> s_takenAs{0};

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
	// Eleven's looks for the next game's roll (the runtime numbers rolls from
	// 1 and counts each new one), so a Ready the runtime remakes over a new
	// roll still carries the Eleven look and the loadout drawn for that game.
	const bool eleven = PickEleven();
	const std::uint64_t nextSeed = ElevenSeed(ElevenSalt(), (runtime ? runtime->randomRoll : 0) + 1);
	for (int fighter = 0; fighter < fighters && fighter < ember::MaxFighters; fighter++) {
		sf4e::selection::Pick look;
		look.fighter = fighter;
		look = eleven ? ElevenPick(fighter, nextSeed) : RememberedLook(fighter).value_or(look);
		ToWire(look, tiny, remembered[(size_t)fighter]);
	}
	sf4e::NetplayFacade::NoteRandomFighter(random, lobby, remembered);
}

void FollowRandomFighter(const sf4e::NetplayFacade::RuntimeSnapshot& snapshot)
{
	if (!PickRandom()) return;
	const int fighter = snapshot.randomFighter;
	const bool eleven = PickEleven();
	const int as = eleven ? 2 : 1;
	// Each roll once: a look edited since on the rolled fighter stays. A roll
	// taken as the other kind of pick is taken again.
	const int was = s_takenAs.load();
	const bool changed = was != 0 && was != as;
	if (!ember::ValidFighter(fighter) || (snapshot.randomRoll == s_taken.load() && !changed)) return;
	s_taken = snapshot.randomRoll;
	s_takenAs = as;
	PlayerPick pick = CurrentPick();
	if (eleven) {
		// The game's Eleven look and a loadout drawn for this roll, whether or
		// not the fighter is the one already stored: every game is drawn anew.
		pick.pick = ElevenPick(fighter, ElevenSeed(ElevenSalt(), snapshot.randomRoll));
		SetPick(pick);
		mod::Log("Fighter select: Eleven rolled %s costume %d colour %d", FighterCode(fighter).c_str(), pick.pick.costume,
			pick.pick.color);
		return;
	}
	if (pick.pick.fighter == fighter && !changed) return;
	pick.pick = LookFor(pick.pick, fighter, true);
	SetPick(pick);
	mod::Log("Fighter select: Random rolled %s", FighterCode(fighter).c_str());
}

void RandomFighterPlayed()
{
	sf4e::NetplayFacade::NoteRandomFighterPlayed();
}

} // namespace sfve::adapter
