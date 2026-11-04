#pragma once

// The mod touches game code only on the exact build its offsets were derived
// from: the exe's PE timestamp must match, and every function it hooks or
// calls must start with the bytes recorded for it. Anything else and the mod
// stays inert (the network guard still runs).

#include <stddef.h>
#include <stdint.h>

#include <string>

#include <windows.h>

namespace sfve::mod {

struct CodeSite {
	const char* name;
	uint32_t rva;
	uint8_t bytes[16];
};

struct GameBuild {
	const char* label;
	uint32_t peTimestamp;
	const CodeSite* sites;
	size_t siteCount;
};

enum class BuildState {
	NoBuildTable,  // no offsets have been derived yet
	Mismatch,      // a different exe, or a site's bytes differ
	Verified,
};

bool ImageTimestamp(HMODULE image, uint32_t* out);
BuildState CheckBuild(HMODULE exe, const GameBuild* build, std::string* detail);
const char* BuildStateName(BuildState state);

} // namespace sfve::mod
