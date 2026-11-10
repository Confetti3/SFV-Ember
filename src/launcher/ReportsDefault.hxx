#pragma once

// SFV Ember sends problem reports, crashes' and desyncs', unless the player
// turned them off (EMBER_LIBRARY.md section 51). The library's default is
// off (PlayerPreferences::sendProblemReports: SF4 Ember asks first), and a
// player who ever saved their settings has that default saved too, so it
// cannot tell a choice from a default. SFV's launcher therefore turns the
// setting on once, before its first game, and records that it did
// (kReportsDefaultKey); from then on it is the player's setting like any
// other, shown in Settings, Problem reports.

#include <string>

namespace sfve::launcher {

// The record that the launcher turned problem reports on once.
inline constexpr const char* kReportsDefaultKey = "sfvProblemReportsOnByDefault";

// Turns Send problem reports on in `settingsDirectory` once, unless this was
// done before; true when the setting is on afterwards or was left as the
// player has it since. False when the settings cannot be read or saved
// (the launcher logs it; nothing else depends on it).
bool ApplyReportsDefault(const std::wstring& settingsDirectory);

} // namespace sfve::launcher
