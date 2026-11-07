#pragma once

// Street Fighter V under Ember's runtime: the library's game adapter
// (sf4e/sf4e__GameAdapter.hxx), defined once by SfvGame.cxx as
// ember::game::Game(), over sfv-ember's own modules. Of the optional
// features SFV offers watching a table's games (Capabilities::spectators),
// Training (Capabilities::training: SFV's own Training mode, SfvTraining) and
// replays of the room battles it recorded (Capabilities::replays, SfvReplays).
//
// The adapter's battle and the desync check (SfvVerification) read the
// battle the mod is playing, which the mod's netplay mode hands over here
// for a room's battle: its rollback session, the bound battle and the
// battle's way out. With none in play they answer as with no battle.

#include "ember/SfvCatalog.hxx"

#include <cstdint>
#include <functional>
#include <optional>

namespace sfve {
class RollbackSession;
}
namespace sfve::mod {
class SfvAdapter;
}

namespace sfve::adapter {

class RoomBattle;

struct BattleInPlay {
	const RollbackSession* session = nullptr; // the battle's rollback session, live or not
	const mod::SfvAdapter* game = nullptr;    // the bound battle
	const RoomBattle* room = nullptr;         // what the room hears of it, and its WIN
	// Ends the session and takes the battle back to the main menu, for the
	// runtime's Retire, Abort and Fail (NetplayMode's exit). `why` is for the
	// log. Game thread; it only asks, so it is safe from any runtime call.
	std::function<void(const char* why)> exit;
};

// The room's match the runtime holds now, as SfvBattle::Enter launches it:
// both picks, the stage and the rules; empty with no session client. Game
// thread.
std::optional<RoomMatch> RoomMatchNow();

// Game thread: the mod sets it as a room's battle binds and clears it (an
// empty BattleInPlay) before that battle's session or objects go. Setting
// it also forgets the room's sync of the battle before.
void SetBattleInPlay(const BattleInPlay& battle);
const BattleInPlay& CurrentBattle();

// Game thread: the battle in play is a Practice room's and its session is
// live (Training::MatchPracticeActive, EMBER_LIBRARY.md section 27).
bool PracticeBattleInPlay();

// Game thread: P1's session, kept after its battle closed for the watchers
// still streaming (RoomBattle::Close), until the runtime retires it or ends
// the drain, which close it; null once the mod has freed it. The adapter's
// battle is Live while it is.
void SetWatcherDrain(RollbackSession* session);

// Game thread, as the game exits (EmberHost): the settings still queued are
// written, and the preferences' writer stops.
void StopPreferences();

// Every member's battle has loaded (the room client's Battle::OnSessionSynced
// since the battle in play was set): its rollback session may start. Game
// thread.
bool SessionSynced();

// The launch a room's match armed (Battle::Enter) until the runtime drops it
// (Battle::DropNextBattle). Each entry arms a new ticket, and the launch,
// which waits for the main menu, goes ahead only while its ticket is still
// the armed one: a player who left the room, or a later entry, stops it.
// Game thread.
class LaunchArming {
public:
	enum class Ticket : std::uint64_t { None = 0 };

	Ticket Arm()
	{
		armed_ = Ticket{++armedCount_};
		return armed_;
	}
	void Drop() { armed_ = Ticket::None; }
	bool Armed(Ticket ticket) const { return ticket != Ticket::None && ticket == armed_; }

private:
	std::uint64_t armedCount_ = 0;
	Ticket armed_ = Ticket::None;
};

} // namespace sfve::adapter
