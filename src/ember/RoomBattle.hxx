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

	// The main menu is back: the session closes and the runtime hears how
	// the battle ended. Nothing for a battle that is not the room's.
	void Close(RollbackSession* session);

	// The adapter's view (SfvGame's Battle): the result held for the prompt,
	// the WIN being shown, each side's Rematch as the inputs carry it, and
	// the games this battle played and went on from.
	bool ResultHeld() const { return prompt_.active && prompt_.offered && !prompt_.reported; }
	bool ResultScreenShown() const { return prompt_.active; }
	unsigned RematchChoices() const { return prompt_.active ? prompt_.choices : 0; }
	unsigned GamesInPlace() const { return gamesInPlace_; }

	// The last room match's result, as JSON ("null" before one).
	const std::string& LastResult() const { return lastResult_; }
	// The WIN's state, as JSON, for the battle status.
	std::string DescribeRematch() const;

private:
	void Report(bool continued);

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
	bool matchEnded_ = false;  // its match reached its end (the result was taken)
	Prompt prompt_;
	unsigned gamesInPlace_ = 0;
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
