#pragma once

// What the mod does with each rendered frame of a bound battle. One mode per
// process (-sfve_mode); BattleDriver owns the battle's identity, binding,
// inventory and status, and hands each frame to the mode.

#include "mod/SfvAdapter.hxx"

#include <string>

namespace sfve::mod {

class BattleMode {
public:
	virtual ~BattleMode() = default;
	// Once, from SfveStart, after the driver holds the mode for the process's
	// life: the mode's own pipe commands and whatever it hosts.
	virtual void Install() {}
	// A battle (re)started binding: forget the previous one.
	virtual void NewBattle(SfvAdapter& adapter) = 0;
	// One rendered frame. The mode decides how the battle advances: the
	// original update, an Advance, or not at all (waiting for a peer).
	virtual void Frame(SfvAdapter& adapter) = 0;
	// The pipe asked for a fresh start of the mode's work on this battle.
	virtual void Restart() {}
	// JSON value for the status report ("null" when there is nothing yet).
	virtual std::string Describe() const = 0;
};

// The original battle, untouched (probe mode).
class PassThroughMode : public BattleMode {
public:
	void NewBattle(SfvAdapter&) override {}
	void Frame(SfvAdapter& adapter) override { adapter.RunOriginal(); }
	std::string Describe() const override { return "null"; }
};

} // namespace sfve::mod
