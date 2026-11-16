#pragma once

// How often a session keeps a checkpoint: every kCheckpointInterval frames it
// also keeps one hash per region, so peers that disagree can find where
// (RollbackSession), and a replay records them for its playback to compare
// (ReplayFile). 20 from beta.9, 60 before: a checkpoint every third of a
// second of play instead of every second. A replay's recorded checkpoints,
// multiples of the interval it was recorded at, stay valid at any divisor of
// it, so a replay file is accepted when its checkpoints are multiples of
// this (beta.8's at 60 are), and a playback compares only the frames the file
// lists.

namespace sfve {

inline constexpr int kCheckpointInterval = 20;

} // namespace sfve
