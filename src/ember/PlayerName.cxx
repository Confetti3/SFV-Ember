#include "ember/PlayerName.hxx"

#include "ember/SavedSettings.hxx"

namespace sfve::adapter {

bool TestCopy(int instance)
{
	return instance > 0;
}

std::string PlayerNameFor(int instance, const std::string& saved)
{
	return TestCopy(instance) ? "SFV " + std::to_string(instance + 1) : saved;
}

std::string SavedDisplayName()
{
	const std::optional<nlohmann::json> saved = LoadSavedSettings("the player's name");
	if (!saved) return {};
	const auto name = saved->find("displayName");
	return name != saved->end() && name->is_string() ? name->get<std::string>() : std::string();
}

} // namespace sfve::adapter
