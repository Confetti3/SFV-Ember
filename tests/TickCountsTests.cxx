// Ticks per battle update, logged where the count changes (mod/TickCounts.hxx).

#include <doctest/doctest.h>

#include "mod/TickCounts.hxx"

using sfve::mod::TickCounts;

TEST_CASE("a line where the count changes, none while it holds")
{
	TickCounts counts;
	CHECK(counts.Note(1, 1, false) == "ticks: input frame 1: 1 tick(s) an update from here");
	CHECK(counts.Note(2, 1, false).empty());
	CHECK(counts.Note(3, 0, false) == "ticks: input frame 3: 0 tick(s) an update from here, 1 before");
	CHECK(counts.Note(4, 0, true).empty());
	CHECK(counts.Note(5, 2, true) == "ticks: input frame 5 (replayed): 2 tick(s) an update from here, 0 before");
	CHECK(counts.Summary() == "5 updates: 2 with no tick, 1 with more than one; 3 change(s) of the count");
	counts.Forget();
	CHECK(counts.Summary().empty());
}

TEST_CASE("at most kMostLines lines a battle; the summary counts every change")
{
	TickCounts counts;
	int lines = 0;
	for (int frame = 0; frame < 200; frame++) lines += counts.Note(frame, (uint32_t)(frame % 2), false).empty() ? 0 : 1;
	CHECK(lines == TickCounts::kMostLines);
	CHECK(counts.Summary().find("200 change(s) of the count (the first 64 logged)") != std::string::npos);
}
