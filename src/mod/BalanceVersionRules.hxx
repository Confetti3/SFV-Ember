#pragma once

// Which balance version a launch gives each fighter (docs/design/beta10/
// desync.md, "Balance versions"). Pure: no game memory.
//
// SFV keeps every balance version of a fighter's moves its updates shipped,
// and a battle plays the one the battle setting's player record names
// (sfv::jump::kSettingVersion). The game resolves that number against the
// copy's own version list (GameContent's FighterBalanceVersions, from the
// install's paks), and -1, or any version the list lacks, becomes the list's
// last entry. So a launch that leaves the number to the game plays each PC's
// own newest version, and two PCs whose lists end differently play the same
// match with different frame data. Every launch therefore names the version
// itself, and a copy that does not have it refuses the launch instead.

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::mod {

// A launch's version for a fighter: this copy's newest. A replay names the
// recorded one (replay::Player::balanceVersion).
constexpr int32_t kNewestBalanceVersion = -1;

// What the game resolves `requested` to against `versions` (oldest first),
// as rva 0x56B2E0 does: a version the list holds stays; one from
// sfv::versions::kAnyVersionFirst to kAnyVersionLast stays; anything else,
// and a negative one, becomes the last entry's, which must itself not be
// negative. None when it cannot resolve (an empty list).
std::optional<int32_t> ResolveVersion(const std::vector<int32_t>& versions, int32_t requested);

// The version a launch writes for `fighter`, or why it cannot: `wanted` is
// kNewestBalanceVersion or a version to play exactly. `versions` is this
// copy's list (none when it could not be read). Only a version the list
// holds is ever launched: the game's development range is never asked for.
struct VersionChoice {
	int32_t version = kNewestBalanceVersion; // when refusal is empty
	std::string refusal;                     // after "player N's ": empty when it launches
};
VersionChoice ChooseLaunchVersion(std::string_view fighter, const std::optional<std::vector<int32_t>>& versions, int32_t wanted);

// "60, 61" (the newest last), or "none".
std::string VersionListText(const std::optional<std::vector<int32_t>>& versions);

} // namespace sfve::mod
