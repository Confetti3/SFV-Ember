#pragma once

// Which build's replays this copy plays (EMBER_LIBRARY.md section 26). A
// replay plays only on the build that recorded it (its header's build, the
// room's MatchBuildId), unless the development option -sfve_replay_any_build
// is set. Watch now and the Replays list use this one rule.
//
// The Replays screen's rows (ArchivedReplay) carry no state and no second
// line, so a replay of another build cannot be shown as one; the list leaves
// it out and the file stays where it is. A file whose header cannot be read
// has no build here and is dealt with as before (the list does not name it,
// Watch now refuses it).

#include <optional>
#include <string>

namespace sfve::adapter {

// Whether a replay recorded on `recorded` plays on `current`.
inline bool PlaysOnBuild(const std::string& recorded, const std::string& current, bool anyBuild)
{
	return anyBuild || recorded == current;
}

// Whether the list leaves the replay out for its build: its header was read
// and names another build than this copy's. No build (an unreadable header)
// is never left out here.
inline bool LeftOutForBuild(const std::optional<std::string>& recorded, const std::string& current, bool anyBuild)
{
	return recorded && !PlaysOnBuild(*recorded, current, anyBuild);
}

} // namespace sfve::adapter
