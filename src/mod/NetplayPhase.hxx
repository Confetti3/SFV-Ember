#pragma once

// Where a netplay battle is in its life (NetplayMode): one value, moved only
// by the events below (Next), in place of four flags (started, prepared,
// leave wanted, leaving) whose combinations were not all meaningful. Pure: no
// game, no clock, no session.
//
//   Unbound --BattleBound--> Preparing --Prepared--> Agreeing --SessionStarted--> Playing
//                            Preparing --NoSession------------------------------> Playing
//   any bound phase --LeaveRefused--> Leaving, --LeaveTaken--> Left
//   Leaving, Left --BackAtMenu--> Playing        any --Drained--> Unbound
//   any --BattleBound--> Preparing
//
// What a Playing battle does on a frame (fight, held WIN, the match's end,
// watching, a replay, offline) is decided each frame from its session and
// its room (RoomBattle's own state), not stored here.

#include <stdint.h>

#include <optional>

namespace sfve::mod::netplay {

enum class Phase : uint8_t {
	// No battle bound: none yet, or the last one's session went to the
	// watchers' drain. The game runs as it is.
	Unbound,
	// A battle bound; the match it plays is not known yet (PrepareStart waits).
	Preparing,
	// The session's settings made, the start state set; the copies agree on
	// their start state before GGPO binds the port (StartAgreement).
	Agreeing,
	// The session started, or none was asked for (offline): frames are played.
	Playing,
	// Back to the main menu asked; the scene manager has not taken it yet, so
	// it is asked again each frame. The battle stands still.
	Leaving,
	// The scene manager took the leave; the battle stands still until the
	// main menu replaces it.
	Left,
};

enum class Event : uint8_t {
	BattleBound,    // NewBattle
	Prepared,       // PrepareStart: the match known, the session's settings made
	NoSession,      // PrepareStart: this battle plays with no session
	SessionStarted, // StartSession: the start agreed (or none); the session started or failed to
	LeaveRefused,   // Leave: the scene manager could not take it yet
	LeaveTaken,     // Leave: the scene manager took it
	BackAtMenu,     // PollBattle: the room's battle closed with the main menu in front
	Drained,        // PollBattle: the battle's session went to the watchers' drain
};

// The phase `event` moves `from` to; none for an event that phase never sees.
constexpr std::optional<Phase> Next(Phase from, Event event)
{
	switch (event) {
	case Event::BattleBound: return Phase::Preparing;
	case Event::Prepared:
		if (from == Phase::Preparing) return Phase::Agreeing;
		break;
	case Event::NoSession:
		if (from == Phase::Preparing) return Phase::Playing;
		break;
	case Event::SessionStarted:
		if (from == Phase::Agreeing) return Phase::Playing;
		break;
	case Event::LeaveRefused:
		if (from != Phase::Unbound) return Phase::Leaving;
		break;
	case Event::LeaveTaken:
		if (from != Phase::Unbound) return Phase::Left;
		break;
	case Event::BackAtMenu: return from == Phase::Leaving || from == Phase::Left ? Phase::Playing : from;
	case Event::Drained: return Phase::Unbound;
	}
	return std::nullopt;
}

// The battle goes back to the main menu (asked, or taken).
constexpr bool LeaveWanted(Phase phase)
{
	return phase == Phase::Leaving || phase == Phase::Left;
}

constexpr const char* Name(Phase phase)
{
	switch (phase) {
	case Phase::Unbound: return "unbound";
	case Phase::Preparing: return "preparing";
	case Phase::Agreeing: return "agreeing";
	case Phase::Playing: return "playing";
	case Phase::Leaving: return "leaving";
	case Phase::Left: return "left";
	}
	return "?";
}

constexpr const char* Name(Event event)
{
	switch (event) {
	case Event::BattleBound: return "battle bound";
	case Event::Prepared: return "prepared";
	case Event::NoSession: return "no session";
	case Event::SessionStarted: return "session started";
	case Event::LeaveRefused: return "leave refused";
	case Event::LeaveTaken: return "leave taken";
	case Event::BackAtMenu: return "back at the menu";
	case Event::Drained: return "drained";
	}
	return "?";
}

} // namespace sfve::mod::netplay
