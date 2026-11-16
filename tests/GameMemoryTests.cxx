#include "mod/GameMemory.hxx"

#include <doctest/doctest.h>

namespace {

bool Answers()
{
	return true;
}

int AnswersSeven()
{
	return 7;
}

// A game function that reads a subsystem that is not there.
bool ReadsNullSubsystem()
{
	return **reinterpret_cast<volatile int* volatile*>(static_cast<uintptr_t>(0x10)) != 0;
}

// A guard page's fault (not a memory fault: MemoryFaultFilter) goes past
// TryRead to the caller's own handler, as it would to the crash record.
bool GuardPageFaultReachesCaller()
{
	void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD);
	if (!page) return false;
	int value = 0;
	bool reached = false;
	__try {
		sfve::mod::TryRead(page, &value);
	} __except (GetExceptionCode() == EXCEPTION_GUARD_PAGE ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		reached = true;
	}
	VirtualFree(page, 0, MEM_RELEASE);
	return reached;
}

} // namespace

TEST_CASE("a game call that works gives its answer, one that faults on an access gives none and the process lives")
{
	bool answer = false;
	CHECK(sfve::mod::TryCall(&Answers, &answer));
	CHECK(answer);
	int seven = 0;
	CHECK(sfve::mod::TryCall(&AnswersSeven, &seven));
	CHECK(seven == 7);

	bool faulted = true;
	CHECK_FALSE(sfve::mod::TryCall(&ReadsNullSubsystem, &faulted));
	CHECK(faulted); // untouched by a call that did not return
}

TEST_CASE("a guarded read or write takes a memory fault and lets every other exception through")
{
	using sfve::mod::MemoryFaultFilter;
	CHECK(MemoryFaultFilter(EXCEPTION_ACCESS_VIOLATION) == EXCEPTION_EXECUTE_HANDLER);
	CHECK(MemoryFaultFilter(EXCEPTION_IN_PAGE_ERROR) == EXCEPTION_EXECUTE_HANDLER);
	for (const unsigned long other : {(unsigned long)EXCEPTION_GUARD_PAGE, (unsigned long)EXCEPTION_BREAKPOINT,
			 (unsigned long)EXCEPTION_INT_DIVIDE_BY_ZERO, 0xE06D7363ul /* a C++ throw */}) {
		CHECK(MemoryFaultFilter(other) == EXCEPTION_CONTINUE_SEARCH);
	}
	int value = 0;
	CHECK_FALSE(sfve::mod::TryRead(reinterpret_cast<const int*>(static_cast<uintptr_t>(0x10)), &value));
	CHECK_FALSE(sfve::mod::TryWrite(reinterpret_cast<int*>(static_cast<uintptr_t>(0x10)), 1));
	const int here = 7;
	CHECK(sfve::mod::TryRead(&here, &value));
	CHECK(value == 7);
	CHECK(GuardPageFaultReachesCaller());
}

TEST_CASE("a game call with arguments is guarded the same way: its result when it works, none when it faults")
{
	int sum = 0;
	CHECK(sfve::mod::TryRun([&] { sum = 3 + 4; }));
	CHECK(sum == 7);
	int touched = 0;
	CHECK_FALSE(sfve::mod::TryRun([&] {
		touched = 1;
		ReadsNullSubsystem();
		touched = 2;
	}));
	CHECK(touched == 1); // stopped at the fault
}
