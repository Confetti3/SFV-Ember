#include "mod/MatchEnd.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <utility>

namespace sfve::mod {

namespace {

namespace versus = sfv::versus;

using ShowResultFn = void (*)(uint8_t* versusBattleState);
using RestartFn = void (*)(uint8_t* versusBattleState);

uintptr_t s_base;
ShowResultFn o_showResult;
bool s_linked; // game thread only
uint8_t* s_ended; // game thread only
std::optional<MatchResult> s_decided; // game thread only

// Called by the battle flow's MatchEnd event (code 14), inside a battle
// frame. The game's version marks the results as up (+0x438 = 2) and opens
// the menu; a linked match only gets the mark.
void h_ShowResult(uint8_t* state)
{
	const bool plainVersus = SceneStateKind(state) == sfv::scene::kVersusBattleVtable &&
		!Field<uint8_t>(state, versus::kVersionSelect);
	if (!s_linked || !plainVersus) return o_showResult(state);
	Field<int32_t>(state, versus::kResultPhase) = versus::kResultsUp;
	if (s_ended != state) Log("match end: results skipped; the mod decides for both players");
	s_ended = state;
}

} // namespace

bool InstallMatchEnd(uintptr_t imageBase)
{
	s_base = imageBase;
	return HookCodeSite(imageBase, sfv::kShowResult, (void*)h_ShowResult, (void**)&o_showResult);
}

void SetLinkedMatchEnd(bool on)
{
	s_linked = on;
	if (!on) s_ended = nullptr;
}

bool EndedVersusBattlePending()
{
	return s_ended != nullptr;
}

uint8_t* TakeEndedVersusBattle(const uint8_t* battleData)
{
	uint8_t* ended = std::exchange(s_ended, nullptr);
	if (!ended || ended != CurrentSceneState()) return nullptr;
	s_decided = ReadMatchResult(battleData);
	return ended;
}

std::optional<MatchResult> DecidedMatchResult()
{
	return s_decided;
}

void ForgetEndedVersusBattle()
{
	s_ended = nullptr;
	s_decided.reset();
}

void Rematch(uint8_t* state)
{
	// What the menu's "Continue" does when it restarts at once (rva
	// 0x66A690): clear the results mark, then the game's restart, which
	// broadcasts BattleRestart (flow code 18) synchronously.
	Field<int32_t>(state, versus::kResultPhase) = versus::kNoResult;
	reinterpret_cast<RestartFn>(s_base + sfv::kRestartBattle.rva)(state);
	Log("match end: rematch");
}

bool LeaveBattle()
{
	using GetClassFn = uint8_t* (*)(const wchar_t* package);
	uint8_t* mainMenu = reinterpret_cast<GetClassFn>(s_base + sfv::kMainMenuClass.rva)(L"/Script/KiwiGame");
	if (!mainMenu || !ChangeScene(mainMenu, nullptr)) return false;
	Log("match end: back to the main menu");
	return true;
}

} // namespace sfve::mod
