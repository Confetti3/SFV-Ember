#include "mod/MultiInstance.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <intrin.h>

#include <string>

namespace sfve::mod {

namespace {

std::wstring s_suffix;
uintptr_t s_exeBegin, s_exeEnd;

// Only the game's own objects are renamed: Steam's libraries share named
// objects with the Steam client and must keep their names.
bool FromGame(void* returnAddress)
{
	const auto at = reinterpret_cast<uintptr_t>(returnAddress);
	return at >= s_exeBegin && at < s_exeEnd;
}

using CreateMutexW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
using CreateMutexExW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
using OpenMutexW_t = HANDLE(WINAPI*)(DWORD, BOOL, LPCWSTR);
CreateMutexW_t o_CreateMutexW;
CreateMutexExW_t o_CreateMutexExW;
OpenMutexW_t o_OpenMutexW;

// The per-instance name; logged the first time each name is seen.
std::wstring Rename(const char* api, LPCWSTR name)
{
	std::wstring renamed = std::wstring(name) + s_suffix;
	Log("multi-instance: %s '%ls' -> '%ls'", api, name, renamed.c_str());
	return renamed;
}

HANDLE WINAPI h_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCWSTR name)
{
	if (!name || !FromGame(_ReturnAddress())) return o_CreateMutexW(sa, owner, name);
	return o_CreateMutexW(sa, owner, Rename("CreateMutexW", name).c_str());
}

HANDLE WINAPI h_CreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access)
{
	if (!name || !FromGame(_ReturnAddress())) return o_CreateMutexExW(sa, name, flags, access);
	return o_CreateMutexExW(sa, Rename("CreateMutexExW", name).c_str(), flags, access);
}

HANDLE WINAPI h_OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name)
{
	if (!name || !FromGame(_ReturnAddress())) return o_OpenMutexW(access, inherit, name);
	return o_OpenMutexW(access, inherit, Rename("OpenMutexW", name).c_str());
}

} // namespace

std::optional<HookFailure> InstallMultiInstance(int instance)
{
	s_suffix = L"-sfve" + std::to_wstring(instance);
	const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
	s_exeBegin = reinterpret_cast<uintptr_t>(base);
	s_exeEnd = s_exeBegin + nt->OptionalHeader.SizeOfImage;
	// kernelbase (in every process) holds the real implementations; the A
	// variants forward to W. All three or none: with only some renamed, this
	// copy would create its own names for some mutexes and open the first
	// copy's for others.
	const HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
	HookTransaction hooks;
	hooks.Add("CreateMutexW", (void*)GetProcAddress(kernelbase, "CreateMutexW"), (void*)h_CreateMutexW,
		(void**)&o_CreateMutexW);
	hooks.Add("CreateMutexExW", (void*)GetProcAddress(kernelbase, "CreateMutexExW"), (void*)h_CreateMutexExW,
		(void**)&o_CreateMutexExW);
	hooks.Add("OpenMutexW", (void*)GetProcAddress(kernelbase, "OpenMutexW"), (void*)h_OpenMutexW, (void**)&o_OpenMutexW);
	const std::optional<HookFailure> missing = CommitHookSet(hooks, "multi-instance");
	Log("multi-instance: instance %d %s", instance,
		missing ? "renames no mutex, so the game may find the other copy and quit" : "renames named mutexes");
	return missing;
}

} // namespace sfve::mod
