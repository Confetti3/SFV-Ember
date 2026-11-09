// A hook set goes in whole or not at all (mod/GameHooks.hxx,
// HookTransaction): a set that fails part way leaves nothing of itself in the
// game and no original pointer on a freed trampoline, so the mod never runs a
// battle with half its hooks, and a set it could not take back out says so.
// MinHook is replaced by a stand-in that records which targets are hooked and
// fails the step a test names; the targets are addresses that are never
// called. The process's answer to a set left half in (CommitHookSet, which
// ends it) is not run here.

#include <doctest/doctest.h>

#include "mod/GameHooks.hxx"

#include <set>
#include <string>

using sfve::mod::CodeSite;
using sfve::mod::HookCalls;
using sfve::mod::HookCommit;
using sfve::mod::HookTransaction;
using Outcome = sfve::mod::HookCommit::Outcome;

namespace {

struct FakeMinHook {
	std::set<void*> created;
	std::set<void*> enabled;
	void* failCreate = nullptr;
	void* failEnable = nullptr;
	void* failRemove = nullptr;
};

FakeMinHook s_fake;

// The trampoline the stand-in hands back for a target: an address no target
// or other trampoline has.
void* Trampoline(void* target)
{
	return static_cast<uint8_t*>(target) + 0x10;
}

MH_STATUS WINAPI FakeCreate(LPVOID target, LPVOID, LPVOID* original)
{
	if (target == s_fake.failCreate) return MH_ERROR_NOT_EXECUTABLE;
	if (!s_fake.created.insert(target).second) return MH_ERROR_ALREADY_CREATED;
	*original = Trampoline(target);
	return MH_OK;
}

MH_STATUS WINAPI FakeEnable(LPVOID target)
{
	if (!s_fake.created.count(target)) return MH_ERROR_NOT_CREATED;
	if (target == s_fake.failEnable) return MH_ERROR_MEMORY_PROTECT;
	s_fake.enabled.insert(target);
	return MH_OK;
}

MH_STATUS WINAPI FakeRemove(LPVOID target)
{
	if (!s_fake.created.count(target)) return MH_ERROR_NOT_CREATED;
	if (target == s_fake.failRemove) return MH_ERROR_MEMORY_PROTECT;
	s_fake.created.erase(target);
	s_fake.enabled.erase(target);
	return MH_OK;
}

const HookCalls kFake{&FakeCreate, &FakeEnable, &FakeRemove};

constexpr uintptr_t kBase = 0x140000000;
const CodeSite kInputRecord{"InputRecord", 0x1000, {}};
const CodeSite kFlowMessage{"FlowMessage", 0x2000, {}};
const CodeSite kGameAlloc{"GameAlloc", 0x3000, {}};

void* Target(const CodeSite& site)
{
	return reinterpret_cast<void*>(kBase + site.rva);
}

// Three hooks' original pointers; the allocator's starts on the raw function,
// as GameHooks sets it before hooking.
struct Originals {
	void* inputRecord = nullptr;
	void* flowMessage = nullptr;
	void* gameAlloc = Target(kGameAlloc);
};

void AddThree(HookTransaction& hooks, Originals& originals)
{
	hooks.Add(kBase, kInputRecord, nullptr, &originals.inputRecord);
	hooks.Add(kBase, kFlowMessage, nullptr, &originals.flowMessage);
	hooks.Add(kBase, kGameAlloc, nullptr, &originals.gameAlloc);
}

void CheckUntouched(const Originals& originals)
{
	CHECK(s_fake.created.empty());
	CHECK(s_fake.enabled.empty());
	CHECK(originals.inputRecord == nullptr);
	CHECK(originals.flowMessage == nullptr);
	CHECK(originals.gameAlloc == Target(kGameAlloc));
}

void CheckHook(const sfve::mod::HookFailure& hook, const char* site, MH_STATUS status)
{
	REQUIRE(hook.site != nullptr);
	CHECK(std::string(hook.site) == site);
	CHECK(hook.status == status);
}

} // namespace

TEST_CASE("a set whose hooks all go in leaves every one enabled")
{
	s_fake = {};
	Originals originals;
	HookTransaction hooks(kFake);
	AddThree(hooks, originals);
	CHECK(hooks.Commit().outcome == Outcome::Installed);
	CHECK(s_fake.enabled == std::set<void*>{Target(kInputRecord), Target(kFlowMessage), Target(kGameAlloc)});
	CHECK(originals.inputRecord == Trampoline(Target(kInputRecord)));
	CHECK(originals.gameAlloc == Trampoline(Target(kGameAlloc)));
}

TEST_CASE("a hook that cannot be created takes out the hooks created before it")
{
	s_fake = {};
	s_fake.failCreate = Target(kGameAlloc);
	Originals originals;
	HookTransaction hooks(kFake);
	AddThree(hooks, originals);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RolledBack);
	CheckHook(commit.failed, "GameAlloc", MH_ERROR_NOT_EXECUTABLE);
	CheckUntouched(originals);
}

TEST_CASE("a hook that cannot be enabled takes out the whole set, the enabled ones too")
{
	s_fake = {};
	s_fake.failEnable = Target(kFlowMessage);
	Originals originals;
	HookTransaction hooks(kFake);
	AddThree(hooks, originals);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RolledBack);
	CheckHook(commit.failed, "FlowMessage", MH_ERROR_MEMORY_PROTECT);
	CheckUntouched(originals);
}

TEST_CASE("functions hooked by address go in as one set, as code sites do")
{
	// The overlay's pair: Present without ResizeBuffers must not stay in.
	s_fake = {};
	s_fake.failEnable = Target(kFlowMessage);
	void* present = nullptr;
	void* resizeBuffers = nullptr;
	HookTransaction hooks(kFake);
	hooks.Add("Present", Target(kInputRecord), nullptr, &present);
	hooks.Add("ResizeBuffers", Target(kFlowMessage), nullptr, &resizeBuffers);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RolledBack);
	CheckHook(commit.failed, "ResizeBuffers", MH_ERROR_MEMORY_PROTECT);
	CHECK(s_fake.created.empty());
	CHECK(s_fake.enabled.empty());
	CHECK(present == nullptr);
	CHECK(resizeBuffers == nullptr);
}

TEST_CASE("a function with no address (an export its module lacks) fails the set without reaching MinHook")
{
	s_fake = {};
	void* createMutex = nullptr;
	void* openMutex = nullptr;
	HookTransaction hooks(kFake);
	hooks.Add("CreateMutexW", Target(kInputRecord), nullptr, &createMutex);
	hooks.Add("OpenMutexW", nullptr, nullptr, &openMutex);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RolledBack);
	CheckHook(commit.failed, "OpenMutexW", MH_ERROR_FUNCTION_NOT_FOUND);
	CHECK(s_fake.created.empty()); // the stand-in would have recorded a null target it was given
	CHECK(s_fake.enabled.empty());
	CHECK(createMutex == nullptr);
	CHECK(openMutex == nullptr);
}

TEST_CASE("a hook that was already there is left as it was")
{
	s_fake = {};
	// Another installer's hook on the allocator.
	s_fake.created.insert(Target(kGameAlloc));
	s_fake.enabled.insert(Target(kGameAlloc));
	Originals originals;
	HookTransaction hooks(kFake);
	AddThree(hooks, originals);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RolledBack);
	CheckHook(commit.failed, "GameAlloc", MH_ERROR_ALREADY_CREATED);
	CHECK(s_fake.created == std::set<void*>{Target(kGameAlloc)});
	CHECK(s_fake.enabled == std::set<void*>{Target(kGameAlloc)});
	CHECK(originals.inputRecord == nullptr);
	CHECK(originals.flowMessage == nullptr);
	CHECK(originals.gameAlloc == Target(kGameAlloc));
}

TEST_CASE("a hook MinHook cannot remove makes the rollback incomplete, and the rest still come out")
{
	s_fake = {};
	s_fake.failEnable = Target(kGameAlloc);
	s_fake.failRemove = Target(kInputRecord);
	Originals originals;
	HookTransaction hooks(kFake);
	AddThree(hooks, originals);
	const HookCommit commit = hooks.Commit();
	CHECK(commit.outcome == Outcome::RollbackIncomplete);
	CheckHook(commit.failed, "GameAlloc", MH_ERROR_MEMORY_PROTECT);
	CheckHook(commit.unremoved, "InputRecord", MH_ERROR_MEMORY_PROTECT);
	// Still live, on the trampoline its original pointer names.
	CHECK(s_fake.created == std::set<void*>{Target(kInputRecord)});
	CHECK(s_fake.enabled == std::set<void*>{Target(kInputRecord)});
	CHECK(originals.inputRecord == Trampoline(Target(kInputRecord)));
	CHECK(originals.flowMessage == nullptr);
	CHECK(originals.gameAlloc == Target(kGameAlloc));
}

TEST_CASE("a commit takes out only what it created, never an earlier commit's hooks")
{
	s_fake = {};
	Originals originals;
	HookTransaction hooks(kFake);
	hooks.Add(kBase, kInputRecord, nullptr, &originals.inputRecord);
	REQUIRE(hooks.Commit().outcome == Outcome::Installed);

	s_fake.failEnable = Target(kGameAlloc);
	hooks.Add(kBase, kFlowMessage, nullptr, &originals.flowMessage);
	hooks.Add(kBase, kGameAlloc, nullptr, &originals.gameAlloc);
	REQUIRE(hooks.Commit().outcome == Outcome::RolledBack);
	CHECK(s_fake.enabled == std::set<void*>{Target(kInputRecord)});
	CHECK(originals.inputRecord == Trampoline(Target(kInputRecord)));
	CHECK(originals.flowMessage == nullptr);
	CHECK(originals.gameAlloc == Target(kGameAlloc));
}
