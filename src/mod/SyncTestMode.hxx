#pragma once

// synctest: every frame through SyncTester, which saves, rolls back
// `distance` frames every `distance` frames and replays them, reporting the
// first difference (or, learning, tallying every region that differs). The
// inputs are what the game computed for each pad, so virtual pads, keyboards
// and button layouts all work; with a mash seed both players instead mash
// (seeded, from frame `mashFrom` of the battle), as two linked players do.

#include "core/SyncTest.hxx"
#include "mod/BattleMode.hxx"
#include "mod/ModConfig.hxx"

#include <memory>
#include <set>

namespace sfve::mod {

class SyncTestMode : public BattleMode {
public:
	explicit SyncTestMode(const ModConfig& config) : config_(config) {}

	void NewBattle(SfvAdapter& adapter) override;
	void Frame(SfvAdapter& adapter) override;
	void Restart() override { restart_ = true; }
	std::string Describe() const override;

private:
	FrameInputs Inputs(SfvAdapter& adapter) const;
	void LogFailure() const;
	void LogNewRegions(const SfvAdapter& adapter);

	const ModConfig config_;
	bool restart_ = false;
	bool failureLogged_ = false;
	int failuresLogged_ = 0;
	std::unique_ptr<SyncTester> tester_;
	std::set<std::string> learnedLogged_;
};

} // namespace sfve::mod
