#include "ember/RoomBattle.hxx"

#include "ember/SfvGame.hxx"
#include "mod/GameInput.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/MatchResult.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/GameCatalog.hxx"
#include "common/InputDelay.hxx"
#include "common/Localization.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "sf4e/sf4e__RuntimeSession.hxx"
#include "session/RoomQueries.hxx"

#include <atomic>
#include <cstdio>

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
namespace rip = rematch_in_place;
using mod::Log;

// The answers ride above every bit a pad makes, so the game never sees them
// (Restricted to InputBits before each frame).
static_assert((rip::kBits & mod::input::kInputBits) == 0, "the WIN's answer bits overlap a pad's");

sf4e::room::MatchResult s_gameResult = sf4e::room::MatchResult::Abort; // game thread

// A WIN is showing (game thread writes; the pipe reads), and this fighter's
// decline of it: the pipe's, latched for that WIN, and the shell's, as its
// prompt says each frame.
std::atomic<bool> s_prompting{false};
std::atomic<bool> s_pipeDeclined{false};
std::atomic<bool> s_shellLeft{false};

// The game's own judgement of the match (MatchResult), as the room counts it.
// One the game left undecided is no result (Abort).
sf4e::room::MatchResult RoomResult(const mod::MatchResult& result)
{
	if (!result.decided) return sf4e::room::MatchResult::Abort;
	return result.winner == 0 ? sf4e::room::MatchResult::P1Win
		: result.winner == 1  ? sf4e::room::MatchResult::P2Win
							  : sf4e::room::MatchResult::Draw;
}

const char* AnswerName(rip::Answer answer)
{
	switch (answer) {
	case rip::Answer::Wait: return "wait";
	case rip::Answer::Rematch: return "rematch";
	case rip::Answer::Leave: return "leave";
	}
	return "?";
}

} // namespace

sf4e::room::MatchResult RoomGameResult()
{
	return s_gameResult;
}

bool DeclineRematch()
{
	if (!s_prompting) return false;
	s_pipeDeclined = true;
	return true;
}

void NoteShellLeftPrompt(bool left)
{
	s_shellLeft = left;
}

void RoomBattle::Bind(RollbackSession* previous, RollbackSession& session, const mod::SfvAdapter& game,
	std::function<void(const char* why)> exit)
{
	if (active_) Close(previous);
	active_ = mod::TakeLinkedBattle(&practice_);
	watching_ = false;
	replay_ = false;
	watchedDelay_ = -1;
	watchedOwnDelays_ = false;
	streamLossTold_ = false;
	matchEnded_ = false;
	prompt_ = {};
	scenes_ = {};
	declines_ = 0;
	s_prompting = false;
	gamesInPlace_ = 0;
	linkWarned_ = peerLossTold_ = false;
	s_gameResult = sf4e::room::MatchResult::Abort;
	if (!active_) return;
	SetBattleInPlay({&session, &game, this, std::move(exit)});
	facade::ClearBattleState();
	facade::CancelDeferredGgpoClose();
	facade::ResetGgpoBattleWatch();
	// A new game starts clean, as SF4's session start does: an Error (a
	// cancelled game, a refused Ready, the last desync) stays until something
	// clears it, and the match HUD would show it over this game.
	facade::ClearMatchNotice();
	phase_ = sf4e::GgpoSyncPhase::Starting;
	facade::NotifyRuntimeBattleLoaded();
	Log("netplay: the room's battle has loaded; waiting for every member's");
}

void RoomBattle::BindReplay()
{
	// Bind ran first and found no room's battle, so nothing of a room is set.
	active_ = watching_ = replay_ = true;
	endpoints_ = {};
	endpoints_.localSlot = 2;
	Log("netplay: a replay's battle has loaded");
}

RoomBattle::Start RoomBattle::SessionConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay)
{
	if (!facade::IsRuntimeMatchLive()) {
		Log("netplay: the room's match ended before its session started");
		return Start::Gone;
	}
	if (!SessionSynced() || !facade::GetRuntimeMatchEndpoints(endpoints_)) return Start::Waiting;
	const std::size_t slot = endpoints_.localSlot;
	// Two fighters in slots 0 and 1, and the watchers after them.
	if (endpoints_.participantCount < 2 || endpoints_.participantCount > endpoints_.remotePorts.size() ||
		slot >= endpoints_.participantCount) {
		Log("netplay: the room's match is not one this copy can play (slot %zu of %zu)", slot, endpoints_.participantCount);
		return Start::Gone;
	}
	const auto& netplay = ember::RuntimeSession::netplay;
	if (!netplay) {
		Log("netplay: the room's match has no session client any more");
		return Start::Gone;
	}
	const auto& match = netplay->client._matchData;
	if (slot >= 2) return WatchConfig(config, seed, delay);
	if (!endpoints_.remotePorts[1 - slot]) {
		Log("netplay: the room's match has no link to the other fighter (slot %zu)", slot);
		return Start::Gone;
	}
	// The delay the room granted this seat, as SF4's start checks it (UserApp).
	if (match.inputDelay[slot] > sf4e::InputDelayWireCeiling) {
		Log("netplay: the room granted a delay of %d, past the wire's %d", (int)match.inputDelay[slot], sf4e::InputDelayWireCeiling);
		return Start::Gone;
	}
	if (!facade::BindRuntimeInput((int)slot)) {
		Log("netplay: this copy's pad could not be bound to its side");
		return Start::Gone;
	}
	config->localPlayer = (int)slot;
	config->localPort = endpoints_.localPort;
	config->remotePort = endpoints_.remotePorts[1 - slot];
	// P1 streams to every watcher whose link came up before the start; one
	// without a port sits this game out rather than hold the fighters (SF4's
	// StartRuntimeGgpo). They wait for each other only so long.
	if (slot == 0) {
		for (std::size_t watcher = 2; watcher < endpoints_.participantCount; watcher++) {
			if (endpoints_.remotePorts[watcher]) config->watchers.push_back({watcher, endpoints_.remotePorts[watcher]});
		}
	}
	config->watcherSyncDeadlineMs = netplay->client.SpectatorSyncDeadlineMs();
	*seed = match.rngSeed;
	*delay = match.inputDelay[slot];
	facade::ReportGgpoTransport(config->remoteIp.c_str(), config->remotePort);
	Log("netplay: room match as P%zu, local port %u, the peer's port %u, delay %d, %zu watcher(s)", slot + 1,
		config->localPort, config->remotePort, *delay, config->watchers.size());
	// The room's match session holds the local port until GGPO takes it.
	facade::ReleaseRuntimePortToGgpo();
	return Start::Ready;
}

RoomBattle::Start RoomBattle::WatchConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay)
{
	watching_ = true;
	const std::size_t slot = endpoints_.localSlot;
	if (!endpoints_.remotePorts[0]) {
		// P1 left this watcher out (its link did not come up before the start,
		// or P1 dropped it as the battle loaded): it watches the next game.
		// Nothing is lost of a game whose end the room already has.
		Log("netplay: no stream from P1 for this watcher (slot %zu); it watches the next game", slot);
		if (!facade::IsRuntimeMatchEndCommitted()) {
			facade::PushAlert(sf4e::loc::T("runtime.spectator_stream_lost"), sf4e::NoticeSeverity::Error);
		}
		return Start::Gone;
	}
	const auto& netplay = ember::RuntimeSession::netplay;
	const auto& match = netplay->client._matchData;
	// The HUD shows the delay the fighters were granted, as SF4's does.
	const auto granted = sf4e::room::WatchedGameDelay(netplay->client.GetRoomSnapshot().delayPolicy, match.inputDelay[0],
		match.inputDelay[1]);
	watchedDelay_ = granted.frames;
	watchedOwnDelays_ = granted.own;
	config->watching = true;
	config->localPort = endpoints_.localPort;
	config->remotePort = endpoints_.remotePorts[0];
	*seed = match.rngSeed;
	*delay = 0;
	facade::ReportGgpoTransport(config->remoteIp.c_str(), config->remotePort);
	Log("netplay: room match watched from slot %zu, local port %u, P1's port %u (the fighters' delay %d%s)", slot,
		config->localPort, config->remotePort, watchedDelay_, watchedOwnDelays_ ? ", their own" : "");
	facade::ReleaseRuntimePortToGgpo();
	return Start::Ready;
}

void RoomBattle::EndDroppedWatcherLinks(RollbackSession& session)
{
	for (const std::size_t slot : session.TakeDroppedWatchers()) facade::EndRuntimeSpectatorStream(slot);
}

void RoomBattle::NoteSessionStart(bool started)
{
	if (started) {
		facade::NoteRuntimeMatchStart(endpoints_);
	} else {
		facade::PushAlert(sf4e::loc::T("runtime.authorized_connection_unavailable"));
	}
}

void RoomBattle::NoteLink(const RollbackSession& session)
{
	if (!active_) return;
	if (watching_) {
		// A watcher hears only of its own stream, as SF4's spectator does: lost
		// before the game's end is known, it loses this game but not its place
		// at the table, and the runtime leaves the game. Nothing is lost of a
		// game whose end the room already has; a watcher who left closed its
		// session without a failure. A replay's end is no room's business.
		if (!session.Failed() || streamLossTold_) return;
		streamLossTold_ = true;
		Log("netplay: the %s ended: %s", replay_ ? "replay" : "watched stream", session.FailureReason().c_str());
		if (replay_ || matchEnded_ || facade::IsRuntimeMatchEndCommitted()) return;
		facade::NotifyRuntimeSpectatorStreamFailed();
		facade::PushAlert(sf4e::loc::T("runtime.spectator_stream_lost"), sf4e::NoticeSeverity::Error);
		return;
	}
	const bool warned = session.ConnectionWarning() && !matchEnded_;
	if (warned != linkWarned_) {
		linkWarned_ = warned;
		if (warned) {
			facade::PushAlert(sf4e::loc::T("runtime.connection_unstable_prediction"), sf4e::NoticeSeverity::Warning);
		} else if (session.Live() && !session.Failed() && !matchEnded_) {
			// GGPO resumed the link (not a drop, nor the result's silence).
			facade::PushAlert(sf4e::loc::T("runtime.connection_restored"), sf4e::NoticeSeverity::Info);
		}
	}
	if (session.PeerDisconnected() && !peerLossTold_) {
		peerLossTold_ = true;
		if (!matchEnded_) facade::PushAlert(sf4e::loc::T("runtime.opponent_disconnected"), sf4e::NoticeSeverity::Error);
	}
}

void RoomBattle::NoteSyncPhase(const RollbackSession& session)
{
	if (!active_ || replay_) return;
	const sf4e::GgpoSyncPhase now = session.Running() ? sf4e::GgpoSyncPhase::Running : sf4e::GgpoSyncPhase::Synchronizing;
	if (now == phase_) return;
	phase_ = now;
	facade::NotifyGgpoSyncPhase(now);
}

bool RoomBattle::EndPending() const
{
	return active_ && !prompt_.active && mod::EndedVersusBattlePending();
}

bool RoomBattle::TakeEnd(const mod::SfvAdapter& game, const RollbackSession& session)
{
	uint8_t* versus = mod::TakeEndedVersusBattle(game.BattleData());
	if (!versus) return false;
	const std::optional<mod::MatchResult> decided = mod::DecidedMatchResult();
	const int frame = session.Frame();
	const int confirmed = session.LastConfirmedFrame();
	if (!decided || !decided->decided) {
		// Both copies read the same state here, so both drop it on this frame.
		Log("netplay: the match ended undecided at frame %d (confirmed %d); no result for the room, the battle steps on",
			frame, confirmed);
		return false;
	}
	lastResult_ = mod::DescribeMatchResult(*decided);
	prompt_ = {};
	scenes_ = {};
	scenes_.taken = true; // the prompt over the scenes, if any, goes on over this WIN
	declines_ = 0;
	prompt_.active = true;
	prompt_.result = RoomResult(*decided);
	prompt_.captureFrame = (std::uint64_t)frame;
	prompt_.confirmedFrame = (std::uint64_t)(confirmed < 0 ? 0 : confirmed);
	prompt_.versus = versus;
	if (!replay_) s_gameResult = prompt_.result; // the room's battle's result (RoomGameResult)
	matchEnded_ = true;
	if (watching_) {
		// A watcher has no answer of its own and nothing to report: it holds
		// the WIN as the fighters do and follows their answers.
		Log("netplay: match result %s at frame %d, game %u of this battle; the WIN follows the fighters' answers",
			lastResult_.c_str(), frame, gamesInPlace_ + 1);
		return true;
	}
	// Whether this copy's room offers the next game in this battle: the
	// table's Instant rematch, and a set the game does not end. The copies'
	// rooms may not agree yet, so the offer is one of the answers the held
	// frames exchange: a copy that offers none declines.
	prompt_.offered = ember::Catalog().rematchInPlace && facade::RematchInPlaceOffered(prompt_.result);
	s_pipeDeclined = false;
	s_prompting = true;
	Log("netplay: match result %s at frame %d (confirmed %d), game %u of this battle; %s", lastResult_.c_str(), frame,
		confirmed, gamesInPlace_ + 1,
		prompt_.offered ? "the WIN waits for both fighters' answers" : "no rematch in place offered; back to the room");
	// With nothing to hold it for, the room hears the result at once, as SF4's
	// does (NativeResult); the held frames only carry the decline.
	if (!prompt_.offered) Report(false);
	return true;
}

void RoomBattle::NoteDecided(const mod::SfvAdapter& game, const RollbackSession& session)
{
	// A watcher and a replay have no prompt; a new game (a rematch reset the
	// match data) or a rollback that undid the decision starts it over.
	if (!active_ || watching_ || replay_ || !game.MatchDecided()) {
		scenes_ = {};
		return;
	}
	if (prompt_.active || scenes_.active || scenes_.taken) return;
	const int frame = session.Frame() - 1; // the frame just stepped
	if (scenes_.seen < 0) scenes_.seen = frame;
	// Opened only once the frame it was first seen decided on is confirmed,
	// so a decision a misprediction made never opens it.
	if (session.LastConfirmedFrame() < scenes_.seen) return;
	const mod::MatchResult decided = mod::ReadMatchResult(game.BattleData());
	if (!decided.decided) {
		scenes_.seen = -1;
		return;
	}
	scenes_.active = true;
	const sf4e::room::MatchResult result = RoomResult(decided);
	scenes_.offered = ember::Catalog().rematchInPlace && facade::RematchInPlaceOffered(result);
	s_gameResult = result; // the prompt's score and seat rules read it (RoomGameResult); TakeEnd sets it again
	Log("netplay: the match is decided (seen at frame %d, confirmed %d), game %u of this battle; %s", scenes_.seen,
		session.LastConfirmedFrame(), gamesInPlace_ + 1,
		scenes_.offered ? "the rematch prompt opens while the game plays its scenes" : "no rematch in place offered");
}

void RoomBattle::NoteFightFrame()
{
	// SF4 reads the fighter's input through the runtime every frame; the
	// runtime's prompt timer (how long a fighter has been asked) starts over
	// only on a read with no prompt due, so a later WIN's asks its full time.
	unsigned mapped = 0, raw = 0;
	if (active_ && !watching_ && !prompt_.active) facade::ReadRuntimeMatchInput((int)endpoints_.localSlot, mapped, raw);
}

PackedInput RoomBattle::AnswerBits()
{
	if (watching_) return 0; // a watcher sends nothing
	unsigned mapped = 0, raw = 0;
	// The runtime's answer: a parked Rematch, or the fighter still asked.
	if (prompt_.offered && !prompt_.reported) facade::ReadRuntimeMatchInput((int)endpoints_.localSlot, mapped, raw);
	return rip::LocalBits(prompt_.offered && !prompt_.reported, s_pipeDeclined || s_shellLeft || prompt_.exiting, raw);
}

bool RoomBattle::DeclineToExit()
{
	if (!prompt_.active || watching_) return false;
	if (!prompt_.exiting) {
		prompt_.exiting = true;
		Log("netplay: the room asks the battle to end over the WIN; this fighter declines, and both copies leave on "
			"the frame that reads it");
	}
	return true;
}

RoomBattle::Answer RoomBattle::HeldFrame(const FrameInputs& inputs, int held, mod::SfvAdapter& game, std::uint32_t seed)
{
	prompt_.held = held;
	prompt_.bits[0] = inputs.p[0] & rip::kBits;
	prompt_.bits[1] = inputs.p[1] & rip::kBits;
	prompt_.choices = rip::Choices(inputs.p[0], inputs.p[1]);
	const Answer answer = rip::Decide(inputs.p[0], inputs.p[1], held);
	if (answer != Answer::Rematch) return answer;
	// The game's own Continue, then the same seeds on both copies: the
	// restart takes no clock reading the other copy would not make, and the
	// flow record mirrors the restarted flow before this frame's save.
	mod::Rematch(prompt_.versus);
	const std::uint32_t gameSeed = seed ^ (0x9E3779B9u * (gamesInPlace_ + 1));
	Log("netplay: rematch in place (held frame %d); reseeded %d random generator(s)", held, game.ReseedRandom(gameSeed));
	game.Recapture();
	return answer;
}

void RoomBattle::Decided(Answer answer, const RollbackSession& session)
{
	if (answer == Answer::Wait) return;
	lastAnswer_ = AnswerName(answer);
	char bits[64];
	snprintf(bits, sizeof(bits), "P1 %08X, P2 %08X", (unsigned)prompt_.bits[0], (unsigned)prompt_.bits[1]);
	Log("netplay: the WIN's answers (%s) after %d held frame(s), frame %d: %s", bits, prompt_.held, session.Frame(),
		answer == Answer::Rematch ? "rematch in place" : "back to the room");
	if (answer == Answer::Rematch) {
		// A watcher's HUD counts the game it saw end (SF4's
		// NotifyRuntimeWatchedGame); the fighters report it to the room.
		if (replay_) {
			// A replay's game ends in place: no room counts it.
		} else if (watching_) {
			facade::NotifyRuntimeWatchedGame(prompt_.result);
		} else {
			Report(true);
		}
		++gamesInPlace_;
		s_gameResult = sf4e::room::MatchResult::Abort;
		matchEnded_ = false;
		prompt_ = {};
		scenes_ = {};
	} else {
		if (!prompt_.reported && !watching_) Report(false);
		prompt_.active = false;
		declines_ = rip::Declines(prompt_.bits[0], prompt_.bits[1]);
	}
	s_prompting = false;
	s_pipeDeclined = false;
}

void RoomBattle::FlushHeld(const char* why)
{
	if (!prompt_.active) return;
	if (!prompt_.reported && !watching_) {
		Log("netplay: %s over the WIN; the room hears game %u as the match's end", why, gamesInPlace_ + 1);
		Report(false);
	}
	prompt_.active = false;
	s_prompting = false;
}

void RoomBattle::Report(bool continued)
{
	facade::NotifyRuntimeMatchResult(prompt_.result, prompt_.captureFrame, prompt_.confirmedFrame, gamesInPlace_, continued);
	prompt_.reported = true;
}

bool RoomBattle::Close(RollbackSession* session)
{
	if (!active_) return false;
	FlushHeld("the battle closed");
	if (replay_) {
		// No runtime or room heard of it: the session closes, and that is all.
		if (session && session->Live()) session->Close();
		active_ = replay_ = watching_ = false;
		Log("netplay: the replay's battle closed after %u game(s) in place", gamesInPlace_);
		return false;
	}
	// A session still live here played its match to the end; one already
	// gone broke, or the runtime ended it.
	const bool ended = matchEnded_ || (session && session->Live());
	// The room hears the match's end while the session is still the battle's:
	// as SF4's CloseBattle has it, that is when the runtime decides whether P1
	// keeps the session for its watchers (Battle::SpectatorStreams).
	if (ended) facade::NotifyMatchEnded();
	const bool keep = ended && session && session->Live() && facade::DrainingSpectators();
	if (keep) {
		session->KeepForWatchers();
	} else if (session && session->Live()) {
		session->Close();
	}
	SetBattleInPlay({});
	active_ = false;
	phase_ = sf4e::GgpoSyncPhase::None;
	facade::NotifyGgpoSyncPhase(phase_);
	if (!ended) facade::NotifyRuntimeBattleClosedWithoutSession();
	if (facade::IsControlPlaneLost()) facade::FinalizeControlPlaneLossAfterBattle();
	Log("netplay: the room's battle closed %s after %u game(s) in place; back in the room%s",
		ended ? "at its match's end" : "without its session", gamesInPlace_,
		keep ? " (the session stays for the watchers)" : "");
	return keep;
}

std::string RoomBattle::DescribeRematch() const
{
	char text[256];
	snprintf(text, sizeof(text),
		"{\"prompt\":%s,\"scenes\":%s,\"offered\":%s,\"held\":%d,\"choices\":%u,\"declines\":%u,\"bits\":[%u,%u],\"games\":%u,\"last\":\"%s\"}",
		prompt_.active ? "true" : "false", ResultScenesPlaying() ? "true" : "false", prompt_.offered ? "true" : "false", prompt_.held, prompt_.choices, declines_,
		(unsigned)prompt_.bits[0], (unsigned)prompt_.bits[1], gamesInPlace_, lastAnswer_.c_str());
	return text;
}

} // namespace sfve::adapter
