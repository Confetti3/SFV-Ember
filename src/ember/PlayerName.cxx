#include "ember/PlayerName.hxx"

#include "mod/Log.hxx"

#include "netplay/SettingsStore.hxx"

#include <nlohmann/json.hpp>

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
	nlohmann::json saved;
	std::string error;
	if (!sf4e::netplay::SettingsStore(sf4e::netplay::SettingsStore::DefaultDirectory()).LoadLauncher(saved, error)) {
		mod::Log("ember: the saved settings could not be read (%s); no saved name", error.c_str());
		return {};
	}
	const auto name = saved.find("displayName");
	return name != saved.end() && name->is_string() ? name->get<std::string>() : std::string();
}

} // namespace sfve::adapter
