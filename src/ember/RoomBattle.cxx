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
	active_ = mod::TakeLinkedBattle();
	matchEnded_ = false;
	prompt_ = {};
	s_prompting = false;
	gamesInPlace_ = 0;
	s_gameResult = sf4e::room::MatchResult::Abort;
	if (!active_) return;
	SetBattleInPlay({&session, &game, this, std::move(exit)});
	facade::ClearBattleState();
	facade::CancelDeferredGgpoClose();
	facade::ResetGgpoBattleWatch();
	phase_ = sf4e::GgpoSyncPhase::Starting;
	facade::NotifyRuntimeBattleLoaded();
	Log("netplay: the room's battle has loaded; waiting for every member's");
}

RoomBattle::Start RoomBattle::SessionConfig(RollbackSession::Config* config, std::uint32_t* seed, int* delay)
{
	if (!facade::IsRuntimeMatchLive()) {
		Log("netplay: the room's match ended before its session started");
		return Start::Gone;
	}
	if (!SessionSynced() || !facade::GetRuntimeMatchEndpoints(endpoints_)) return Start::Waiting;
	const std::size_t slot = endpoints_.localSlot;
	// Two fighters: SFV's session has no spectators (Capabilities::spectators is off).
	if (slot > 1 || endpoints_.participantCount < 2 || !endpoints_.remotePorts[1 - slot]) {
		Log("netplay: the room's match is not one this copy fights (slot %zu of %zu)", slot, endpoints_.participantCount);
		return Start::Gone;
	}
	const auto& netplay = ember::RuntimeSession::netplay;
	if (!netplay) {
		Log("netplay: the room's match has no session client any more");
		return Start::Gone;
	}
	const auto& match = netplay->client._matchData;
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
	*seed = match.rngSeed;
	*delay = match.inputDelay[slot];
	facade::ReportGgpoTransport(config->remoteIp.c_str(), config->remotePort);
	Log("netplay: room match as P%zu, local port %u, the peer's port %u, delay %d", slot + 1, config->localPort,
		config->remotePort, *delay);
	// The room's match session holds the local port until GGPO takes it.
	facade::ReleaseRuntimePortToGgpo();
	return Start::Ready;
}

void RoomBattle::NoteSessionStart(bool started)
{
	if (started) {
		facade::NoteRuntimeMatchStart(endpoints_);
	} else {
		facade::PushAlert(sf4e::loc::T("runtime.authorized_connection_unavailable"));
	}
}

void RoomBattle::NoteSyncPhase(const RollbackSession& session)
{
	if (!active_) return;
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
	prompt_.active = true;
	prompt_.result = RoomResult(*decided);
	prompt_.captureFrame = (std::uint64_t)frame;
	prompt_.confirmedFrame = (std::uint64_t)(confirmed < 0 ? 0 : confirmed);
	prompt_.versus = versus;
	s_gameResult = prompt_.result;
	matchEnded_ = true;
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

void RoomBattle::NoteFightFrame()
{
	// SF4 reads the fighter's input through the runtime every frame; the
	// runtime's prompt timer (how long a fighter has been asked) starts over
	// only on a read with no prompt due, so a later WIN's asks its full time.
	unsigned mapped = 0, raw = 0;
	if (active_ && !prompt_.active) facade::ReadRuntimeMatchInput((int)endpoints_.localSlot, mapped, raw);
}

PackedInput RoomBattle::AnswerBits()
{
	unsigned mapped = 0, raw = 0;
	// The runtime's answer: a parked Rematch, or the fighter still asked.
	if (prompt_.offered && !prompt_.reported) facade::ReadRuntimeMatchInput((int)endpoints_.localSlot, mapped, raw);
	return rip::LocalBits(prompt_.offered && !prompt_.reported, s_pipeDeclined || s_shellLeft, raw);
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
		Report(true);
		++gamesInPlace_;
		s_gameResult = sf4e::room::MatchResult::Abort;
		matchEnded_ = false;
		prompt_ = {};
	} else {
		if (!prompt_.reported) Report(false);
		prompt_.active = false;
	}
	s_prompting = false;
	s_pipeDeclined = false;
}

void RoomBattle::FlushHeld(const char* why)
{
	if (!prompt_.active) return;
	if (!prompt_.reported) {
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

void RoomBattle::Close(RollbackSession* session)
{
	if (!active_) return;
	FlushHeld("the battle closed");
	// A session still live here played its match to the end; one already
	// gone broke, or the runtime ended it.
	const bool ended = matchEnded_ || (session && session->Live());
	if (session && session->Live()) session->Close();
	SetBattleInPlay({});
	active_ = false;
	phase_ = sf4e::GgpoSyncPhase::None;
	facade::NotifyGgpoSyncPhase(phase_);
	if (ended) {
		facade::NotifyMatchEnded();
	} else {
		facade::NotifyRuntimeBattleClosedWithoutSession();
	}
	if (facade::IsControlPlaneLost()) facade::FinalizeControlPlaneLossAfterBattle();
	Log("netplay: the room's battle closed %s after %u game(s) in place; back in the room",
		ended ? "at its match's end" : "without its session", gamesInPlace_);
}

std::string RoomBattle::DescribeRematch() const
{
	char text[256];
	snprintf(text, sizeof(text),
		"{\"prompt\":%s,\"offered\":%s,\"held\":%d,\"choices\":%u,\"bits\":[%u,%u],\"games\":%u,\"last\":\"%s\"}",
		prompt_.active ? "true" : "false", prompt_.offered ? "true" : "false", prompt_.held, prompt_.choices,
		(unsigned)prompt_.bits[0], (unsigned)prompt_.bits[1], gamesInPlace_, lastAnswer_.c_str());
	return text;
}

} // namespace sfve::adapter
