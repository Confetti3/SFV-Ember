#pragma once

// The player's Screen mode (mod/ScreenMode.hxx), saved as interface.json's
// "screenMode" in SFV Ember's settings folder, beside the Background
// artwork's choice (SfvBackdrop). The launcher reads it before it starts the
// game, so it takes effect at the next start; the mod's Look > Screen mode
// row shows and saves it. Both read and write the folder they are given
// (SettingsStore::DefaultDirectory in the program), so a test uses its own.

#include "mod/ScreenMode.hxx"

#include <filesystem>
#include <string>

namespace sfve::adapter {

// The saved mode: Game's setting when there is none, and when the file or
// its value does not read, with `problem` saying why (empty otherwise).
// Never throws; reads the file each time it is called.
screen::Mode ReadScreenMode(const std::filesystem::path& folder, std::string& problem);

// Saves `mode`, keeping interface.json's other choices. False, with `error`,
// when it could not be written; the file is then as it was.
bool WriteScreenMode(const std::filesystem::path& folder, screen::Mode mode, std::string& error);

// The mod's copy of the saved choice, read from the player's settings folder
// the first time it is asked. Any thread.
screen::Mode SfvScreenMode();
// Look's Screen mode row: saves the choice for the next start. False, the
// choice unchanged, when it could not be saved.
bool ChooseSfvScreenMode(screen::Mode mode);

} // namespace sfve::adapter
