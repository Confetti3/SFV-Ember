#pragma once

// A battle Ember's room launched, as the mod's netplay mode plays it
// (EMBER_LIBRARY.md sections 12, 14 and 19): everything the battle reports to
// the runtime (RuntimeEvents) and the room's part of its session's start. The
// mode owns the battle and its rollback session; this module owns whether
// the battle is the room's and what the room has heard of it.
//
// A battle is the room's when the room's own launch loaded it
// (VersusLaunch's TakeLinkedBattle), whatever the room link holds as it
// binds: a battle the player starts from SFV's menus or the pipe's "launch"
// while in a room plays offline, and a room battle whose room closed while
// it loaded still leaves as the room's.
//
// Its WIN (section 19): on the confirmed frame a game ends, the battle stands
// still and the session goes on in held frames, which carry both fighters'
// answers to Ember's post-match prompt (RematchDecision.hxx). Both copies
// read them on the same confirmed frames and decide alike: the game's own
// restart in this battle and session (the room hears the game as one of a
// running match, continued), or back to the room (the room hears the match's
// end).
//
// A room member who watches the table (EMBER_LIBRARY.md section 25) plays
// the same battle from P1's stream of the fighters' confirmed frames. It
// reports no result and answers no prompt: it takes each game's end and
// holds its WIN on the same frames the fighters do, reads their answers from
// the same held frames, and goes on in place or back to the room with them.
// P1 streams to its watchers, drops the ones that cannot keep up, and keeps
// its session for them after the battle while the runtime drains them.
//
// Game thread only, but for DeclineRematch and NoteShellLeftPrompt.

#include "core/RollbackSession.hxx"
#include "ember/RematchDecision.hxx"

#include "common/RoomRules.hxx"
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
	// The room's battle is a Practice room's (its launch said so).
	bool Practice() const { return active_ && practice_; }

	enum class Start { Waiting, Ready, Gone };
	// The room's part of the session's start, once every member's battle has
	// loaded: this copy's side and ports (P1's watchers too), or the stream
	// it watches, the seed and the delay. Waiting until then; Gone when the
	// room's match ended first, is not one this copy can play or watch, or
	// left this watcher out. Ready also hands the local port to GGPO, so the
	// session starts next.
	Start SessionConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay);
	// The session started, or could not.
	void NoteSessionStart(bool started);
	// This copy watches the room's battle rather than fighting it (known
	// once SessionConfig said Ready).
	bool Watching() const { return active_ && watching_; }
	// The delay a watched game was granted with (room::WatchedGameDelay),
	// which a watcher's HUD shows as SF4's does: frames, and whether each
	// fighter plays at their own.
	int WatchedDelay() const { return watchedDelay_; }
	bool WatchedOwnDelays() const { return watchedOwnDelays_; }

	// Each frame P1's session runs, and while it is kept for its watchers:
	// the watchers it dropped or lost have their links ended, so each one's
	// copy gives up at once (SF4's EndRuntimeSpectatorStream). Any battle,
	// the room's or not.
	static void EndDroppedWatcherLinks(RollbackSession& session);

	// Each live frame: the session's phase as the runtime last heard it.
	void NoteSyncPhase(const RollbackSession& session);
	// Each frame the battle plays, and once more when its session is gone:
	// the player hears of the link as SF4 Ember tells them (its GGPO events),
	// an unstable connection as it starts (a warning), its return (a note),
	// and the opponent dropped (an error). Nothing once the game's result is
	// taken: the other copy may leave the WIN first.
	void NoteLink(const RollbackSession& session);
	// The match's result was taken and the battle holds it (TakeEnd), until
	// a rematch goes on from it: SF4's "result known", which silences the link.
	bool ResultTaken() const { return active_ && matchEnded_; }
	// The room's match ended and waits for a confirmed frame.
	bool EndPending() const;
	// On a confirmed frame, with the match's end pending: takes it and holds
	// the battle for the answers over its WIN (Prompting); true then. An end
	// the game left undecided is logged and dropped, and the battle steps on
	// (false).
	bool TakeEnd(const mod::SfvAdapter& game, const RollbackSession& session);

	// The battle stands still over its WIN, for the answers.
	bool Prompting() const { return prompt_.active; }
	// This copy's bits for the next held frame (RematchDecision's LocalBits).
	PackedInput AnswerBits();
	// Each frame the fight steps: the runtime reads the fighter's input as
	// SF4's battle has it read every frame, which restarts its prompt timer
	// between WINs.
	void NoteFightFrame();
	using Answer = rematch_in_place::Answer;
	// Inside a held frame, with both fighters' confirmed inputs, `held`
	// frames into the WIN: what both copies do. On Rematch the game restarts
	// here, before the next frame is saved, with the session's random
	// generators reseeded from `seed` and the game's number.
	Answer HeldFrame(const FrameInputs& inputs, int held, mod::SfvAdapter& game, std::uint32_t seed);
	// After that held frame's step: the room hears what was decided.
	void Decided(Answer answer, const RollbackSession& session);
	// The battle leaves another way (its session broke, the runtime ended
	// it) with a result still held: the room hears it as the match's end.
	void FlushHeld(const char* why);

	// The main menu is back: the runtime hears how the battle ended, and the
	// session closes. True when P1 keeps it instead for its watchers (the
	// runtime's drain, which decides as the match's end is heard): the caller
	// keeps it polled until the runtime retires it (SetWatcherDrain). Nothing
	// for a battle that is not the room's.
	bool Close(RollbackSession* session);

	// The adapter's view (SfvGame's Battle): the result held for the prompt,
	// the WIN being shown, each side's Rematch as the inputs carry it, and
	// the games this battle played and went on from.
	bool ResultHeld() const { return prompt_.active && prompt_.offered && !prompt_.reported && !watching_; }
	bool ResultScreenShown() const { return prompt_.active; }
	unsigned RematchChoices() const { return prompt_.active ? prompt_.choices : 0; }
	// Each side whose input declined, from the held frame that decided on it
	// (a decline leaves at once) until the next WIN or battle.
	unsigned RematchDeclines() const { return declines_; }
	unsigned GamesInPlace() const { return gamesInPlace_; }

	// The last room match's result, as JSON ("null" before one).
	const std::string& LastResult() const { return lastResult_; }
	// The WIN's state, as JSON, for the battle status.
	std::string DescribeRematch() const;

private:
	void Report(bool continued);
	// SessionConfig for a watcher's slot: P1's stream.
	Start WatchConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay);

	struct Prompt {
		bool active = false;
		bool offered = false;   // this copy's room offers the rematch in place
		bool reported = false;  // the room has heard this game's result
		sf4e::room::MatchResult result = sf4e::room::MatchResult::Abort;
		std::uint64_t captureFrame = 0, confirmedFrame = 0;
		std::uint8_t* versus = nullptr; // the versus battle's scene state, for the restart
		unsigned choices = 0;           // last held frame's Rematch per side
		std::uint32_t bits[2] = {};     // last held frame's answer bits
		int held = 0;
	};
	bool active_ = false;      // the room's battle, until its close is reported
	bool practice_ = false;    // ...a Practice room's
	bool watching_ = false;    // ...which this copy watches
	int watchedDelay_ = -1;    // the watched game's granted delay (WatchedDelay)
	bool watchedOwnDelays_ = false;
	bool streamLossTold_ = false; // a watcher's stream failure was reported
	bool matchEnded_ = false;  // its match reached its end (the result was taken)
	Prompt prompt_;
	unsigned declines_ = 0;    // RematchDeclines
	unsigned gamesInPlace_ = 0;
	bool linkWarned_ = false;  // the warning NoteLink last told of
	bool peerLossTold_ = false;
	sf4e::GgpoSyncPhase phase_ = sf4e::GgpoSyncPhase::None; // as last reported
	sf4e::NetplayFacade::RuntimeMatchEndpoints endpoints_;
	std::string lastResult_ = "null";
	std::string lastAnswer_ = "none";
};

// The game's result as the room counts it: the one taken on the last match
// end's confirmed frame, Abort before one and after a rematch went on from it.
// It outlasts the battle (the room hears it after the battle has left) until
// the next battle binds. Game thread.
sf4e::room::MatchResult RoomGameResult();

// This copy's fighter declines the rematch over the WIN now showing: the
// pipe's "rematch no" (any thread; false when no WIN is showing), and the
// shell's Back to room or Character select (render thread, every frame:
// ApplicationShell::PostMatchLeft).
bool DeclineRematch();
void NoteShellLeftPrompt(bool left);

} // namespace sfve::adapter
