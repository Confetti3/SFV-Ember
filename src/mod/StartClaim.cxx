#include "mod/StartClaim.hxx"

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace sfve::mod {

namespace {

struct Claim {
	wchar_t module[MAX_PATH];
	char loader[32];
};

// Held for the process's life: the section must outlive every later check.
HANDLE s_section;

std::wstring OwnModule()
{
	HMODULE self = nullptr;
	wchar_t path[MAX_PATH] = {};
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&OwnModule), &self)) {
		GetModuleFileNameW(self, path, MAX_PATH);
	}
	return path;
}

} // namespace

StartClaimResult ClaimStart(const char* loader)
{
	StartClaimResult result;
	result.module = OwnModule();
	wchar_t name[64];
	swprintf_s(name, L"Local\\SFVEmber.Mod.%lu", GetCurrentProcessId());
	HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Claim), name);
	if (!section) {
		// No section, no way to know of another copy: start, and say so.
		result.won = true;
		result.error = GetLastError();
		return result;
	}
	const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
	auto* claim = static_cast<Claim*>(MapViewOfFile(section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(Claim)));
	if (!claim) {
		result.won = !existed;
		result.error = GetLastError();
		if (existed) CloseHandle(section);
		else s_section = section;
		return result;
	}
	if (existed) {
		result.won = false;
		result.winnerModule.assign(claim->module, wcsnlen(claim->module, MAX_PATH));
		result.winnerLoader.assign(claim->loader, strnlen(claim->loader, sizeof(claim->loader)));
		UnmapViewOfFile(claim);
		CloseHandle(section);
		return result;
	}
	wcsncpy_s(claim->module, result.module.c_str(), _TRUNCATE);
	strncpy_s(claim->loader, loader, _TRUNCATE);
	UnmapViewOfFile(claim);
	s_section = section;
	result.won = true;
	return result;
}

} // namespace sfve::mod
