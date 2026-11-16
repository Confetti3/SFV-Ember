#include "ember/SfvScreenMode.hxx"

#include "ember/SfvSettingsFile.hxx"

#include "netplay/SettingsStore.hxx"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>

namespace sfve::adapter {

namespace {

constexpr const wchar_t* kChoiceFile = L"interface.json";

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
	const nlohmann::json choices = ReadSettingsFile(folder, kChoiceFile, problem);
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
	return UpdateSettingsFile(folder, kChoiceFile, [mode](nlohmann::json& choices) { choices[screen::kChoiceKey] = screen::Name(mode); }, error);
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
