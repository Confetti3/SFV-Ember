#include "mod/BalanceVersionRules.hxx"

#include "mod/SfvMenuLayouts.hxx"

#include <algorithm>

namespace sfve::mod {

std::optional<int32_t> ResolveVersion(const std::vector<int32_t>& versions, int32_t requested)
{
	namespace v = sfv::versions;
	if (requested >= 0) {
		if (requested >= v::kAnyVersionFirst && requested <= v::kAnyVersionLast) return requested;
		if (std::find(versions.begin(), versions.end(), requested) != versions.end()) return requested;
	}
	if (versions.empty() || versions.back() < 0) return std::nullopt;
	return versions.back();
}

VersionChoice ChooseLaunchVersion(std::string_view fighter, const std::optional<std::vector<int32_t>>& versions, int32_t wanted)
{
	const std::string name(fighter);
	if (!versions || versions->empty()) {
		return {kNewestBalanceVersion, "fighter " + name + "'s balance versions cannot be read on this copy (its game files may be "
										   "incomplete): verify the game's files"};
	}
	const int32_t newest = versions->back();
	if (wanted == kNewestBalanceVersion) {
		if (newest < 0) return {kNewestBalanceVersion, "fighter " + name + "'s newest balance version on this copy is not a version"};
		return {newest, {}};
	}
	if (std::find(versions->begin(), versions->end(), wanted) != versions->end()) return {wanted, {}};
	return {kNewestBalanceVersion, "fighter " + name + " has no balance version " + std::to_string(wanted) +
									   " on this copy (it has " + VersionListText(versions) +
									   "): verify the game's files or remove mods"};
}

std::string VersionListText(const std::optional<std::vector<int32_t>>& versions)
{
	if (!versions || versions->empty()) return "none";
	std::string text;
	for (const int32_t version : *versions) {
		if (!text.empty()) text += ", ";
		text += std::to_string(version);
	}
	return text;
}

} // namespace sfve::mod
