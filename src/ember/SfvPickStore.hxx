#pragma once

// Keeps the player's pick memory (SfvPick, SfvPickMemory: each fighter's look
// and the fighter last chosen) between runs, in the "sfvPicks" key of the
// settings file's overlay section (the library's SettingsStore; SF4 Ember
// keeps its per-fighter picks in that section too). Read once, on the first
// game frame after the catalog is filled, since a saved look is judged
// against the pick space; written a moment after the memory last changed, and
// once more as the game exits. A copy that is a test copy (instance above 0)
// reads but never writes, so it cannot replace the player's picks.
//
// Game thread.

namespace sfve::adapter {

// Starts the per-frame work.
void InstallSfvPickStore();
// Writes what has changed since the last write; the game's exit (EmberHost's
// StopAtExit) calls it. Nothing more is written after it.
void FlushSfvPickStore();

} // namespace sfve::adapter
