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
