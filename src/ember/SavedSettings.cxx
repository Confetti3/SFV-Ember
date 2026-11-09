#include "ember/SavedSettings.hxx"

#include "ember/PlayerName.hxx"
#include "mod/Log.hxx"

#include "netplay/PlayerPreferencesJson.hxx"
#include "netplay/SettingsStore.hxx"

#include <string>

namespace sfve::adapter {

std::optional<nlohmann::json> LoadSavedSettings(const char* what)
{
	nlohmann::json saved;
	std::string error;
	if (!sf4e::netplay::SettingsStore(sf4e::netplay::SettingsStore::DefaultDirectory()).LoadLauncher(saved, error)) {
		mod::Log("%s: the saved settings could not be read (%s)", what, error.c_str());
		return std::nullopt;
	}
	return saved;
}

nlohmann::json PreferencesToSave(const sf4e::netplay::PlayerPreferences& preferences, int instance)
{
	nlohmann::json values = sf4e::netplay::PlayerPreferencesJson(preferences);
	if (TestCopy(instance)) values.erase("displayName");
	return values;
}

} // namespace sfve::adapter
