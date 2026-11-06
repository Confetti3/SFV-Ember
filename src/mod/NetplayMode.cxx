#include "mod/NetplayMode.hxx"

#include "ember/EmberHost.hxx"
#include "ember/RoomCommands.hxx"
#include "ember/SfvShell.hxx"
#include "mod/ControlPipe.hxx"
#include "mod/GameInput.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"

#include <stdio.h>
#include <stdlib.h>

namespace sfve::mod {

namespace {

const char* StepName(RollbackSession::StepResult r)
{
	switch (r) {
	case RollbackSession::StepResult::NoSession: return "no session";
	case RollbackSession::StepResult::Waiting: return "waiting";
	case RollbackSession::StepResult::Advanced: return "advanced";
	case RollbackSession::StepResult::PredictionStall: return "prediction stall";
	case RollbackSession::StepResult::Skipped: return "skipped";
	case RollbackSession::StepResult::Failed: return "failed";
	}
	return "?";
}

} // namespace

void NetplayMode::Install()
{
	RegisterCommands();
	if (!config_.room) return;
	adapter::RegisterRoomCommands();
	// The runtime ticks this mode for the process's life (the driver holds it).
	adapter::InstallEmberHost({config_.instance}, *this);
	adapter::InstallEmberShell();
}

void NetplayMode::NewBattle(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	auto session = std::make_unique<RollbackSession>(adapter);
	room_.Bind(session_.get(), *session, adapter, [this](const char* why) {
		Log("netplay: the room asks the battle to end (%s)", why);
		exitAsked_ = true;
	});
	session_ = std::move(session);
	started_ = leaveWanted_ = leaving_ = false;
	exitAsked_ = false;
	ForgetEndedVersusBattle();
	pacer_.Reset();
}

void NetplayMode::Frame(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	if (exitAsked_.exchange(false)) {
		// The runtime retired or aborted the match: no session goes on, and
		// the battle goes back to the main menu.
		room_.FlushHeld("the runtime ended the battle");
		if (session_->Live()) session_->Close();
		started_ = true;
		Leave();
	}
	if (leaveWanted_) {
		if (!leaving_) Leave();
		// The battle stands still until the main menu replaces it; the
		// session keeps answering meanwhile, so the other copy still gets the
		// inputs it may be missing for the last frames.
		if (session_->Live()) session_->Poll();
		return;
	}
	if (!started_ && !StartSession(adapter)) return; // the battle waits for the match
	if (!session_->Live()) {
		if (room_.Active()) {
			// A room battle with no session (it broke, or never started) has
			// no other player: back to the main menu and the room.
			Log("netplay: no session (%s); back to the main menu", session_->FailureReason().c_str());
			room_.NoteLink(*session_); // the opponent dropped, if they did
			room_.FlushHeld("the session ended");
			Leave();
			return;
		}
		// No session (none was asked for): the battle carries on offline
		// rather than freezing.
		adapter.RunOriginal();
		return;
	}
	room_.NoteSyncPhase(*session_);
	room_.NoteLink(*session_);
	const bool frozen = freezeFrame_ >= 0 && session_->Frame() >= freezeFrame_;
	if (room_.Prompting() && !frozen) {
		// Over the WIN the battle stands still; held frames carry both
		// fighters' answers, each stepped once its inputs are confirmed, and
		// both copies decide on the same one (RoomBattle, EMBER_LIBRARY.md 19).
		const PackedInput answer = room_.AnswerBits();
		adapter::RoomBattle::Answer decided = adapter::RoomBattle::Answer::Wait;
		last_ = session_->StepHeld(answer, [&](const FrameInputs& inputs) {
			decided = room_.HeldFrame(inputs, session_->HeldFrames(), adapter, seed_);
		});
		if (last_ != RollbackSession::StepResult::Advanced) return;
		room_.Decided(decided, *session_);
		DumpIfRequested();
		if (decided == adapter::RoomBattle::Answer::Rematch) {
			startTime_ = adapter.StartTime();
		} else if (decided == adapter::RoomBattle::Answer::Leave) {
			Leave(); // both copies leave on this same confirmed frame
		}
		return;
	}
	// A room match that ended is decided only on a confirmed frame: nothing
	// before the decision can be rolled back then, on either copy (MatchEnd).
	const bool endPending = room_.EndPending();
	if (frozen || (endPending && session_->LastConfirmedFrame() < session_->Frame())) {
		session_->Poll(); // held frames still receive inputs
		DumpIfRequested(); // and get confirmed
		return;
	}
	if (session_->Running()) pacer_.Frame(*session_, last_ == RollbackSession::StepResult::PredictionStall);
	const int frame = session_->Frame();
	// Sent as the player plays; frames whose inputs the game ignores (outside
	// the fight) get neutral ones in RollbackSession, whatever was sent.
	const PackedInput local = !config_.mashSeed ? adapter.Sampled(0)
		: frame >= config_.mashFrom              ? input::Mash(config_.mashSeed, frame)
												 : 0;
	// The game's judgement, the same on both copies (it is restored with the
	// rest of the state on rollback): both copies hold the battle for the
	// WIN's answers from this same confirmed frame.
	if (endPending && room_.TakeEnd(adapter, *session_)) return;
	// The match's start time lies outside the rollback state, and a match
	// data reset takes it from the clock; the fighters' generators are
	// reseeded from it at each round's start. It stays the session's.
	room_.NoteFightFrame();
	if (room_.Active() && adapter.StartTime() != startTime_) {
		Log("netplay: the match's start time became %u at frame %d; set back to %u", adapter.StartTime(), frame,
			startTime_);
		adapter.SetStartTime(startTime_);
	}
	last_ = session_->Step(local);
	DumpIfRequested();
	PerturbIfDue();
}

void NetplayMode::PollBattle()
{
	std::lock_guard lock(mutex_);
	if (!room_.Active() || !MainMenuInFront()) return;
	room_.Close(session_.get());
	leaveWanted_ = leaving_ = false;
}

void NetplayMode::Leave()
{
	if (!leaveWanted_) SetLinkedMatchEnd(false);
	const bool retry = leaveWanted_;
	leaveWanted_ = true;
	leaving_ = LeaveBattle();
	if (!leaving_ && !retry) Log("netplay: could not leave the battle yet (no scene manager); asking again each frame");
}

bool NetplayMode::StartSession(SfvAdapter& adapter)
{
	RollbackSession::Config c;
	uint32_t seed = kSessionSeed;
	int delay = config_.delay;
	if (room_.Active()) {
		switch (room_.SessionConfig(&c, &seed, &delay)) {
		case adapter::RoomBattle::Start::Waiting: return false;
		case adapter::RoomBattle::Start::Gone: started_ = true; return true;
		case adapter::RoomBattle::Start::Ready: break;
		}
	} else if (config_.room) {
		// A battle of the player's own while rooms are on: it plays offline.
		started_ = true;
		return true;
	} else {
		c.localPlayer = config_.player;
		c.localPort = config_.port;
		c.remotePort = config_.peerPort;
	}
	started_ = true;
	// At least one frame: a decision held for a confirmed frame (match end)
	// waits for the other side's input for the next frame, which it has
	// always sent by then.
	c.inputDelay = delay < 1 ? 1 : delay;
	if (config_.hashEvery > 0) {
		c.hashEveryFrames = config_.hashEvery;
		c.keepAllHashes = true;
	}
	// Both copies are at the battle's first frame: give the clock-seeded
	// random generators the same seeds before anything is simulated.
	Log("netplay: reseeded %d random generator(s)", adapter.ReseedRandom(seed));
	seed_ = seed;
	startTime_ = adapter.StartTime();
	std::string error;
	const bool started = session_->Start(c, &error);
	if (!started) Log("netplay: could not start: %s", error.c_str());
	if (room_.Active()) room_.NoteSessionStart(started);
	// The results menu would let each player leave the match their own way.
	SetLinkedMatchEnd(room_.Active() && session_->Live());
	return true;
}

// Layout: per region {uint64 size, uint64 address}, then its bytes. Written
// once the frame is confirmed, so two copies' dumps of a frame are final. The
// session may free the frame's save before then, so its latest save (a
// rollback replays and saves it again) is copied while it is kept.
void NetplayMode::DumpIfRequested()
{
	if (dumpFrame_ < 0) return;
	if (const uint8_t* saved = session_->SavedState(dumpFrame_)) {
		dumpBytes_.assign(saved, saved + session_->Layout().TotalSize());
	}
	// With input delay a frame's inputs can be confirmed before it is played.
	if (session_->Frame() < dumpFrame_ || session_->LastConfirmedFrame() < dumpFrame_) return;
	if (dumpBytes_.empty()) {
		Log("netplay: frame %d was not kept; nothing dumped", dumpFrame_);
		dumpFrame_ = -1;
		return;
	}
	const uint8_t* bytes = dumpBytes_.data();
	const std::wstring path = LogFolder() + L"\\state-" + std::to_wstring(config_.instance) + L"-" +
		std::to_wstring(dumpFrame_) + L".bin";
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
	for (const StateRegion& r : session_->Layout().Regions()) {
		const uint64_t header[2] = {r.size, reinterpret_cast<uint64_t>(r.ptr)};
		fwrite(header, sizeof(header), 1, f);
		fwrite(bytes, 1, r.size, f);
		bytes += r.size;
	}
	fclose(f);
	Log("netplay: dumped frame %d to %ls", dumpFrame_, path.c_str());
	dumpFrame_ = -1;
	dumpBytes_.clear();
}

// The live state is the played frame's, already saved: a rollback to that
// frame or earlier undoes the change, so it is meant for a frame no rollback
// crosses (before the players' inputs start).
void NetplayMode::PerturbIfDue()
{
	if (perturb_.frame < 0 || session_->Frame() < perturb_.frame) return;
	const StateRegion* r = session_->Layout().FindByNameOrSize(perturb_.region);
	if (r && perturb_.offset + 4 <= r->size) {
		int32_t& value = *reinterpret_cast<int32_t*>(r->ptr + perturb_.offset);
		value += perturb_.add;
		Log("netplay: perturbed %s+0x%zX at frame %d (now %d)", r->name.c_str(), perturb_.offset, session_->Frame(), value);
	} else {
		Log("netplay: nothing to perturb at %s+0x%zX", perturb_.region.c_str(), perturb_.offset);
	}
	perturb_.frame = -1;
}

std::string NetplayMode::Describe() const
{
	std::lock_guard lock(mutex_);
	if (!session_) return "null";
	const RollbackSession::Stats& s = session_->GetStats();
	char text[512];
	snprintf(text, sizeof(text),
		"{\"room\":%s,\"live\":%s,\"running\":%s,\"failure\":%s,\"frame\":%d,\"confirmed\":%d,\"last\":\"%s\","
		"\"advanced\":%llu,\"rollbacks\":%llu,\"resimulated\":%llu,\"longestRollback\":%d,\"stalls\":%llu,"
		"\"pacing\":",
		room_.Active() ? "true" : "false", session_->Live() ? "true" : "false", session_->Running() ? "true" : "false",
		JsonString(session_->FailureReason()).c_str(), session_->Frame(), session_->LastConfirmedFrame(),
		StepName(last_), (unsigned long long)s.framesAdvanced, (unsigned long long)s.rollbacks,
		(unsigned long long)s.resimulatedFrames, s.longestRollback, (unsigned long long)s.predictionStalls);
	// The link as the HUD reads it (RollbackSession's ConnectionWarning and the rest).
	char link[192];
	snprintf(link, sizeof(link),
		",\"link\":{\"warning\":%s,\"stalled\":%s,\"countdownMs\":%d,\"delay\":%d,\"recentRollback\":%u,\"peerDropped\":%s}",
		session_->ConnectionWarning() ? "true" : "false", session_->PredictionStalled() ? "true" : "false",
		session_->DisconnectCountdownMs(), session_->AppliedDelay(), session_->RecentRollbackFrames(),
		session_->PeerDisconnected() ? "true" : "false");
	return text + pacer_.Describe() + ",\"lastResult\":" + room_.LastResult() + ",\"rematch\":" + room_.DescribeRematch() +
		",\"heldFrames\":" + std::to_string(s.heldFrames) + ",\"heldReplays\":" + std::to_string(s.heldReplays) + link + "}";
}

void NetplayMode::RegisterCommands()
{
	RegisterCommand("freeze", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		freezeFrame_ = args.empty() ? -1 : atoi(args.c_str());
		return std::string("ok");
	});
	// "riftgain N": the pacer's rift gain as N/120 (tuning; FramePacer).
	RegisterCommand("riftgain", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		pacer_.SetRiftGain(atof(args.c_str()) / 120.0);
		return std::string("ok");
	});
	RegisterCommand("dump", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		dumpFrame_ = atoi(args.c_str());
		dumpBytes_.clear();
		return std::string("ok");
	});
	RegisterCommand("perturb", [this](const std::string& args) {
		char region[64] = {};
		Perturbation p;
		if (sscanf_s(args.c_str(), "%d %63s %zi %d", &p.frame, region, (unsigned)sizeof(region), &p.offset, &p.add) != 4 ||
			p.frame < 0) {
			return std::string("error usage: perturb <frame> <region name or size> <offset> <add>");
		}
		p.region = region;
		std::lock_guard lock(mutex_);
		perturb_ = p;
		return std::string("ok");
	});
	RegisterCommand("hash", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		uint64_t hash = 0;
		const int frame = atoi(args.c_str());
		if (!session_ || !session_->HashAt(frame, &hash)) return std::string("error frame not kept");
		char text[32];
		snprintf(text, sizeof(text), "ok %016llx", (unsigned long long)hash);
		return std::string(text);
	});
	RegisterCommand("checkpoint", [this](const std::string& args) {
		std::lock_guard lock(mutex_);
		if (!session_) return std::string("error no session");
		const int atOrBefore = args.empty() ? session_->LastConfirmedFrame() : atoi(args.c_str());
		const int frame = session_->LatestCheckpoint(atOrBefore);
		std::vector<RegionHash> hashes;
		if (frame < 0 || !session_->RegionHashesAt(frame, &hashes)) return std::string("error no checkpoint");
		// size:hash per region, in the canonical order peers compare.
		std::string out = "ok " + std::to_string(frame);
		char item[48];
		for (const RegionHash& h : hashes) {
			snprintf(item, sizeof(item), " %llu:%016llx", (unsigned long long)h.size, (unsigned long long)h.hash);
			out += item;
		}
		return out;
	});
}

} // namespace sfve::mod
