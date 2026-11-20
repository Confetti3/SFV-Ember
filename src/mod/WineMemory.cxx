#include "mod/WineMemory.hxx"

#include "mod/AddressRanges.hxx"
#include "mod/GameHooks.hxx"
#include "mod/Log.hxx"

#include <windows.h>

#include <vector>

namespace sfve::mod {

namespace {

using NtAllocateVirtualMemoryFn = LONG(NTAPI*)(HANDLE process, PVOID* base, ULONG_PTR zeroBits, PSIZE_T size, ULONG type, ULONG protect);
NtAllocateVirtualMemoryFn o_NtAllocateVirtualMemory = nullptr;

// GetCurrentProcess's pseudo handle, which is what VirtualAlloc and the heap pass.
const HANDLE kThisProcess = reinterpret_cast<HANDLE>(-1);

LONG NTAPI h_NtAllocateVirtualMemory(HANDLE process, PVOID* base, ULONG_PTR zeroBits, PSIZE_T size, ULONG type, ULONG protect)
{
	if (!TopDownFits(process == kThisProcess, base && *base, zeroBits, type)) {
		return o_NtAllocateVirtualMemory(process, base, zeroBits, size, type, protect);
	}
	// Top down below 8 TB, not at the end of user space (see kTopDownZeroBits).
	const LONG status = o_NtAllocateVirtualMemory(process, base, kTopDownZeroBits, size, type | MEM_TOP_DOWN, protect);
	if (status >= 0) return status;
	// No room under the limit: the caller's own request, as before the hook.
	return o_NtAllocateVirtualMemory(process, base, zeroBits, size, type, protect);
}

} // namespace

bool RunningUnderWine()
{
	const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

bool TopDownFits(bool thisProcess, bool fixedAddress, uint64_t zeroBits, uint32_t allocationType)
{
	return thisProcess && !fixedAddress && zeroBits == 0 && (allocationType & (MEM_RESERVE | MEM_COMMIT)) != 0;
}

bool InstallWineMemory()
{
	if (!RunningUnderWine()) return true;
	const MH_STATUS init = MH_Initialize();
	if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
		Log("wine memory: MinHook did not initialize (%d); the heap stays low", (int)init);
		return false;
	}
	// The start ranges first: what exists before any allocation goes top down.
	std::vector<uint64_t> heaps;
	HANDLE handles[64];
	const DWORD count = GetProcessHeaps(64, handles);
	for (DWORD i = 0; i < count && i < 64; i++) heaps.push_back(reinterpret_cast<uint64_t>(handles[i]));
	const AddressRanges start = StartRanges(ReadReservations(), heaps);
	SetStartRanges(start);
	uint64_t bytes = 0;
	for (const AddressRange& r : start.Ranges()) bytes += r.end - r.begin;
	Log("wine memory: %zu start range(s) below 1 TB taken as addresses (%lu heap(s) and the loaded images, %llu MB)",
		start.Ranges().size(), count, (unsigned long long)(bytes >> 20));
	for (const AddressRange& r : start.Ranges()) {
		Log("wine memory:   0x%llX-0x%llX", (unsigned long long)r.begin, (unsigned long long)r.end);
	}

	HookTransaction hooks;
	hooks.Add("NtAllocateVirtualMemory", reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtAllocateVirtualMemory")),
		reinterpret_cast<void*>(&h_NtAllocateVirtualMemory), reinterpret_cast<void**>(&o_NtAllocateVirtualMemory));
	if (CommitHookSet(hooks, "wine memory")) return false;
	// Where a new allocation now lands, for the log: in [1 TB, 8 TB) is the point.
	void* probe = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	const uint64_t at = reinterpret_cast<uint64_t>(probe);
	if (probe) VirtualFree(probe, 0, MEM_RELEASE);
	Log("wine memory: allocations go top down below 8 TB (NtAllocateVirtualMemory hooked); a new one lands at 0x%llX%s",
		(unsigned long long)at,
		InTopDownRange(at) ? "" : at >= kWineUserSpace ? ", NOT below 8 TB: 43-bit pointers (XAudio2_7) would break"
			: ", NOT above 1 TB: the low-heap rule serves");
	return true;
}

} // namespace sfve::mod
