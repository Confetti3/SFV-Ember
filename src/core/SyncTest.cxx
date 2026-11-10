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

SyncTestReport RunSyncTest(GameAdapter& game, const SyncTestOptions& options)
{
	SyncTester tester(game, options.distance, false, false, options.scheduleSeed);
	for (int frame = 0; frame < options.frames && tester.Step(options.inputs(frame)); frame++) {
	}
	return tester.Report();
}

} // namespace sfve
