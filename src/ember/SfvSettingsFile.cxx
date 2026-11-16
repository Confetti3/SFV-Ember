#include "ember/SfvSettingsFile.hxx"

#include "netplay/JsonFileStore.hxx"

#include <map>
#include <mutex>

namespace sfve::adapter {

namespace {

namespace json_file = sf4e::netplay::json_file;

// The lock of a file, by its path: made on first use and kept.
std::mutex& LockOf(const std::filesystem::path& file)
{
	static std::mutex s_locksMutex;
	static std::map<std::filesystem::path, std::mutex> s_locks;
	std::lock_guard lock(s_locksMutex);
	return s_locks[file];
}

// Under the file's lock.
nlohmann::json ReadLocked(const std::filesystem::path& folder, const wchar_t* name, std::string& problem)
{
	problem.clear();
	const std::string shown = std::filesystem::path(name).string();
	std::string bytes, error;
	bool missing = false;
	nlohmann::json settings;
	if (!json_file::ReadBytes(folder / name, bytes, missing, error)) {
		problem = shown + " does not read (" + error + ")";
		return nlohmann::json::object();
	}
	if (missing) return nlohmann::json::object();
	if (!json_file::Parse(bytes, settings, error) || !settings.is_object()) {
		problem = shown + " is no settings object (" + error + ")";
		return nlohmann::json::object();
	}
	return settings;
}

} // namespace

nlohmann::json ReadSettingsFile(const std::filesystem::path& folder, const wchar_t* name, std::string& problem)
{
	std::lock_guard lock(LockOf(folder / name));
	return ReadLocked(folder, name, problem);
}

bool UpdateSettingsFile(const std::filesystem::path& folder, const wchar_t* name,
	const std::function<void(nlohmann::json&)>& edit, std::string& error)
{
	std::lock_guard lock(LockOf(folder / name));
	// The folder exists wherever Ember has run; a fresh one (a test's) is made.
	std::error_code ignored;
	std::filesystem::create_directories(folder, ignored);
	std::string problem;
	nlohmann::json settings = ReadLocked(folder, name, problem);
	edit(settings);
	return json_file::Publish(folder, name, settings, error);
}

} // namespace sfve::adapter
