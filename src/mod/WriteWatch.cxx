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

namespace {

struct StackBounds {
	DWORD64 low = 0;
	DWORD64 high = 0;
};

// The thread's stack, from its TEB (NT_TIB: StackBase, then StackLimit).
StackBounds ThreadStackBounds(HANDLE thread)
{
	struct BasicInformation {
		LONG exitStatus;
		void* teb;
		void* clientId[2];
		ULONG_PTR affinity;
		LONG priority;
		LONG basePriority;
	} info{};
	using QueryFn = LONG(NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
	static const auto query =
		reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
	if (!query || query(thread, 0 /* ThreadBasicInformation */, &info, sizeof(info), nullptr) < 0 || !info.teb) return {};
	const NT_TIB* tib = static_cast<const NT_TIB*>(info.teb);
	return {reinterpret_cast<DWORD64>(tib->StackLimit), reinterpret_cast<DWORD64>(tib->StackBase)};
}

// Walks a suspended thread's frames. Nothing here may allocate or take a
// lock the suspended thread could hold (the unwinder's function-table lookup
// can still wait on ntdll's table lock if the thread was suspended while
// registering code; diagnostics only). Reads outside the stack end the walk.
int WalkFrames(CONTEXT context, StackBounds stack, DWORD64* frames, int maxFrames)
{
	int count = 0;
	__try {
		while (count < maxFrames && context.Rip) {
			frames[count++] = context.Rip;
			// A return address points after its call; look up the call itself.
			const DWORD64 lookup = count == 1 ? context.Rip : context.Rip - 1;
			DWORD64 imageBase = 0;
			PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(lookup, &imageBase, nullptr);
			if (!entry) {
				// Only the innermost frame can be a leaf function (return
				// address on top of the stack); further out it means no
				// unwind data, and the walk stops.
				if (count != 1 || context.Rsp < stack.low || context.Rsp + 8 > stack.high) break;
				context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
				context.Rsp += 8;
				continue;
			}
			void* handlerData = nullptr;
			DWORD64 establisher = 0;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, entry, &context, &handlerData, &establisher, nullptr);
			if (context.Rsp < stack.low || context.Rsp >= stack.high) break;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return count;
}

} // namespace

std::string ThreadStack(uint32_t threadId)
{
	if (threadId == GetCurrentThreadId()) return "error a thread cannot walk its own stack this way";
	constexpr int kMaxFrames = 64;
	DWORD64 frames[kMaxFrames];
	int count = 0;
	HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
	if (!thread) return "error cannot open the thread";
	const StackBounds stack = ThreadStackBounds(thread);
	if (!stack.high) {
		CloseHandle(thread);
		return "error cannot find the thread's stack";
	}
	if (SuspendThread(thread) == (DWORD)-1) {
		CloseHandle(thread);
		return "error cannot suspend the thread";
	}
	CONTEXT context{};
	context.ContextFlags = CONTEXT_FULL;
	if (GetThreadContext(thread, &context)) count = WalkFrames(context, stack, frames, kMaxFrames);
	ResumeThread(thread);
	CloseHandle(thread);
	std::string out;
	char item[160];
	for (int i = 0; i < count; i++) {
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(frames[i]), &module);
		const uintptr_t offset = frames[i] - reinterpret_cast<uintptr_t>(module);
		if (module == GetModuleHandleW(nullptr)) {
			snprintf(item, sizeof(item), " 0x%llX", (unsigned long long)offset);
		} else {
			char name[MAX_PATH] = "?";
			if (module) GetModuleFileNameA(module, name, sizeof(name));
			const char* file = strrchr(name, '\\');
			snprintf(item, sizeof(item), " %s+0x%llX", file ? file + 1 : name, (unsigned long long)offset);
		}
		out += item;
	}
	return out;
}

} // namespace sfve::mod
