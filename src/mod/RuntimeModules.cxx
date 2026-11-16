#include "mod/RuntimeModules.hxx"

#include "mod/Log.hxx"
#include "platform/Utf8.hxx"

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <cwctype>
#include <stdio.h>
#include <vector>

namespace sfve::mod {

namespace {

std::wstring FolderOf(const std::wstring& path)
{
	const size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring Lower(std::wstring text)
{
	std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return c == L'/' ? L'\\' : (wchar_t)std::towlower(c); });
	return text;
}

std::wstring PathOf(HMODULE module)
{
	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
	return length && length < MAX_PATH ? std::wstring(path, length) : std::wstring();
}

} // namespace

std::string FileVersionText(const std::wstring& path)
{
	const DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
	if (!size) return {};
	std::vector<char> data(size);
	if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
	VS_FIXEDFILEINFO* info = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info || length < sizeof(*info)) return {};
	char text[64];
	snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
		HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
	return text;
}

bool InFolder(const std::wstring& path, const std::wstring& folder)
{
	return !folder.empty() && Lower(FolderOf(path)) == Lower(folder);
}

std::string DescribeModule(const wchar_t* name, const std::wstring& gameFolder)
{
	const HMODULE module = GetModuleHandleW(name);
	const std::string narrow = sf4e::platform::WideToUtf8(std::wstring(name));
	const std::wstring path = module ? PathOf(module) : std::wstring();
	if (path.empty()) return narrow + ": not loaded";
	const std::string version = FileVersionText(path);
	return narrow + " " + (version.empty() ? "(no version)" : version) + " at " + sf4e::platform::WideToUtf8(path) +
		(InFolder(path, gameFolder) ? ": the game's own folder's copy, which the mod's imports found before System32's" : "");
}

void LogRuntimeModules()
{
	const std::wstring gameFolder = FolderOf(PathOf(nullptr));
	for (const wchar_t* name : {L"msvcp140.dll", L"msvcp140_1.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"}) {
		Log("runtime: %s", DescribeModule(name, gameFolder).c_str());
	}
	Log("runtime: the mod was built with MSVC %d (STL %d), the dynamic runtime", _MSC_FULL_VER, _MSVC_STL_VERSION);
}

} // namespace sfve::mod
