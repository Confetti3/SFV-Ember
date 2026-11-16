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
