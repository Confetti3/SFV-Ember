#include "mod/NetplayPhase.hxx"

#include <doctest/doctest.h>

#include <initializer_list>
#include <optional>

using sfve::mod::netplay::Event;
using sfve::mod::netplay::LeaveWanted;
using sfve::mod::netplay::Next;
using sfve::mod::netplay::Phase;

namespace {

constexpr Phase kPhases[] = {Phase::Unbound, Phase::Preparing, Phase::Agreeing, Phase::Playing, Phase::Leaving, Phase::Left};

// The phase after `events` from `from`, or none when one was refused.
std::optional<Phase> After(Phase from, std::initializer_list<Event> events)
{
	std::optional<Phase> at = from;
	for (const Event e : events) {
		if (!at) return std::nullopt;
		at = Next(*at, e);
	}
	return at;
}

} // namespace

TEST_CASE("a room battle: bound, prepared, agreed, played, left, and back at the menu")
{
	CHECK(After(Phase::Unbound, {Event::BattleBound}) == Phase::Preparing);
	CHECK(After(Phase::Unbound, {Event::BattleBound, Event::Prepared}) == Phase::Agreeing);
	CHECK(After(Phase::Unbound, {Event::BattleBound, Event::Prepared, Event::SessionStarted}) == Phase::Playing);
	// The scene manager refuses once, then takes it.
	CHECK(After(Phase::Playing, {Event::LeaveRefused}) == Phase::Leaving);
	CHECK(After(Phase::Playing, {Event::LeaveRefused, Event::LeaveTaken}) == Phase::Left);
	// Kept for no watcher: the battle's session stays, and the battle plays on (offline) until the next binds.
	CHECK(After(Phase::Left, {Event::BackAtMenu}) == Phase::Playing);
	// Kept for the watchers: no battle until the next binds.
	CHECK(After(Phase::Left, {Event::Drained, Event::BackAtMenu}) == Phase::Unbound);
	CHECK(After(Phase::Unbound, {Event::BattleBound}) == Phase::Preparing);
}

TEST_CASE("a battle with no session plays from its first frame")
{
	CHECK(After(Phase::Preparing, {Event::NoSession}) == Phase::Playing);
}

TEST_CASE("a start's events come in their order only")
{
	// Prepared once: a second one, or a start before it, is refused.
	CHECK_FALSE(Next(Phase::Agreeing, Event::Prepared));
	CHECK_FALSE(Next(Phase::Preparing, Event::SessionStarted));
	CHECK_FALSE(Next(Phase::Agreeing, Event::NoSession));
	CHECK_FALSE(Next(Phase::Playing, Event::Prepared));
	CHECK_FALSE(Next(Phase::Playing, Event::SessionStarted));
	CHECK_FALSE(Next(Phase::Unbound, Event::Prepared));
	CHECK_FALSE(Next(Phase::Leaving, Event::SessionStarted));
}

TEST_CASE("any bound battle can leave, a leave taken can be asked again, and no unbound one leaves")
{
	for (const Phase from : kPhases) {
		INFO("from " << sfve::mod::netplay::Name(from));
		if (from == Phase::Unbound) {
			CHECK_FALSE(Next(from, Event::LeaveRefused));
			CHECK_FALSE(Next(from, Event::LeaveTaken));
			continue;
		}
		CHECK(Next(from, Event::LeaveRefused) == Phase::Leaving);
		CHECK(Next(from, Event::LeaveTaken) == Phase::Left);
	}
	// The runtime's exit over a leave already taken asks the scene manager
	// again, which may refuse it this time.
	CHECK(Next(Phase::Left, Event::LeaveRefused) == Phase::Leaving);
}

TEST_CASE("back at the menu ends only a leave; a new battle and the drain apply from anywhere")
{
	for (const Phase from : kPhases) {
		INFO("from " << sfve::mod::netplay::Name(from));
		CHECK(Next(from, Event::BattleBound) == Phase::Preparing);
		CHECK(Next(from, Event::Drained) == Phase::Unbound);
		CHECK(Next(from, Event::BackAtMenu) == (LeaveWanted(from) ? Phase::Playing : from));
	}
}

TEST_CASE("only the two leave phases want the main menu")
{
	for (const Phase p : kPhases) CHECK(LeaveWanted(p) == (p == Phase::Leaving || p == Phase::Left));
	static_assert(Next(Phase::Preparing, Event::Prepared) == Phase::Agreeing);
}
