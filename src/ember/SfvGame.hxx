#pragma once

// Street Fighter V under Ember's runtime: the library's game adapter
// (sf4e/sf4e__GameAdapter.hxx), defined once by SfvGame.cxx as
// ember::game::Game(), over sfv-ember's own modules. SFV offers none of the
// optional features yet (every Capabilities flag is off), and its Training
// and replays are the library's absent ones.
//
// The adapter's battle and the desync check (SfvVerification) read the
// battle the mod is playing, which the mod's netplay mode hands over here:
// its rollback session and the bound battle. Until NetplayMode does that
// (the step after this one), no battle is in play and they answer as with
// no battle.

namespace sfve {
class RollbackSession;
}
namespace sfve::mod {
class SfvAdapter;
}

namespace sfve::adapter {

struct BattleInPlay {
	const RollbackSession* session = nullptr; // the battle's rollback session, live or not
	const mod::SfvAdapter* game = nullptr;    // the bound battle
};

// Game thread: the mod sets it as a battle binds and clears it (an empty
// BattleInPlay) before that battle's objects go.
void SetBattleInPlay(const BattleInPlay& battle);
const BattleInPlay& CurrentBattle();

} // namespace sfve::adapter
