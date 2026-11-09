#pragma once

// The one Street Fighter V build SFV Ember runs on: the exe's PE timestamp
// and its name for players. The mod's BuildCheck verifies it in the running
// game (SfvOffsets' VerifiedBuild); the launcher refuses any other exe before
// it starts (SfvLaunch, ExecutableBuild).

#include <stdint.h>

namespace sfve::mod::sfv {

constexpr uint32_t kExeTimestamp = 0x648AD3A4;
constexpr const char* kBuildLabel = "SFV CE build 13173604";

} // namespace sfve::mod::sfv
