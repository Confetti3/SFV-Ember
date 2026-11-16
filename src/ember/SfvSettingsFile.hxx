#pragma once

// One small JSON settings file in SFV Ember's settings folder (interface.json,
// training.json), edited by whoever owns one of its keys: the Background
// artwork's choice, the Screen mode, the viewer's hitboxes. Each owner changes
// its own key and keeps the others, so every change is one step under one
// lock per file: read the file, edit its object, publish it. Two owners on two
// threads can no longer each write the file from what it held before the
// other's write. The launcher reads the same files without the mod, so this
// has no log. Any thread.

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace sfve::adapter {

// The file's object: empty when there is none (no problem) or it does not
// read as one, with `problem` saying why ("interface.json does not read (..)",
// "interface.json is no settings object (..)"; empty otherwise).
nlohmann::json ReadSettingsFile(const std::filesystem::path& folder, const wchar_t* name, std::string& problem);

// Reads the file's object (empty when there is none or it does not read: the
// edit writes a fresh one), lets `edit` change it and publishes it, as one
// step. False, with `error`, when it could not be written; the file is then as
// it was.
bool UpdateSettingsFile(const std::filesystem::path& folder, const wchar_t* name,
	const std::function<void(nlohmann::json&)>& edit, std::string& error);

} // namespace sfve::adapter
