#include "ember/SfvScreenMode.hxx"

#include "netplay/JsonFileStore.hxx"
#include "netplay/SettingsStore.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>

namespace sfve::adapter {

namespace {

namespace json_file = sf4e::netplay::json_file;

constexpr const wchar_t* kChoiceFile = L"interface.json";

// interface.json's object: empty when there is none or it does not read as
// one. `problem` says which of the two; a missing file is no problem.
nlohmann::json ReadChoices(const std::filesystem::path& folder, std::string& problem)
{
	problem.clear();
	std::string bytes, error;
	bool missing = false;
	nlohmann::json choices;
	if (!json_file::ReadBytes(folder / kChoiceFile, bytes, missing, error)) {
		problem = "interface.json does not read (" + error + ")";
		return nlohmann::json::object();
	}
	if (missing) return nlohmann::json::object();
	if (!json_file::Parse(bytes, choices, error) || !choices.is_object()) {
		problem = "interface.json is no settings object (" + error + ")";
		return nlohmann::json::object();
	}
	return choices;
}

std::mutex s_mutex;
bool s_loaded = false;
std::atomic<int> s_mode{static_cast<int>(screen::Mode::Game)};

void LoadLocked()
{
	if (s_loaded) return;
	s_loaded = true;
	std::string problem;
	s_mode = static_cast<int>(ReadScreenMode(std::filesystem::path(sf4e::netplay::SettingsStore::DefaultDirectory()), problem));
}

} // namespace

screen::Mode ReadScreenMode(const std::filesystem::path& folder, std::string& problem)
{
	const nlohmann::json choices = ReadChoices(folder, problem);
	const auto saved = choices.find(screen::kChoiceKey);
	if (saved == choices.end()) return screen::Mode::Game;
	if (saved->is_string()) {
		if (const auto mode = screen::Parse(saved->get<std::string>())) return *mode;
	}
	problem = std::string("interface.json's ") + screen::kChoiceKey + " is not one of game, window or borderless";
	return screen::Mode::Game;
}

bool WriteScreenMode(const std::filesystem::path& folder, screen::Mode mode, std::string& error)
{
	// The folder exists wherever Ember has run; a fresh one (a test's) is made.
	std::error_code ignored;
	std::filesystem::create_directories(folder, ignored);
	std::string problem;
	nlohmann::json choices = ReadChoices(folder, problem);
	choices[screen::kChoiceKey] = screen::Name(mode);
	return json_file::Publish(folder, kChoiceFile, choices, error);
}

screen::Mode SfvScreenMode()
{
	std::lock_guard lock(s_mutex);
	LoadLocked();
	return static_cast<screen::Mode>(s_mode.load());
}

bool ChooseSfvScreenMode(screen::Mode mode)
{
	std::lock_guard lock(s_mutex);
	LoadLocked();
	std::string error;
	if (!WriteScreenMode(std::filesystem::path(sf4e::netplay::SettingsStore::DefaultDirectory()), mode, error)) return false;
	s_mode = static_cast<int>(mode);
	return true;
}

} // namespace sfve::adapter
