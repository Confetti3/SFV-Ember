#pragma once

// synctest: every frame through SyncTester, which saves, rolls back
// `distance` frames every `distance` frames and replays them, reporting the
// first difference (or, learning, tallying every region that differs). The
// inputs are what the game computed for each pad, so virtual pads, keyboards
// and button layouts all work.

#include "core/SyncTest.hxx"
#include "mod/BattleMode.hxx"

#include <memory>
#include <set>

namespace sfve::mod {

class SyncTestMode : public BattleMode {
public:
	SyncTestMode(int distance, bool learn, bool perturb) : distance_(distance), learn_(learn), perturb_(perturb) {}

	void NewBattle(SfvAdapter& adapter) override;
	void Frame(SfvAdapter& adapter) override;
	void Restart() override { restart_ = true; }
	std::string Describe() const override;

private:
	void LogFailure() const;
	void LogNewRegions(const SfvAdapter& adapter);

	int distance_;
	bool learn_;
	bool perturb_;
	bool restart_ = false;
	bool failureLogged_ = false;
	int failuresLogged_ = 0;
	std::unique_ptr<SyncTester> tester_;
	std::set<std::string> learnedLogged_;
};

} // namespace sfve::mod
