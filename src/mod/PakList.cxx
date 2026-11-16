#include "mod/PakList.hxx"

#include "mod/Log.hxx"
#include "mod/PakRules.hxx"

#include <windows.h>

#include <stdlib.h>
#include <wchar.h>

#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

namespace sfve::mod {

namespace {

namespace fs = std::filesystem;

std::mutex s_mutex;
std::string s_summary = "not taken";

// The folders the engine mounts paks from (PakRules.hxx), from the game's
// exe (<game>/StreetFighterV/Binaries/Win64): its Content and the engine's,
// and the game's Saved folder both where an install keeps it and in the
// user's local application data, where SFV's installed build keeps it.
std::vector<std::pair<paks::Folder, fs::path>> PakFolders()
{
	wchar_t exe[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::vector<std::pair<paks::Folder, fs::path>> folders;
	if (length && length < MAX_PATH) {
		const fs::path game = fs::path(exe).parent_path().parent_path().parent_path(); // <game>/StreetFighterV
		folders.push_back({paks::Folder::Content, game / L"Content" / L"Paks"});
		folders.push_back({paks::Folder::Engine, game.parent_path() / L"Engine" / L"Content" / L"Paks"});
		folders.push_back({paks::Folder::Saved, game / L"Saved" / L"Paks"});
	}
	wchar_t local[MAX_PATH] = {};
	const DWORD localLength = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
	if (localLength && localLength < MAX_PATH) {
		folders.push_back({paks::Folder::Saved, fs::path(local) / L"StreetFighterV" / L"Saved" / L"Paks"});
	}
	return folders;
}

std::string Utf8(const std::wstring& text)
{
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	std::string out(size > 0 ? (size_t)size : 0, '\0');
	if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size, nullptr, nullptr);
	return out;
}

std::vector<paks::Found> FindPaks()
{
	std::vector<paks::Found> found;
	for (const auto& [folder, root] : PakFolders()) {
		std::error_code error;
		if (!fs::is_directory(root, error)) continue;
		for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; !error && it != end;
			 it.increment(error)) {
			if (!it->is_regular_file(error) || _wcsicmp(it->path().extension().c_str(), L".pak") != 0) continue;
			const uint64_t size = it->file_size(error);
			if (error) {
				error.clear();
				continue;
			}
			found.push_back({folder, Utf8(fs::relative(it->path(), root, error).generic_wstring()), size});
		}
		if (error) Log("paks: %ls could not be read whole (%s)", root.c_str(), error.message().c_str());
	}
	return found;
}

} // namespace

void TakePakCensus()
{
	const paks::Census census = paks::Judge(FindPaks());
	for (const paks::Judged& judged : census.paks) Log("paks: %s", paks::Line(judged).c_str());
	const std::string summary = paks::Summary(census);
	Log("paks: %s%s", summary.c_str(), paks::AllStock(census) ? "" : " (this copy may compute matches with other game data)");
	std::lock_guard lock(s_mutex);
	s_summary = summary;
}

std::string PakCensusSummary()
{
	std::lock_guard lock(s_mutex);
	return s_summary;
}

} // namespace sfve::mod
