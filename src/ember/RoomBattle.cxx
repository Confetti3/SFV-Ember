#include "ember/RoomBattle.hxx"

#include "ember/SfvGame.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/MatchResult.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/InputDelay.hxx"
#include "common/Localization.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"
#include "sf4e/sf4e__RuntimeSession.hxx"

namespace sfve::adapter {

namespace {

namespace facade = sf4e::NetplayFacade;
using mod::Log;

} // namespace

void RoomBattle::Bind(RollbackSession* previous, RollbackSession& session, const mod::SfvAdapter& game,
	std::function<void(const char* why)> exit)
{
	if (active_) Close(previous);
	active_ = mod::TakeLinkedBattle();
	matchEnded_ = false;
	if (!active_) return;
	SetBattleInPlay({&session, &game, std::move(exit)});
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
	return active_ && mod::EndedVersusBattlePending();
}

bool RoomBattle::TakeEnd(const mod::SfvAdapter& game, const RollbackSession& session)
{
	if (!mod::TakeEndedVersusBattle(game.BattleData())) return false;
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
	const sf4e::room::MatchResult result = ember::game::Game().battle.GameResult();
	// SFV shows no prompt over the WIN yet, so a room game never goes on in
	// place: both copies take this same confirmed frame back to the room.
	const bool offered = facade::RematchInPlaceOffered(result);
	Log("netplay: match result %s at frame %d (confirmed %d); back to the room%s", lastResult_.c_str(), frame, confirmed,
		offered ? " (the table offers a rematch in place, which SFV cannot prompt for yet)" : "");
	facade::NotifyRuntimeMatchResult(result, (std::uint64_t)frame, (std::uint64_t)confirmed, 0, false);
	matchEnded_ = true;
	return true;
}

void RoomBattle::Close(RollbackSession* session)
{
	if (!active_) return;
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
	Log("netplay: the room's battle closed %s; back in the room", ended ? "at its match's end" : "without its session");
}

} // namespace sfve::adapter
