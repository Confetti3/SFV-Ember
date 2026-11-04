#include "mod/WriteWatch.hxx"

#include "mod/Log.hxx"

#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace sfve::mod {

namespace {

std::mutex s_mutex;
std::map<uintptr_t, uint64_t> s_hits; // rva -> count
uintptr_t s_base;
uintptr_t s_size; // writes from outside the game (our own save/restore copies) count as rva 0
PVOID s_handler;

LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	// A data breakpoint fires after the write: the address is the next
	// instruction; good enough to find the writing function.
	const uintptr_t ip = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	const uintptr_t at = ip >= s_base && ip < s_base + s_size ? ip - s_base : 0;
	{
		std::lock_guard lock(s_mutex);
		s_hits[at]++;
	}
	info->ContextRecord->Dr6 = 0;
	return EXCEPTION_CONTINUE_EXECUTION;
}

struct Request {
	uint32_t threadId;
	uintptr_t address;
	int length;
	bool arm;
};

// Debug registers of another thread are set while it is suspended.
DWORD WINAPI Apply(void* param)
{
	const Request* r = static_cast<Request*>(param);
	HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, r->threadId);
	if (!thread) return 1;
	SuspendThread(thread);
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
	GetThreadContext(thread, &ctx);
	if (r->arm) {
		const DWORD64 lengthBits = r->length == 8 ? 2 : r->length == 4 ? 3 : r->length == 2 ? 1 : 0;
		ctx.Dr0 = r->address;
		ctx.Dr7 = (ctx.Dr7 & ~0xF0003ull) | 1 /* L0 */ | (1ull << 16) /* write */ | (lengthBits << 18);
	} else {
		ctx.Dr0 = 0;
		ctx.Dr7 &= ~0xF0003ull;
	}
	SetThreadContext(thread, &ctx);
	ResumeThread(thread);
	CloseHandle(thread);
	return 0;
}

bool Run(Request r)
{
	HANDLE helper = CreateThread(nullptr, 0, Apply, &r, 0, nullptr);
	if (!helper) return false;
	WaitForSingleObject(helper, 5000);
	DWORD code = 1;
	GetExitCodeThread(helper, &code);
	CloseHandle(helper);
	return code == 0;
}

} // namespace

bool ArmWriteWatch(uint32_t gameThreadId, uintptr_t address, int length)
{
	if (!s_handler) {
		s_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(s_base);
		s_size = reinterpret_cast<const IMAGE_NT_HEADERS*>(s_base + dos->e_lfanew)->OptionalHeader.SizeOfImage;
		s_handler = AddVectoredExceptionHandler(1, OnException);
	}
	{
		std::lock_guard lock(s_mutex);
		s_hits.clear();
	}
	const bool ok = Run({gameThreadId, address, length, true});
	Log("watch: %s %d byte(s) at %p", ok ? "armed on" : "could not arm", length, (void*)address);
	return ok;
}

void DisarmWriteWatch(uint32_t gameThreadId)
{
	Run({gameThreadId, 0, 0, false});
}

std::string WriteWatchHits()
{
	std::vector<std::pair<uint64_t, uintptr_t>> hits;
	{
		std::lock_guard lock(s_mutex);
		for (const auto& [rva, count] : s_hits) hits.emplace_back(count, rva);
	}
	std::sort(hits.rbegin(), hits.rend());
	std::string out;
	char item[48];
	for (const auto& [count, rva] : hits) {
		snprintf(item, sizeof(item), "%s0x%llX:%llu", out.empty() ? "" : " ", (unsigned long long)rva,
			(unsigned long long)count);
		out += item;
	}
	return out;
}

} // namespace sfve::mod
