#pragma once

// Which build an executable on disk is, read before it starts: its PE
// header's TimeDateStamp, the stamp the mod's BuildCheck verifies in the
// running game. SFV Ember's launcher starts only the build its mod was made
// for (SfvLaunch's StartGame), never another one and never UE4's starter of
// the same name in the game's top folder (EMBER_LIBRARY.md section 42).

#include <stdint.h>

#include <optional>
#include <string>

namespace sfve::launcher {

// The file's PE TimeDateStamp; empty when it cannot be read or is no PE image.
std::optional<uint32_t> ExecutableTimestamp(const std::wstring& path);

} // namespace sfve::launcher
