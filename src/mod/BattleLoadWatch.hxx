#pragma once

// The log's record of a battle load, and the report of one that never ends:
// the library's watch (common/LoadWatch.hxx, shared with SF4 Ember) fed with
// the moments SFV's scene manager shows. A load starts when the scene becomes
// a Versus or Training battle, its first frame is the first battle Ember's
// adapter binds after that, and it is over when the scene leaves the battle.
// The scene manager ticks once a frame, so it is the
// clock. Log lines only: nothing here steers the game. Time is passed in
// (milliseconds on a monotonic clock), so no clock and no game are needed.

#include "common/LoadWatch.hxx"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sfve::mod {

class BattleLoadWatch {
public:
	// One scene-manager tick: `inBattle` is whether the current scene is a
	// battle, `battleSeen` the number of the last battle the adapter bound
	// (it changes when the next one binds). The lines now due.
	std::vector<std::string> Observe(bool inBattle, std::uint64_t battleSeen, std::uint64_t nowMs)
	{
		std::vector<std::string> lines;
		if (inBattle != inBattle_) {
			inBattle_ = inBattle;
			if (inBattle) {
				watch_.LoadStarted(nowMs);
				loading_ = true;
				bound_ = battleSeen;
				lines.push_back("Battle load: started");
			} else {
				watch_.BattleClosed(nowMs);
				loading_ = false;
			}
		} else if (loading_ && battleSeen != bound_) {
			loading_ = false;
			Add(lines, watch_.FirstFrame(nowMs));
		}
		if (loading_) Add(lines, watch_.Tick(nowMs));
		return lines;
	}

private:
	static std::uint64_t Seconds(std::uint64_t ms) { return (ms + 500) / 1000; }

	static void Add(std::vector<std::string>& lines, const std::optional<sf4e::loadwatch::Report>& report)
	{
		if (!report) return;
		switch (report->kind) {
		case sf4e::loadwatch::ReportKind::StalledAt20s:
			lines.push_back("Battle load: no first frame after " + std::to_string(Seconds(sf4e::loadwatch::FirstStallMs)) + " s");
			break;
		case sf4e::loadwatch::ReportKind::StillStalledAt60s:
			lines.push_back("Battle load: still no first frame after " + std::to_string(Seconds(sf4e::loadwatch::SecondStallMs)) + " s");
			break;
		case sf4e::loadwatch::ReportKind::Recovered:
			lines.push_back("Battle load: first frame after " + std::to_string(Seconds(report->elapsedMs)) + " s");
			break;
		}
	}

	sf4e::loadwatch::LoadWatch watch_;
	bool inBattle_ = false;
	bool loading_ = false;
	std::uint64_t bound_ = 0; // the battle number when the load started
};

} // namespace sfve::mod
