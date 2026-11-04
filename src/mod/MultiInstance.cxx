#include "mod/MultiInstance.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <intrin.h>

#include <MinHook.h>

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

bool Hook(const char* name, void* detour, void** original)
{
	void* target = nullptr;
	// kernelbase holds the real implementations; the A variants forward to W.
	MH_STATUS st = MH_CreateHookApiEx(L"kernelbase.dll", name, detour, original, &target);
	if (st == MH_OK) st = MH_EnableHook(target);
	if (st != MH_OK) Log("multi-instance: could not hook %s (%d)", name, (int)st);
	return st == MH_OK;
}

} // namespace

bool InstallMultiInstance(int instance)
{
	if (instance <= 0) return true;
	s_suffix = L"-sfve" + std::to_wstring(instance);
	const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
	s_exeBegin = reinterpret_cast<uintptr_t>(base);
	s_exeEnd = s_exeBegin + nt->OptionalHeader.SizeOfImage;
	const bool ok = Hook("CreateMutexW", (void*)h_CreateMutexW, (void**)&o_CreateMutexW) &&
		Hook("CreateMutexExW", (void*)h_CreateMutexExW, (void**)&o_CreateMutexExW) &&
		Hook("OpenMutexW", (void*)h_OpenMutexW, (void**)&o_OpenMutexW);
	Log("multi-instance: instance %d %s", instance, ok ? "renames named mutexes" : "INCOMPLETE");
	return ok;
}

} // namespace sfve::mod
