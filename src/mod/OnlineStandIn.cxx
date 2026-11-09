#include "mod/OnlineStandIn.hxx"

#include "mod/GameHooks.hxx"
#include "mod/Log.hxx"
#include "mod/SfvOffsets.hxx"

#include <array>
#include <atomic>
#include <cstring>

namespace sfve::mod {

namespace {

// The game's GetOnlineSubsystem takes nothing it uses; the four registers are
// passed on as they came, so the original sees exactly its own call.
using GetOnlineSubsystemFn = void* (*)(void*, void*, void*, void*);

GetOnlineSubsystemFn o_getOnlineSubsystem;
std::atomic<uint32_t> s_uses;
std::atomic<bool> s_logged;

// A getter's 16-byte result, returned through `out`: cleared, it is a null
// TSharedPtr or an empty FString.
void* ClearResult(void* /*self*/, void* out)
{
	if (out) std::memset(out, 0, 16);
	return out;
}

// Any slot the game's callers do not use (none is known to be called).
uintptr_t ReturnZero()
{
	return 0;
}

constexpr size_t kSlots = 128;
// The slots the callers use (OnlineStandIn.hxx): +0x08, +0x50 and +0xE8.
constexpr size_t kClearedSlots[] = {0x08 / 8, 0x50 / 8, 0xE8 / 8};

std::array<const void*, kSlots> MakeVtable()
{
	std::array<const void*, kSlots> slots{};
	slots.fill(reinterpret_cast<const void*>(&ReturnZero));
	for (size_t slot : kClearedSlots) slots[slot] = reinterpret_cast<const void*>(&ClearResult);
	return slots;
}

const std::array<const void*, kSlots> s_vtable = MakeVtable();

// The stand-in subsystem: an object is its vtable pointer.
struct StandIn {
	const void* const* vtable;
};
const StandIn s_standIn{s_vtable.data()};

void* h_GetOnlineSubsystem(void* a, void* b, void* c, void* d)
{
	void* subsystem = o_getOnlineSubsystem(a, b, c, d);
	if (subsystem) return subsystem;
	++s_uses;
	if (!s_logged.exchange(true)) {
		Log("online: the game has no online subsystem (Steam's sign-in did not reach it); "
			"it gets the offline stand-in, so it runs as not logged in");
	}
	return const_cast<StandIn*>(&s_standIn);
}

} // namespace

bool InstallOnlineStandIn(uintptr_t imageBase)
{
	HookTransaction hooks;
	hooks.Add(imageBase, sfv::kGetOnlineSubsystem, (void*)h_GetOnlineSubsystem, (void**)&o_getOnlineSubsystem);
	const bool ok = !CommitHookSet(hooks, "online");
	Log("online: %s", ok ? "a missing online subsystem gets the offline stand-in" : "stand-in NOT installed");
	return ok;
}

uint32_t OnlineStandInUses()
{
	return s_uses;
}

} // namespace sfve::mod
