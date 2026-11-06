#pragma once

// A battle Ember's room launched, as the mod's netplay mode plays it
// (EMBER_LIBRARY.md sections 12 and 14): everything the battle reports to the
// runtime (RuntimeEvents) and the room's part of its session's start. The
// mode owns the battle and its rollback session; this module owns whether
// the battle is the room's and what the room has heard of it.
//
// A battle is the room's when the room's own launch loaded it
// (VersusLaunch's TakeLinkedBattle), whatever the room link holds as it
// binds: a battle the player starts from SFV's menus or the pipe's "launch"
// while in a room plays offline, and a room battle whose room closed while
// it loaded still leaves as the room's.
//
// Game thread only.

#include "core/RollbackSession.hxx"

#include "sf4e/sf4e__RuntimeEvents.hxx"

#include <cstdint>
#include <functional>
#include <string>

namespace sfve::mod {
class SfvAdapter;
}

namespace sfve::adapter {

class RoomBattle {
public:
	// A battle binds with its new session: it becomes the room's when the
	// room's launch loaded it, and the runtime then hears it has loaded.
	// `exit` is the battle's way out for the runtime's Retire, Abort and
	// Fail (it only asks). A room battle not yet closed (the main menu never
	// came back between) is closed first, with `previous`, its session.
	void Bind(RollbackSession* previous, RollbackSession& session, const mod::SfvAdapter& game,
		std::function<void(const char* why)> exit);
	bool Active() const { return active_; }

	enum class Start { Waiting, Ready, Gone };
	// The room's part of the session's start, once every member's battle has
	// loaded: this copy's side and ports, the seed and the delay. Waiting
	// until then; Gone when the room's match ended first or is not one this
	// copy fights. Ready also hands the local port to GGPO, so the session
	// starts next.
	Start SessionConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay);
	// The session started, or could not.
	void NoteSessionStart(bool started);

	// Each live frame: the session's phase as the runtime last heard it.
	void NoteSyncPhase(const RollbackSession& session);
	// The room's match ended and waits for a confirmed frame.
	bool EndPending() const;
	// On a confirmed frame, with the match's end pending: takes it and sends
	// its result to the room; true when the battle now goes back to the
	// room. An end the game left undecided is logged and dropped, and the
	// battle steps on.
	bool TakeEnd(const mod::SfvAdapter& game, const RollbackSession& session);

	// The main menu is back: the session closes and the runtime hears how
	// the battle ended. Nothing for a battle that is not the room's.
	void Close(RollbackSession* session);

	// The last room match's result, as JSON ("null" before one).
	const std::string& LastResult() const { return lastResult_; }

private:
	bool active_ = false;      // the room's battle, until its close is reported
	bool matchEnded_ = false;  // its match reached its end (the result was taken)
	sf4e::GgpoSyncPhase phase_ = sf4e::GgpoSyncPhase::None; // as last reported
	sf4e::NetplayFacade::RuntimeMatchEndpoints endpoints_;
	std::string lastResult_ = "null";
};

} // namespace sfve::adapter
