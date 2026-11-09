#pragma once

// Starting a battle without the menus, as SF4 Ember does: the mod picks the
// fighters and the stage and starts the match itself, so two linked copies go
// from the main menu straight into the same battle.
//
// SFV has its own way into a battle without character select: the "battle
// jump" scene state (BattleJumpSetupState), which the game uses to go from
// a battle into training with the same players. The mod starts it from the
// main menu with a target of its own (local versus, versus CPU, training)
// and:
//   - before its first step, which reads the battle flow's loader (left by
//     the previous battle; leaving the main menu resets the flow), gives the
//     flow a loader so that step has one to read;
//   - after its second step, when the new mode's data object exists and
//     before anything copies it into the battle setting, writes the picks
//     into that data object through its own setters.
// The game then loads the stage and fighters and enters the battle itself.

#include <stdint.h>

#include <functional>
#include <optional>
#include <string>

namespace sfve::mod {

enum class LaunchTarget {
	VersusCpu = 0, // player 2 is the CPU
	Versus = 1,    // two players
	Training = 2,
};

// An empty fighter, a negative costume or colour, or an empty stage keeps
// what the mode's data object already holds (the profile's defaults). The
// V-Skill and V-Trigger never do: every launch writes both players' choices,
// so no battle inherits them from an earlier one.
struct FighterPick {
	std::wstring fighter; // the game's three-letter code, e.g. L"KEN"
	int costume = -1;
	int colour = -1;
	// The game's numbers for the V-Skill and V-Trigger: 0 is I, 1 is II
	// (SfvCatalog's FighterPickOf makes them from a pick's loadout).
	int vSkill = 0;
	int vTrigger = 0;
};

// The pick's costume and colour (where given), its V-Skill and its
// V-Trigger, into the game's per-fighter record (a CharPkg,
// sfv::jump::kCharPkgSize bytes): the fields WriteFighter sets besides the
// code, which the game's own setters then copy. No game code.
void FillCharPkg(uint8_t* chara, const FighterPick& pick);

// A player's look as a launch writes it and as the loaded battle's setting
// holds it, read back after the load to log what each copy loaded.
struct PlayerLook {
	std::string fighter; // the game's code
	int costume = -1;
	int colour = -1;
};
// The loaded look is not the one the launch wrote. Codes compare as the game
// compares them, ignoring case (GameContent's SameCode): one side is the
// launch's FString narrowed, the other the setting record's own code.
bool LookMismatch(const PlayerLook& written, const PlayerLook& loaded);

// The match's rules, as the versus battle settings offer them; negative
// keeps what the mode's data holds (the player's own settings).
struct MatchRules {
	std::wstring stage; // the game's stage code
	int rounds = -1;    // best of 1, 3, 5 or 7
	int timeFrames = -1; // the round clock in frames (99 s = 5940); kNoTimeLimit for none
	static constexpr int kNoTimeLimit = -2; // the game's own value for no time limit
};

struct MatchPicks {
	LaunchTarget target = LaunchTarget::Versus;
	FighterPick players[2];
	MatchRules rules;
	// A linked match's launch (Ember's room): the battle it loads is that
	// match's, which the battle mode learns from TakeLinkedBattle. Any other
	// launch (the pipe's) loads a battle of the player's own.
	bool linked = false;
	// A Practice room's match (EMBER_LIBRARY.md section 27): the battle mode
	// sets Training's refilling gauges before the session's first frame
	// (PracticeGauges). Only a linked launch carries it.
	bool practice = false;
	// A replay's launch (EMBER_LIBRARY.md section 26): the battle it loads
	// plays a recorded battle back, which the battle mode learns from
	// TakeReplayBattle. The game's ownership check never runs for it.
	bool replay = false;
};

// A player's setup as the pipe's commands name it, space separated:
// "CODE[:costume[:colour[:vskill[:vtrigger]]]]", then "STAGE ROUNDS TIME": a
// stage code, the rounds (1, 3, 5 or 7) and the round time in seconds (30 to
// 546) or "inf". A costume and a colour are the game's numbers, 0 to 255 (EX
// colours are 100 and up); a V-Skill and a V-Trigger are 1 or 2, as the game
// calls them I and II. "-" keeps a default (I for a V-Skill or V-Trigger).
// Codes are 1 to 8 capital letters or digits. False for anything else.
bool ParseSetup(const std::string& setup, FighterPick* fighter, MatchRules* rules);

// The rounds and round time SFV plays, as ParseSetup checks them too: best of
// 1, 3, 5 or 7 (the game needs rounds / 2 + 1 wins and keeps seven round
// records), and 30 to 546 seconds (the game's clock holds up to 32767
// frames). RoundClockFrames gives the clock in frames, or nothing for a time
// SFV cannot play.
bool PlayableRounds(int rounds);
std::optional<int> RoundClockFrames(int seconds);

// A room's round time as SFV plays it: RoundClockFrames, and the room's
// longest, 9999 s (room::RoundTimes' last, a Practice room's
// room::PracticeRoundTime), as the game's own no time limit
// (MatchRules::kNoTimeLimit, the versus settings' infinite clock). Nothing
// for a time SFV cannot play.
constexpr int kNoLimitSeconds = 9999;
std::optional<int> RoomClockFrames(int seconds);

// `gameCheckOutsideRooms`: a launch that is not a room's (the pipe's
// `launch`) runs the game's own check, as its menus do (sfv::jump::
// kMessageCheckOwnership). The check judges both players by what this copy
// allows: a fighter it does not allow ends the jump with an error notice, a
// costume or colour it does not allow becomes 0, and a stage it does not
// allow is replaced, in one branch by rand(). A room's match never runs it,
// whatever this says: two copies allowing different content would load
// different battles. Ember's own gate judges each player's pick on that
// player's copy before Ready instead (SfvCatalog, EMBER_LIBRARY.md
// section 23).
bool InstallVersusLaunch(uintptr_t imageBase, bool gameCheckOutsideRooms);

// Called on the game thread when a launch is refused or fails on the way
// (the game then stays on, or goes back to, the main menu, or loads the
// battle with the mode's defaults).
using LaunchFailed = std::function<void(const std::string& why)>;

// Starts the battle from the main menu; any thread (runs on the game thread).
// Refused when the game is not on the main menu or a launch is under way.
void QueueLaunch(const MatchPicks& picks, LaunchFailed onFailed = {});

// Launches once the game is on the main menu (from the title or a battle's
// end it may not be yet), unless stillWanted() turns false first or the
// menu does not come within two minutes; any thread.
void LaunchOnMainMenu(const MatchPicks& picks, std::function<bool()> stillWanted, LaunchFailed onFailed);

// Game thread, as a battle binds: true once for the battle a linked launch
// loaded (its picks went into the jump and the load did not fail), false for
// any other battle, which is the player's own (a native menu's, or the
// pipe's "launch"). A linked launch that fails before its battle binds
// leaves nothing to take.
// `practice`, when given, says whether that launch was a Practice room's.
bool TakeLinkedBattle(bool* practice = nullptr);
// The same for a replay's launch (MatchPicks::replay): true once for the
// battle it loaded.
bool TakeReplayBattle();

// Control command: "launch [versus|cpu|training] [P1] [P2] [stage]", a player
// as CODE[:costume[:colour[:vskill[:vtrigger]]]] or "-" to keep the default.
void RegisterVersusLaunchCommands();

} // namespace sfve::mod
