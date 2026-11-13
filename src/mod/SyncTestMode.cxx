#include "mod/SyncTestMode.hxx"

#include "mod/ControlPipe.hxx"
#include "mod/GameInput.hxx"
#include "mod/Log.hxx"

#include <stdio.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace sfve::mod {

void SyncTestMode::NewBattle(SfvAdapter& adapter)
{
	tester_ = std::make_unique<SyncTester>(adapter, config_.distance, config_.learn, config_.perturb, config_.scheduleSeed);
	failureLogged_ = false;
	failuresLogged_ = 0;
	learnedLogged_.clear();
	scripts_[0] = input::ScriptedInput(config_.script, 0);
	scripts_[1] = input::ScriptedInput(config_.script, 1);
	char schedule[48] = "";
	if (config_.scheduleSeed) snprintf(schedule, sizeof(schedule), ", random schedule %u", config_.scheduleSeed);
	Log("synctest: started, %zu bytes of state, rollback distance %d%s%s%s%s%s", adapter.Layout().TotalSize(), config_.distance,
		config_.learn ? ", learning" : "", config_.perturb ? ", perturbed replays" : "", schedule,
		config_.script.script != input::Script::Moves             ? ""
			: config_.script.moveSet == input::MoveSet::Supers      ? ", Critical Arts"
			: config_.script.moveSet == input::MoveSet::Cody        ? ", Z29's items"
			: config_.script.moveSet == input::MoveSet::Projectiles ? ", projectiles"
			: config_.script.moveSet == input::MoveSet::Luke        ? ", Luke's target combos"
			: config_.script.moveSet == input::MoveSet::Akira       ? ", Akira's V-Skill"
			: config_.script.moveSet == input::MoveSet::Targets     ? ", target combos"
																	: ", system moves",
		config_.script.idle == 0 ? ", P1 idle" : config_.script.idle == 1 ? ", P2 idle" : "");
	// Training's refilling gauges before anything is simulated, as a Practice
	// room's match sets them (NetplayMode), so the mash's V-Triggers and
	// Critical Arts are played rather than refused for want of meter.
	if (config_.practiceGauges && !adapter.SetPracticeGauges()) Log("synctest: the battle plays with a match's gauges");
}

void SyncTestMode::Frame(SfvAdapter& adapter)
{
	if (restart_) {
		restart_ = false;
		NewBattle(adapter);
	}
	if (!tester_->Report().ok) {
		adapter.RunOriginal(); // the battle goes on without checks
		return;
	}
	const bool going = tester_->Step(Inputs(adapter));
	if (config_.learn) {
		LogNewRegions(adapter);
	} else if (!going && !failureLogged_) {
		failureLogged_ = true;
		LogFailure();
	}
}

FrameInputs SyncTestMode::Inputs(SfvAdapter& adapter)
{
	const int frame = tester_->Report().framesRun;
	// The facing each fighter has now, after the last frame: a block of moves
	// that starts here plays its motions toward it.
	const auto facing = [&](int side) { return config_.script.seed ? adapter.FacingRight(side) : std::nullopt; };
	return FrameInputs{{scripts_[0].At(frame, facing(0)).value_or(adapter.Sampled(0)),
		scripts_[1].At(frame, facing(1)).value_or(adapter.Sampled(1))}};
}

void SyncTestMode::LogFailure() const
{
	const SyncTestReport& r = tester_->Report();
	Log("synctest: FAILED after %d frames, %d rollbacks: %s", r.framesRun, r.rollbacks, r.message.c_str());
	for (const RegionDiff& d : r.diffs) {
		Log("synctest:   %s: %zu byte(s) differ, first at +0x%zX", d.region.c_str(), d.differingBytes, d.firstOffset);
		for (const WordDiff& w : d.words) {
			Log("synctest:     +0x%04zX  played %016llX  replayed %016llX", w.offset, (unsigned long long)w.saved,
				(unsigned long long)w.live);
		}
	}
}

// Learning: the first difference in each region, once, with its words.
void SyncTestMode::LogNewRegions(const SfvAdapter& adapter)
{
	const SyncTestReport& r = tester_->Report();
	if (r.failures == failuresLogged_) return;
	failuresLogged_ = r.failures;
	for (const auto& [region, example] : r.regionExamples) {
		if (!learnedLogged_.insert(region).second) continue;
		const StateRegion* info = adapter.Layout().Find(region);
		Log("learn: %s (%zu bytes) differs at frame %d: %zu byte(s) from +0x%zX", region.c_str(),
			info ? info->size : 0, r.failedFrame, example.differingBytes, example.firstOffset);
		for (const WordDiff& w : example.words) {
			Log("learn:     +0x%04zX  played %016llX  replayed %016llX", w.offset, (unsigned long long)w.saved,
				(unsigned long long)w.live);
		}
	}
}

std::string SyncTestMode::Describe() const
{
	if (!tester_) return "null";
	const SyncTestReport& r = tester_->Report();
	std::string json = "{\"ok\":" + std::string(r.ok ? "true" : "false") +
		",\"framesRun\":" + std::to_string(r.framesRun) + ",\"rollbacks\":" + std::to_string(r.rollbacks) +
		",\"message\":" + JsonString(r.message) + ",\"failures\":" + std::to_string(r.failures) +
		",\"regionFailures\":{";
	std::vector<std::pair<int, std::string>> worst;
	for (const auto& [region, count] : r.regionFailures) worst.emplace_back(count, region);
	std::sort(worst.rbegin(), worst.rend());
	for (size_t i = 0; i < worst.size() && i < 24; i++) {
		json += (i ? "," : "") + JsonString(worst[i].second) + ":" + std::to_string(worst[i].first);
	}
	return json + "}}";
}

} // namespace sfve::mod
