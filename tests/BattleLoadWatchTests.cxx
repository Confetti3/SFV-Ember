// The battle load's log lines from the scene manager's ticks (mod/BattleLoadWatch.hxx).

#include <doctest/doctest.h>

#include "mod/BattleLoadWatch.hxx"

using sfve::mod::BattleLoadWatch;

TEST_CASE("a healthy load logs its start and nothing more")
{
	BattleLoadWatch watch;
	CHECK(watch.Observe(false, 0, 1000).empty());
	CHECK(watch.Observe(true, 0, 2000) == std::vector<std::string>{"Battle load: started"});
	CHECK(watch.Observe(true, 0, 5000).empty());
	CHECK(watch.Observe(true, 1, 8000).empty()); // bound: the first frame, with no stall to recover from
	CHECK(watch.Observe(true, 1, 90000).empty());
	CHECK(watch.Observe(false, 1, 95000).empty());
}

TEST_CASE("a load that never binds is reported at 20 s and at 60 s, once each")
{
	BattleLoadWatch watch;
	watch.Observe(true, 3, 0);
	CHECK(watch.Observe(true, 3, 19999).empty());
	CHECK(watch.Observe(true, 3, 20000) == std::vector<std::string>{"Battle load: no first frame after 20 s"});
	CHECK(watch.Observe(true, 3, 30000).empty());
	CHECK(watch.Observe(true, 3, 60000) == std::vector<std::string>{"Battle load: still no first frame after 60 s"});
	CHECK(watch.Observe(true, 3, 90000).empty());
}

TEST_CASE("a stalled load that binds says it recovered")
{
	BattleLoadWatch watch;
	watch.Observe(true, 3, 0);
	watch.Observe(true, 3, 21000);
	CHECK(watch.Observe(true, 4, 33000) == std::vector<std::string>{"Battle load: first frame after 33 s"});
	CHECK(watch.Observe(true, 4, 200000).empty());
}

TEST_CASE("leaving the battle ends a load that never bound, and the next load starts fresh")
{
	BattleLoadWatch watch;
	watch.Observe(true, 1, 0);
	watch.Observe(true, 1, 21000);
	CHECK(watch.Observe(false, 1, 22000).empty());
	CHECK(watch.Observe(false, 1, 100000).empty());
	CHECK(watch.Observe(true, 1, 110000) == std::vector<std::string>{"Battle load: started"});
	CHECK(watch.Observe(true, 1, 125000).empty());
	CHECK(watch.Observe(true, 1, 130000) == std::vector<std::string>{"Battle load: no first frame after 20 s"});
}
