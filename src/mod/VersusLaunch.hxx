#pragma once

// Starting a battle without the menus, as SF4 Ember does: the mod picks the
// fighters and the stage and starts the match itself, so two linked copies go
// from the main menu straight into the same battle.
//
// SFV has its own way into a battle without character select: the "battle
// jump" scene state (BattleJumpSetupState), which the game uses to go from
// a battle into training with the same players. The mod starts it from the
// main menu with a target of its own (local versus) and:
//   - before its first step, which reads the battle flow's loader (left by
//     the previous battle; leaving the main menu resets the flow), gives the
//     flow a loader so that step has one to read;
//   - after its second step, when the new mode's data object exists and
//     before anything copies it into the battle setting, writes the picks
//     into that data object through its own setters.
// The game then loads the stage and fighters and enters the battle itself.

#include <stdint.h>

#include <string>

namespace sfve::mod {

enum class LaunchTarget {
	VersusCpu = 0, // player 2 is the CPU
	Versus = 1,    // two players
	Training = 2,
};

// An empty fighter, a negative costume or colour, or an empty stage keeps
// what the mode's data object already holds (the profile's defaults).
struct FighterPick {
	std::wstring fighter; // the game's three-letter code, e.g. L"KEN"
	int costume = -1;
	int colour = -1;
};

struct MatchPicks {
	LaunchTarget target = LaunchTarget::Versus;
	FighterPick players[2];
	std::wstring stage; // the game's stage code
};

bool InstallVersusLaunch(uintptr_t imageBase);

// Starts the battle from the main menu; any thread (runs on the game thread).
// Refused (logged) when the game is not on the main menu.
void QueueLaunch(const MatchPicks& picks);

// Control command: "launch [versus|cpu|training] [P1] [P2] [stage]", a player
// as CODE[:costume[:colour]] or "-" to keep the default.
void RegisterVersusLaunchCommands();

} // namespace sfve::mod
