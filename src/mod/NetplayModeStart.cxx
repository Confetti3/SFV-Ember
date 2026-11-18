// NetplayMode: a battle's start. The session's settings once the match is
// known, the start state made and agreed between the copies before GGPO binds
// the port, and the session started with the battle's recording.

#include "mod/NetplayMode.hxx"

#include "ember/DesyncReporter.hxx"
#include "ember/DesyncTrace.hxx"
#include "ember/EmberHost.hxx"
#include "ember/RoomCommands.hxx"
#include "ember/SfvFrameMeter.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvReplays.hxx"
#include "ember/SfvShell.hxx"
#include "ember/SfvTraining.hxx"
#include "ember/SfvVerification.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameExit.hxx"
#include "mod/GameHooks.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameMemory.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/RuntimeModules.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/VersusLaunch.hxx"

#include "SpectatorCatchUp.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include <optional>

namespace sfve::mod {

bool NetplayMode::StartSession(SfvAdapter& adapter)
{
	if (phase_ == netplay::Phase::Preparing) {
		switch (PrepareStart(adapter)) {
		case Prepared::Waiting: return false;
		case Prepared::NoSession: Enter(netplay::Event::NoSession); return true;
		case Prepared::Ready: break;
		}
	}
	// Every copy stands still at its start state until they agree on it
	// (core/StartAgreement.hxx); the battle waits meanwhile.
	if (startAgreement_ && !AgreeStart(adapter)) return false;
	Enter(netplay::Event::SessionStarted);
	RollbackSession::Config& c = pending_.config;
	const bool practice = pending_.practice;
	const uint32_t seed = pending_.seed;
	// A room battle is recorded from its first frame (EMBER_LIBRARY.md
	// section 26): the session reports each frame it plays to the recorder.
	// A watched one only with Settings' "Save matches you watch", and never a
	// Practice room's (its refilling gauges are set outside the frames, and
	// it reaches no result), as SF4's (UserApp's recordReplay).
	const bool record = room_.Active() && !replay_ && !practice &&
		(!room_.Watching() || sf4e::NetplayFacade::GetRuntimeSnapshotShared()->preferences.recordWatched);
	if (record) {
		recording_.Start();
		c.onCheckpoint = [this](int frame, uint64_t hash) { recording_.OnCheckpoint(frame, hash); };
	}
	// A desync's report needs the inputs of the frames before it whether or not
	// the battle is recorded (ember/DesyncTrace.hxx), so every room battle's
	// frames go to the trace as well; the recorder's call is as it was.
	sfve::adapter::Trace().Reset();
	if (room_.Active()) {
		c.onFrame = [this, record](int frame, const RecordedFrame& played) {
			if (record) recording_.OnFrame(frame, played);
			sfve::adapter::Trace().NoteFrame(frame, played);
		};
	}
	std::string error;
	const bool started = session_->Start(c, &error);
	if (!started) Log("netplay: could not start: %s", error.c_str());
	if (room_.Active() && !replay_) room_.NoteSessionStart(started);
	if (record) {
		// The room's names for both sides are known once the start is noted.
		const int recordedDelay = room_.Watching() ? room_.WatchedDelay() : c.inputDelay;
		if (!started || !adapter::RecordingHeader((int)room_.LocalSlot(), seed, recordedDelay, &recording_.Header())) {
			if (started) Log("netplay: the room's match is not known; this battle is not recorded");
			recording_.Stop();
		}
		// P1's start state, which a playback compares its own with, and the
		// balance versions every copy agreed on before frame 0.
		recording_.Header().startHash = startHash_;
		recording_.Header().addresses = startAddresses_;
		for (int side = 0; side < kPlayers; side++) {
			recording_.Header().players[side].balanceVersion = match_data::SideVersion(adapter.BattleFacts(), side);
		}
	}
	// The results menu would let each player leave the match their own way.
	// A replay's battle holds its WIN as the room's did.
	SetLinkedMatchEnd(room_.Active() && session_->Live());
	return true;
}

NetplayMode::Prepared NetplayMode::PrepareStart(SfvAdapter& adapter)
{
	script_ = input::ScriptedInput(config_.script, config_.player);
	RollbackSession::Config c;
	uint32_t seed = kSessionSeed;
	int delay = config_.delay;
	if (replay_) {
		// A replay: its own frames, seed and checkpoints, and no network.
		c.replay = &replay_->frames;
		c.onCheckpoint = [this](int frame, uint64_t hash) {
			adapter::NotePlaybackCheckpoint(frame, hash);
			if (config_.replayRegionsFrom > 0 && frame >= config_.replayRegionsFrom) adapter::LogCheckpointRegions(*session_, frame);
		};
		c.replayCheckDistance = config_.replayCheck;
		c.replayLag = config_.replayLag;
		c.replayLagPlayer = config_.replayLagPlayer;
		// Each playback in this process its own schedule.
		static uint32_t s_playbacks = 0;
		if (config_.replayLagJitter) c.replayLagJitter = config_.replayLagJitter + s_playbacks++;
		c.replayCheckSeed = config_.scheduleSeed;
		seed = replay_->header.seed;
		delay = 0;
	} else if (room_.Active()) {
		switch (room_.SessionConfig(&c, &seed, &delay)) {
		case adapter::RoomBattle::Start::Waiting: return Prepared::Waiting;
		case adapter::RoomBattle::Start::Gone: return Prepared::NoSession;
		case adapter::RoomBattle::Start::Ready: break;
		}
	} else if (config_.room) {
		// A battle of the player's own while rooms are on: it plays offline.
		return Prepared::NoSession;
	} else {
		c.localPlayer = config_.player;
		c.localPort = config_.port;
		c.remotePort = config_.peerPort;
	}
	Enter(netplay::Event::Prepared);
	// At least one frame: a decision held for a confirmed frame (match end)
	// waits for the other side's input for the next frame, which it has
	// always sent by then.
	c.inputDelay = delay < 1 ? 1 : delay;
	if (config_.hashEvery > 0) {
		c.hashEveryFrames = config_.hashEvery;
		c.keepAllHashes = true;
	}
	// A sample of the restores re-hashed against their saves; a replay's
	// playback makes no GGPO saves, so it changes nothing there.
	c.verifyLoadsEvery = config_.verifyLoadsEvery;
	// Both copies are at the battle's first frame: the battle frame's counts
	// start from 0 on each, and the clock-seeded random generators get the
	// same seeds, before anything is simulated.
	adapter.ZeroBattleFrameCounts();
	Log("netplay: reseeded %d random generator(s)", adapter.ReseedRandom(seed));
	// What else runs in this game (mod/RuntimeModules.hxx: an overlay, another
	// mod that may change fight state), so two copies' logs compare.
	Log("netplay: modules besides Windows' own: %s", OtherModulesText(LoadedOtherModules()).c_str());
	// What the battle's ticks before the session recorded for each player,
	// which the session's first frames read back: nothing pressed for a
	// room's or a replay's battle on every copy (BattleDriver). Anything else
	// is logged, as the two copies would start from different inputs.
	for (int player = 0; player < kPlayers; player++) {
		if (const PackedInput held = adapter.RecordedInputs(player)) {
			Log("netplay: P%d's input record holds %08x before frame 0", player + 1, held);
		}
	}
	// A Practice room's game: Training's refilling gauges on both copies at
	// this same frame, before anything is simulated (EMBER_LIBRARY.md 27).
	// A test's -sfve_practice does the same outside rooms.
	const bool practice = room_.Practice() || config_.practiceGauges;
	if (practice && !adapter.SetPracticeGauges()) Log("netplay: the Practice match plays with a match's gauges");
	// A test's -sfve_start_ex: only the EX gauge, full at every round's
	// start, so a scripted Critical Art can end a round (with -vital, which
	// Practice's refilling vital would undo). The tables are written once
	// here, before anything is simulated, like Practice's, so both copies
	// have it from the same frame and no rollback touches it; Practice
	// already refills EX, so it needs nothing more.
	if (config_.startFullExtra && !practice && !adapter.SetStartFullExtra()) {
		Log("netplay: the EX gauge starts as a match's");
	}
	seed_ = seed;
	startTime_ = adapter.StartTime();
	// A test's difference in this copy's start state (-sfve_start_perturb),
	// for the agreement below to find and repair.
	NetplayDevCommands::PerturbStart(adapter, config_.startPerturb);
	pending_ = {c, seed, practice};
	startHash_ = 0;
	startAddresses_.clear();
	if (replay_) {
		// No peer to agree with: the recording says what P1 started from,
		// and which words the copies' hashes zeroed (mod/AddressRanges.hxx).
		if (!replay_->header.addresses.empty()) AdoptAgreedRanges(replay_->header.addresses);
		adapter::NotePlaybackStart(replay_->header.startHash, adapter.Layout().HashLive(HashScope::Peers));
	} else {
		BeginStartAgreement(adapter);
	}
	return Prepared::Ready;
}

void NetplayMode::BeginStartAgreement(SfvAdapter& adapter)
{
	const RollbackSession::Config& c = pending_.config;
	StartAgreement::Config agreement;
	agreement.token = pending_.seed;
	// "game.N" is a place in the game's table, which two processes order
	// differently; every other region name is its owner's (RegionNames,
	// SfvLayoutRules) and the same in every process.
	agreement.stableName = [](const std::string& name) { return name.rfind("game.", 0) != 0; };
	agreement.facts = adapter.BattleFacts();
	// Which words both fighters' hashes zero (mod/AddressRanges.hxx): this
	// copy's ranges below 1 TB go to the others, and the match's come back.
	agreement.addresses = RangesToShare(start_state::kMostOwnRanges);
	agreement.adoptAddresses = AdoptAgreedRanges;
	std::vector<uint16_t> ports{c.remotePort};
	if (!c.watching && c.localPlayer == 0) {
		// P1: P2 first, then each watcher it streams to (the order
		// RoomBattle::LeaveOutUnagreedWatchers reads the report in).
		agreement.role = StartAgreement::Role::Reference;
		agreement.requiredPeers = 1;
		for (const RollbackSession::Config::Watcher& w : c.watchers) ports.push_back(w.port);
		// A watcher's battle loads as fast as its PC does, often seconds after
		// the fighters'. P1 serves it for as long as SF4's P1 waits for a
		// watcher to synchronize; one not done by then sits the game out
		// (AgreeStart) and holds the fighters no longer.
		agreement.watcherGraceMs = c.watcherSyncDeadlineMs;
	} else {
		agreement.role = StartAgreement::Role::Follower;
		// A watcher waits for P1 as long as P1 waits for it; a fighter, the
		// whole time limit.
		if (c.watching) agreement.timeLimitMs = c.watcherSyncDeadlineMs;
	}
	auto channel = std::make_unique<UdpStartChannel>();
	if (!channel->Open(c.localPort, c.remoteIp, ports)) {
		Log("netplay: no start state agreement on port %u; the session starts as it is", c.localPort);
		return;
	}
	startChannel_ = std::move(channel);
	startAgreement_ = std::make_unique<StartAgreement>(adapter, *startChannel_, std::move(agreement));
	startAgreement_->Begin(GetTickCount64(), ports.size());
}

bool NetplayMode::AgreeStart(SfvAdapter& adapter)
{
	if (!startAgreement_->Pump(GetTickCount64())) return false;
	const StartAgreement::Report& report = startAgreement_->GetReport();
	if (StartAgreement::RefusesTheMatch(report.result)) {
		// Two copies that compute with other game data (core/MatchData.hxx), or
		// that save and compare another state layout (another build's rollback
		// rules, SfvLayoutRules), cannot play one match: it ends here, on each
		// copy, before frame 0.
		adapter::ReportStartRefused(report, adapter.BattleFacts(), adapter.Layout(), room_.Watching(), pending_.config.inputDelay);
		EndStartAgreement();
		room_.FlushHeld(StartAgreement::ResultName(report.result));
		Leave();
		return false;
	}
	// P1 streams only to the watchers that agreed in time (a follower's
	// report names no peers and its config no watchers).
	adapter::RoomBattle::LeaveOutUnagreedWatchers(pending_.config, report.peers);
	startHash_ = report.reference ? report.reference : report.localBefore;
	startAddresses_ = report.addresses;
	// A repair is reported as a desync is (it was one, before frame 0).
	if (!report.regions.empty() && room_.Active()) {
		adapter::ReportStartRepair(report, adapter.Layout(), room_.Watching(), pending_.config.inputDelay);
	}
	if (const char* failure = adapter.StateFailure()) Log("netplay: after the start state's agreement: %s", failure);
	EndStartAgreement(); // the port is GGPO's from here
	return true;
}

void NetplayMode::EndStartAgreement()
{
	startAgreement_.reset();
	startChannel_.reset();
}

} // namespace sfve::mod
