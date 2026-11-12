#pragma once

// The player's pick for the next Ready: the fighter, costume and colour, the
// V-Skill and V-Trigger (the loadout, which a fighter's change fits as the
// costume is), and the stage it offers when it plays P1, as ids of SFV's catalog
// (SfvCatalog). One pick, owned here: Ember's shell edits it on its pick
// screen and table page (SfvShell), and the pipe's test command `pick` sets it
// (RoomCommands); both Ready with it.
//
// It is always a pick the catalog takes: what is stored and read is fitted
// to the catalog's rules (its NormalizePick, the same on every copy), so a
// costume a fighter lacks becomes its first costume and a colour the costume
// lacks its first colour. It is this copy's player's own pick, so it is also
// fitted to what they can ready (SfvCatalog's FitOwnPick: a fighter,
// costume or colour the game does not let them pick, or this copy has not
// installed, becomes their first one they can, and a costume is one they can
// only in a colour they can, so the fitted pick is always one they can
// ready while any fighter is). Its stage is Random or one they are offered
// (SfvCatalog's StageOffered: installed here and, by default, granted by the
// account); another becomes the first stage they are offered. Until anyone
// picks, it is the first fighter they can play in their first costume and
// colour, on the first stage they are offered. Any thread.

#include "common/MatchWireTypes.hxx"
#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

struct PlayerPick {
	sf4e::selection::Pick pick;
	int stage = 0;
};

PlayerPick CurrentPick();
// Stores the pick, fitted; returns what was stored.
PlayerPick SetPick(const PlayerPick& pick);

// Training's other side (Training select's Dummy row, EMBER_LIBRARY.md
// section 27): a pick of its own, fitted the same way, as SF4 Ember keeps its
// Training dummy apart from the player's pick. Its stage is unused (Training
// loads on the player's). Until the player picks a dummy it is their own
// fighter, costume and colour. Neither is saved between runs: the player's
// pick is not saved either.
PlayerPick CurrentDummy();
PlayerPick SetDummy(const PlayerPick& dummy);

// The pick in a room command (Ready, the room's pick updates), as the wire
// carries it, with this copy's player's voice for its fighter (SfvCatalog's
// OwnVoice, picks::SetVoice). The stored pick carries none. Its size (Tiny)
// goes only while the room's Tiny fighters allowed rule is on (SizeToSend).
void AddPickTo(sf4e::NetplayFacade::RuntimeCommand& command);

// The model size a pick sends (EMBER_LIBRARY.md section 59): Tiny only where
// the room allows tiny fighters, else normal, whatever the stored pick says. A
// pick keeps its size while the rule is off, so a rule turned back on finds it.
int SizeToSend(int storedSize, bool roomAllowsTiny);

// The pick as the wire carries it (AddPickTo's): with this copy's player's
// voice for its fighter, the costume code with its marker, and the size only
// where the room allows tiny fighters (SizeToSend). The Random fighter's
// runtime is handed the remembered looks so (SfvRandomFighter).
void ToWire(sf4e::selection::Pick pick, bool roomAllowsTiny, sf4e::wire::ConfirmedCharaConditions& out);

// Random on the fighter board (SfvRandomFighter): the player's pick is a
// fighter rolled for each game, and the stored pick is the rolled fighter in
// the look last used on them. Off until the board's Random card is chosen;
// choosing a fighter turns it off. Not saved between runs, as the pick is
// not. The dummy has no Random. Any thread.
bool PickRandom();
void SetPickRandom(bool random);

// The look last stored for each fighter this run (SetPick remembers the
// stored pick under its fighter: costume, colour, loadout, costume code and
// size), as SF4 Ember keeps one per fighter; none for a fighter not stored
// since the start. The dummy's picks are not remembered (SF4 Ember's dummy
// keeps to its own pick too). Any thread.
std::optional<sf4e::selection::Pick> RememberedLook(int fighter);

// The pick that choosing `fighter` on the board stores, from `pick`: with
// `remembered`, the fighter's remembered look where there is one; otherwise,
// or with none, `pick` with that fighter. Fitted as the pick screen fits a
// fighter (SfvCatalog's FitLookWithin): in a look this player can ready, or
// for a fighter they cannot play, as the spec fits it.
sf4e::selection::Pick LookFor(const sf4e::selection::Pick& pick, int fighter, bool remembered);

// "RYU costume 0 colour 1 vskill II vtrigger I stage S25", by the game's
// codes, for the status and the logs ("stage random" for Random).
std::string PickText(const PlayerPick& pick);

// The stage the stored pick keeps, of `choices` (the stage row's: Random,
// then the stages this player is offered, SfvCatalog's PlayableIds of
// StageOptions with nothing unavailable): the stage where it is one, else the
// first stage offered, never stage 0, which may not be. With none offered it
// is Random, which then has nothing to roll and the Ready check refuses.
int FittedStage(int stage, const std::vector<int>& choices);

// Why a stage the pipe's `pick` names cannot be this player's stage
// (NameStage); RoomCommands words it.
enum class StageRefusal {
	None,
	NotInGame,    // the code is outside the universe
	NotInstalled, // this copy has not installed it
	NotOffered,   // not theirs to choose (SfvCatalog's StageOffered)
};
struct NamedStage {
	int stage = 0; // the stage the pick keeps
	StageRefusal refusal = StageRefusal::None;
	bool operator==(const NamedStage&) const = default;
};
// The pipe's `pick` stage, by the game's code ("S01", or RND, the game's own
// code for Random), for a pick whose stage is `stage`: Random, or a stage this
// player is offered (SfvCatalog's StageOffered). For a code outside the
// universe, a stage not installed here or one not theirs to choose, the
// refusal, and `stage` as it was. An empty code keeps it too.
NamedStage NameStage(const std::wstring& code, int stage);

} // namespace sfve::adapter
