#include "mod/NetplayMode.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameInput.hxx"
#include "mod/Log.hxx"

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

void NetplayMode::NewBattle(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	session_ = std::make_unique<RollbackSession>(adapter);
	started_ = false;
	frames_ = lockstepHolds_ = 0;
	pacer_.Reset();
}

void NetplayMode::Frame(SfvAdapter& adapter)
{
	std::lock_guard lock(mutex_);
	frames_++;
	if (!started_ && !StartSession(adapter)) return; // the battle waits for the match
	if (!session_->Live()) {
		// No session (it failed or never started): the battle carries on
		// offline rather than freezing.
		adapter.RunOriginal();
		return;
	}
	const bool frozen = freezeFrame_ >= 0 && session_->Frame() >= freezeFrame_;
	if (frozen || HoldForLockstep(adapter)) {
		session_->Poll(); // held frames still receive inputs
		return;
	}
	if (session_->Running()) pacer_.Frame(*session_, last_ == RollbackSession::StepResult::PredictionStall);
	const int frame = session_->Frame();
	const PackedInput local = !config_.mashSeed ? adapter.Sampled(0)
		: frame >= config_.mashFrom              ? input::Mash(config_.mashSeed, frame)
												 : 0;
	last_ = session_->Step(local);
	DumpIfRequested();
}

bool NetplayMode::StartSession(SfvAdapter& adapter)
{
	RollbackSession::Config c;
	uint32_t seed = kSessionSeed;
	int delay = config_.delay;
	if (link_) {
		if (link_->Failed()) {
			// No match will come: the battle runs offline instead of waiting.
			Log("netplay: no match link (%s); the battle runs offline", link_->Failure().c_str());
			started_ = true;
			return true;
		}
		const std::optional<MatchLink::Endpoints> match = link_->Ready();
		if (!match) return false;
		c.localPlayer = match->localPlayer;
		c.localPort = match->localPort;
		c.remotePort = match->remotePort;
		seed = match->seed;
		delay = match->inputDelay;
	} else {
		c.localPlayer = config_.player;
		c.localPort = config_.port;
		c.remotePort = config_.peerPort;
	}
	started_ = true;
	// At least one frame: lockstep (below) waits for the other side's input
	// for the next frame, which it has always sent by then.
	c.inputDelay = delay < 1 ? 1 : delay;
	// Both copies are at the battle's first frame: give the clock-seeded
	// random generators the same seeds before anything is simulated.
	Log("netplay: reseeded %d random generator(s)", adapter.ReseedRandom(seed));
	std::string error;
	if (!session_->Start(c, &error)) Log("netplay: could not start: %s", error.c_str());
	return true;
}

// Layout: per region {uint64 size, uint64 address}, then its bytes.
void NetplayMode::DumpIfRequested()
{
	if (dumpFrame_ < 0 || session_->Frame() != dumpFrame_) return;
	const uint8_t* bytes = session_->SavedState(dumpFrame_);
	if (!bytes) return;
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
}

// Outside the fight (intros, round calls, the KO and results) the battle flow
// waits on presentation (announcer, banners) that a replay does not repeat
// the same way; a frame played there on a guess and then replayed could shift
// it. So there a frame only runs once both players' inputs for it are
// confirmed: no guess, no rollback. (The synctest shows the flow record
// diverging on replays across intro and round-over states, never in a fight.)
bool NetplayMode::HoldForLockstep(const SfvAdapter& adapter)
{
	if (adapter.Fighting() || !session_->Running()) return false;
	// Until the first inputs have crossed (only the delay's padding frames so
	// far) there is nothing to wait for, and waiting would stop both sides.
	const int confirmed = session_->LastConfirmedFrame();
	if (confirmed < 0 || confirmed >= session_->Frame()) return false;
	lockstepHolds_++;
	return true;
}

std::string NetplayMode::Describe() const
{
	std::lock_guard lock(mutex_);
	if (!session_) return "null";
	const RollbackSession::Stats& s = session_->GetStats();
	char text[512];
	snprintf(text, sizeof(text),
		"{\"live\":%s,\"running\":%s,\"failure\":%s,\"frame\":%d,\"confirmed\":%d,\"last\":\"%s\","
		"\"advanced\":%llu,\"rollbacks\":%llu,\"resimulated\":%llu,\"longestRollback\":%d,\"stalls\":%llu,"
		"\"lockstepHolds\":%llu,\"pacing\":",
		session_->Live() ? "true" : "false", session_->Running() ? "true" : "false",
		JsonString(session_->FailureReason()).c_str(), session_->Frame(), session_->LastConfirmedFrame(),
		StepName(last_), (unsigned long long)s.framesAdvanced, (unsigned long long)s.rollbacks,
		(unsigned long long)s.resimulatedFrames, s.longestRollback, (unsigned long long)s.predictionStalls,
		(unsigned long long)lockstepHolds_);
	return text + pacer_.Describe() + "}";
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
		std::vector<uint64_t> hashes;
		if (frame < 0 || !session_->RegionHashesAt(frame, &hashes)) return std::string("error no checkpoint");
		// size:hash per region, so unmatched regions can be grouped by size
		// (each process lists them in its own order).
		std::string out = "ok " + std::to_string(frame);
		const auto& regions = session_->Layout().Regions();
		char item[48];
		for (size_t i = 0; i < hashes.size() && i < regions.size(); i++) {
			snprintf(item, sizeof(item), " %zu:%016llx", regions[i].size, (unsigned long long)hashes[i]);
			out += item;
		}
		return out;
	});
}

} // namespace sfve::mod
