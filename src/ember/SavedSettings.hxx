#pragma once

// The player's saved settings (settings.json's launcher section, the
// library's SettingsStore) as SFV Ember's mod reads them at start and writes
// them back (SfvPreferences).

#include "netplay/PlayerPreferences.hxx"

#include <nlohmann/json.hpp>

#include <optional>

namespace sfve::adapter {

// The saved settings; empty, logged for `what` needed them, when they cannot
// be read.
std::optional<nlohmann::json> LoadSavedSettings(const char* what);

// The preferences as this copy writes them. A test copy (instance N > 0)
// shares the player's settings, so its own name (PlayerName.hxx) is left
// out and the player's stays.
nlohmann::json PreferencesToSave(const sf4e::netplay::PlayerPreferences& preferences, int instance);

} // namespace sfve::adapter
