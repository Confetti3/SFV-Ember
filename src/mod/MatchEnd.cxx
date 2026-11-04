#include "mod/MatchEnd.hxx"

#include "mod/GameHooks.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <atomic>

namespace sfve::mod {

namespace {

namespace versus = sfv::versus;

using ShowResultFn = void (*)(uint8_t* versusBattleState);
using RestartFn = void (*)(uint8_t* versusBattleState);

uintptr_t s_base;
ShowResultFn o_showResult;
std::atomic<bool> s_linked;
uint8_t* s_ended; // game thread only

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
}

uint8_t* TakeEndedVersusBattle()
{
	uint8_t* ended = s_ended;
	s_ended = nullptr;
	return ended;
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

} // namespace sfve::mod
