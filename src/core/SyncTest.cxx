#include "core/SyncTest.hxx"

#include <algorithm>

#include <stdio.h>
#include <string.h>

namespace sfve {

namespace {

constexpr size_t kWordsPerDiff = 4;

} // namespace

std::vector<RegionDiff> DiffSavedAgainstLive(const StateLayout& layout, const uint8_t* saved)
{
	std::vector<RegionDiff> diffs;
	const auto& regions = layout.Regions();
	for (size_t i = 0; i < regions.size(); i++) {
		const StateRegion& r = regions[i];
		if (r.checked) {
			RegionDiff d;
			d.region = r.name;
			for (size_t at = 0; at < r.size; at++) {
				if (saved[at] == r.ptr[at] || layout.IsIgnored(i, at)) continue;
				if (d.differingBytes == 0) d.firstOffset = at;
				d.differingBytes++;
				const size_t word = at & ~size_t(7);
				if (d.words.size() < kWordsPerDiff && (d.words.empty() || d.words.back().offset != word) &&
					word + 8 <= r.size) {
					WordDiff w{word};
					memcpy(&w.saved, saved + word, 8);
					memcpy(&w.live, r.ptr + word, 8);
					d.words.push_back(w);
				}
			}
			if (d.differingBytes) diffs.push_back(d);
		}
		saved += r.size;
	}
	return diffs;
}

SyncTester::SyncTester(GameAdapter& game, int distance, bool learn, bool perturb, uint32_t scheduleSeed)
	: game_(game), distance_(distance < 1 ? 1 : distance), learn_(learn), perturb_(perturb), schedule_(scheduleSeed),
	  saved_(distance_ + 1)
{
	for (Saved& s : saved_) s.bytes.resize(game.Layout().TotalSize());
	ScheduleNextCheck();
}

void SyncTester::ScheduleNextCheck()
{
	if (!schedule_) {
		nextCheck_ = frame_ + distance_;
		nextDepth_ = distance_;
		return;
	}
	nextCheck_ = frame_ + Random(1, distance_);
	nextDepth_ = Random(1, distance_);
}

int SyncTester::Random(int from, int to)
{
	schedule_ ^= schedule_ << 13;
	schedule_ ^= schedule_ >> 17;
	schedule_ ^= schedule_ << 5;
	return from + (int)(schedule_ % (uint32_t)(to - from + 1));
}

void SyncTester::Save(int frame)
{
	Saved& s = At(frame);
	game_.Layout().SaveTo(s.bytes.data());
	s.hash = game_.Layout().HashSaved(s.bytes.data());
}

void SyncTester::Fail(int frame, const char* what)
{
	const std::vector<RegionDiff> diffs = DiffSavedAgainstLive(game_.Layout(), At(frame).bytes.data());
	report_.failures++;
	for (const RegionDiff& d : diffs) {
		report_.regionFailures[d.region]++;
		report_.regionExamples.emplace(d.region, d);
	}
	report_.failedFrame = frame;
	report_.diffs = diffs; // learning keeps the latest
	if (learn_) return;
	report_.ok = false;
	char text[256];
	snprintf(text, sizeof(text), "%s at frame %d (%zu region(s) differ)", what, frame, report_.diffs.size());
	report_.message = text;
}

bool SyncTester::Step(const FrameInputs& inputs)
{
	if (!report_.ok) return false;
	Save(frame_);
	Saved& s = At(frame_);
	s.inputs = inputs;
	s.restorable = game_.InputsMatter();
	s.readsConfirmed = ReadsConfirmed(game_, frame_);
	game_.Advance(AsPlayed(inputs, frame_), false);
	report_.framesRun = ++frame_;
	if (frame_ < nextCheck_) return true;
	const int from = OldestRestorable(nextDepth_);
	ScheduleNextCheck();
	return CheckRollback(from, frame_);
}

int SyncTester::OldestRestorable(int depth)
{
	int oldest = frame_;
	for (int k = frame_ - 1; k >= std::max(0, frame_ - depth); k--) {
		if (At(k).readsConfirmed) break; // a session's rollback never replays it
		if (At(k).restorable) oldest = k;
	}
	return oldest;
}

// As RollbackSession::InputsForThisFrame, asked again on every replayed
// frame: a replay that changes the course of the game (a guessed KO taken
// back) changes which frames read inputs.
FrameInputs SyncTester::AsPlayed(const FrameInputs& inputs, int frame) const
{
	return GivenInputs(game_, inputs, frame);
}

bool SyncTester::CheckRollback(int from, int now)
{
	if (from >= now) return true;
	StateLayout& layout = game_.Layout();
	Save(now);
	if (perturb_) {
		// The timeline a misprediction would have played, then thrown away.
		layout.LoadFrom(At(from).bytes.data());
		game_.AfterLoad();
		for (int k = from; k < now; k++) game_.Advance(AsPlayed(Perturbed(At(k).inputs, k), k), true);
	}
	layout.LoadFrom(At(from).bytes.data());
	game_.AfterLoad();
	report_.rollbacks++;
	if (layout.HashLive() != At(from).hash) {
		report_.loadMismatch = true;
		Fail(from, "restored state differs from the save");
		return learn_;
	}
	bool diverged = false;
	for (int k = from; k < now; k++) {
		game_.Advance(AsPlayed(At(k).inputs, k), true);
		if (!diverged && layout.HashLive() != At(k + 1).hash) {
			Fail(k + 1, "replayed frame differs");
			if (!learn_) return false;
			diverged = true; // each rollback counts once
		}
		// Learning goes on from the replayed state, as a session does, and
		// saves each replayed frame again, as a session does: later checks
		// then restore and compare states of the one timeline the game is
		// on, not a mix of the played and the replayed one (a difference that
		// does not affect the fight could otherwise show up as one that does).
		if (learn_) Save(k + 1);
	}
	return true;
}

// Wrong inputs for the thrown-away timeline: every bit pattern changes.
FrameInputs SyncTester::Perturbed(const FrameInputs& inputs, int frame)
{
	FrameInputs wrong = inputs;
	for (int p = 0; p < kPlayers; p++) wrong.p[p] = ~inputs.p[p] ^ (PackedInput)(frame * 2654435761u);
	return wrong;
}

LaggedReplay::LaggedReplay(GameAdapter& game, const ReplayFrames& frames, int remote, int lag, uint32_t jitterSeed)
	: jitter_(jitterSeed), game_(game), frames_(frames), remote_(remote), lag_(lag < 1 ? 1 : lag),
	  saved_((size_t)lag_ + 3)
{
	for (Saved& s : saved_) s.bytes.resize(game.Layout().TotalSize());
	arrival_.resize(frames.size());
	delay_ = (lag_ + 1) / 2;
}

int LaggedReplay::Known(int frame)
{
	if (!jitter_) return frame - lag_;
	// Each input's arrival, in order, as far as this frame can need: an input
	// arrives no earlier than the one before it, and at most lag_ frames late.
	while (scheduledTo_ + 1 < (int)arrival_.size() && scheduledTo_ + 1 <= frame) {
		const int k = ++scheduledTo_;
		jitter_ ^= jitter_ << 13;
		jitter_ ^= jitter_ >> 17;
		jitter_ ^= jitter_ << 5;
		const uint32_t r = jitter_ % 100;
		if (r < 10 && delay_ < lag_) delay_++;
		else if (r < 20 && delay_ > 1) delay_--;
		const int delay = r >= 97 ? lag_ : delay_;
		const int earliest = k > 0 ? arrival_[(size_t)k - 1] : 0;
		arrival_[(size_t)k] = k + delay > earliest ? k + delay : earliest;
	}
	while (arrivedTo_ + 1 <= scheduledTo_ && arrival_[(size_t)arrivedTo_ + 1] <= frame) arrivedTo_++;
	return arrivedTo_;
}

void LaggedReplay::Step(int frame)
{
	if (frame != next_) return;
	const int known = Known(frame);
	Resolve(known);
	Play(frame, known);
	next_ = frame + 1;
	if (known + 1 > confirmedThrough_) confirmedThrough_ = known + 1;
}

void LaggedReplay::Finish()
{
	Resolve(next_ - 1);
	confirmedThrough_ = next_;
}

const uint8_t* LaggedReplay::SavedAt(int frame) const
{
	if (frame < 0 || frame > confirmedThrough_) return nullptr;
	const Saved& s = saved_[(size_t)frame % saved_.size()];
	return s.frame == frame ? s.bytes.data() : nullptr;
}

void LaggedReplay::Resolve(int known)
{
	const int through = known < next_ - 1 ? known : next_ - 1;
	int wrong = -1;
	for (int k = checked_ + 1; k <= through; k++) {
		const Saved& s = At(k);
		if (s.frame == k && s.guessed && s.remote != frames_[(size_t)k].inputs.p[remote_]) {
			wrong = k;
			break;
		}
	}
	if (through > checked_) checked_ = through;
	if (wrong < 0) return;
	StateLayout& layout = game_.Layout();
	layout.LoadFrom(At(wrong).bytes.data());
	game_.AfterLoad();
	rollbacks_++;
	if (next_ - wrong > longest_) longest_ = next_ - wrong;
	for (int k = wrong; k < next_; k++) Play(k, known);
}

void LaggedReplay::Play(int frame, int known)
{
	Saved& s = At(frame);
	game_.Layout().SaveTo(s.bytes.data());
	s.frame = frame;
	if (onGuessedState && frame >= next_ && known < frame - 1) onGuessedState(frame, s.bytes.data());
	FrameInputs inputs = frames_[(size_t)frame].inputs;
	s.guessed = game_.InputsMatter() && frame > known;
	if (s.guessed) inputs.p[remote_] = known >= 0 ? frames_[(size_t)known].inputs.p[remote_] : 0;
	const FrameInputs given = GivenInputs(game_, inputs, frame);
	s.remote = given.p[remote_];
	game_.Advance(given, frame < next_);
}

SyncTestReport RunSyncTest(GameAdapter& game, const SyncTestOptions& options)
{
	SyncTester tester(game, options.distance, false, false, options.scheduleSeed);
	for (int frame = 0; frame < options.frames && tester.Step(options.inputs(frame)); frame++) {
	}
	return tester.Report();
}

} // namespace sfve
