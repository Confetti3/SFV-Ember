#include "ember/SavedSettings.hxx"

#include "ember/PlayerName.hxx"
#include "mod/Log.hxx"

#include "netplay/PlayerPreferencesJson.hxx"
#include "netplay/SettingsStore.hxx"

#include <windows.h>

#include <string>

namespace sfve::adapter {

std::optional<nlohmann::json> LoadSavedSettings(const char* what)
{
	// As the library's launcher reads them (netplay_persist.cxx): a file another process holds for a moment is
	// tried again after short waits, here on the calling thread since the answer is wanted now. A damaged file
	// is left alone: the runtime's own read (netplay/SavedRead.hxx) sets it aside and tells the player once,
	// which a read of ours that set it aside first would take from it. No notice from here.
	const sf4e::netplay::SettingsStore store(sf4e::netplay::SettingsStore::DefaultDirectory());
	nlohmann::json saved;
	std::string error;
	for (int attempt = 0;; ++attempt) {
		sf4e::netplay::ReadFailure why = sf4e::netplay::ReadFailure::None;
		if (store.LoadLauncher(saved, error, &why)) return saved;
		if (attempt >= 3 || (why != sf4e::netplay::ReadFailure::Busy && why != sf4e::netplay::ReadFailure::Unreadable)) break;
		Sleep(150u << attempt);
	}
	mod::Log("%s: the saved settings could not be read (%s)", what, error.c_str());
	return std::nullopt;
}

nlohmann::json PreferencesToSave(const sf4e::netplay::PlayerPreferences& preferences, int instance)
{
	nlohmann::json values = sf4e::netplay::PlayerPreferencesJson(preferences);
	if (TestCopy(instance)) values.erase("displayName");
	return values;
}

} // namespace sfve::adapter
