#pragma once

// What a session drives once its game is gone (RollbackSession::
// KeepForWatchers): P1's session kept after the fight so its watchers get the
// frames still on their way, while the game frees the battle and goes back
// to the main menu. GGPO may still roll that session back (the other
// fighter's drop replays the frames after the last input it sent), and the
// callbacks then reach this stand-in instead of the freed battle: it has no
// state (saves and loads copy nothing), its frames do nothing and read no
// inputs, and it never fails.

#include "core/GameAdapter.hxx"

namespace sfve {

class DetachedGame final : public GameAdapter {
public:
	std::string BuildId() const override { return "detached"; }
	StateLayout& Layout() override { return layout_; }
	void Advance(const FrameInputs&, bool) override {}
	bool InputsMatter() const override { return false; }

private:
	StateLayout layout_; // empty
};

} // namespace sfve
