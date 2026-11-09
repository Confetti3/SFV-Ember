#include "ember/PlayerName.hxx"

#include "ember/SavedSettings.hxx"
#include "launcher/OldDefaultName.hxx"

namespace sfve::adapter {

bool TestCopy(int instance)
{
	return instance > 0;
}

std::string PlayerNameFor(int instance, const std::string& saved)
{
	if (TestCopy(instance)) return "SFV " + std::to_string(instance + 1);
	// 0.1.0-beta.1's hard-coded names are nobody's choice, and a test copy
	// plays under one of them: as the launcher does, they count as unset.
	return launcher::IsOldDefaultName(saved.c_str()) ? std::string() : saved;
}

std::string SavedDisplayName()
{
	const std::optional<nlohmann::json> saved = LoadSavedSettings("the player's name");
	if (!saved) return {};
	const auto name = saved->find("displayName");
	return name != saved->end() && name->is_string() ? name->get<std::string>() : std::string();
}

} // namespace sfve::adapter
