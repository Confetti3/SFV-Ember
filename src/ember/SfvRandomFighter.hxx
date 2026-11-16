#pragma once

// Random on the fighter board: the player's pick is a fighter rolled for each
// game, in the look last used on that fighter. The runtime owns the roll
// (room::RandomFighterPick, in the library); this is SFV's side of it, modelled
// on SF4 Ember's FollowRandomFighter (the library's
// sf4e__OverlayRandomFighter.cxx): tell the runtime every frame whether the
// pick is Random, take each new roll it publishes once, and say when Training
// played the rolled fighter.
//
// "The look last used on a fighter" is SfvPick's per-fighter memory (session
// only) and its Random flag; these functions read and write the stored pick
// only through SfvPick's own functions, so the pick stays fitted as the rest
// of SFV Ember keeps it (SfvPick.hxx).
//
// The dummy's pick screen has no Random card: nothing here touches the dummy.
//
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <cstdint>
#include <optional>

namespace sfve::adapter {

// What NoteRandomFighter's note to the runtime is made from. The note is the
// same while these are, and the runtime keeps the last one it was given.
struct RandomNoteInputs {
	bool random = false, eleven = false, tiny = false;
	int fighters = 0;
	std::uint64_t pick = 0;   // SfvPick's PickChanges: the stored pick, the remembered looks
	std::uint64_t voices = 0; // SfvCatalog's OwnVoicesRevision
	std::uint64_t roll = 0;   // the roll Eleven's looks are drawn for (Eleven only)
	bool operator==(const RandomNoteInputs&) const = default;
};
// Whether the note is due: the first time, and each time the inputs differ
// from the last that was due.
class RandomNoteGate {
public:
	bool Due(const RandomNoteInputs& inputs);

private:
	std::optional<RandomNoteInputs> last_;
};

// Any thread, every frame the pick screen or the runtime's shell is up: tells
// the runtime whether the player's pick is Random
// (NetplayFacade::NoteRandomFighter, with the stored pick and the per-fighter
// remembered looks as the wire carries them), so it rolls the next game's
// fighter and sends it with every Ready. Each frame it compares what the note
// is made from (RandomNoteInputs) with the last note's, and only a change
// makes and sends a new one.
void NoteRandomFighter(bool random);

// Game thread, every frame: when `snapshot` carries a roll this function has
// not taken (snapshot.randomRoll differs from the last taken, and
// snapshot.randomFighter is a fighter of the catalog), the stored pick
// becomes that fighter in its remembered look, fitted; one log line
// "Fighter select: Random rolled <CODE>". Does nothing while the pick is not
// Random. While the pick is Eleven (SfvPick's PickEleven, SfvEleven) every
// roll is taken, even a repeat of the stored fighter, as that fighter in the
// look the game's Eleven wears with a loadout drawn for the roll ("Fighter
// select: Eleven rolled <CODE> ..."); a roll already taken is taken again
// when the pick changes between Random and Eleven. NoteRandomFighter hands
// the runtime Eleven's looks for the next roll in place of the remembered
// ones, so a Ready the runtime remakes over a new roll carries them.
void FollowRandomFighter(const sf4e::NetplayFacade::RuntimeSnapshot& snapshot);

// Training from Home played the rolled fighter: the next roll comes at once
// (NetplayFacade::NoteRandomFighterPlayed).
void RandomFighterPlayed();

} // namespace sfve::adapter
