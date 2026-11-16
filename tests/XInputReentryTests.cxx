#include "proxy/XInputReentry.hxx"

#include <doctest/doctest.h>

#include <thread>

using sfve::proxy::ReentryScope;
using sfve::proxy::XInputRoute;

TEST_CASE("a call goes to xinput1_3, the call that comes back in to xinput1_4, the third to nothing")
{
	const ReentryScope first;
	CHECK(first.Route() == XInputRoute::Primary);
	{
		const ReentryScope second;
		CHECK(second.Route() == XInputRoute::Fallback);
		{
			const ReentryScope third;
			CHECK(third.Route() == XInputRoute::None);
			const ReentryScope fourth;
			CHECK(fourth.Route() == XInputRoute::None);
		}
		// Back at the second level, the next call in is the third again.
		const ReentryScope again;
		CHECK(again.Route() == XInputRoute::None);
	}
	const ReentryScope next;
	CHECK(next.Route() == XInputRoute::Fallback);
}

TEST_CASE("a call that has finished leaves the next one at the first level")
{
	{
		const ReentryScope scope;
		CHECK(scope.Route() == XInputRoute::Primary);
	}
	const ReentryScope scope;
	CHECK(scope.Route() == XInputRoute::Primary);
}

TEST_CASE("the depth is each thread's own: a poll on one thread is not a re-entry on another")
{
	const ReentryScope outer;
	XInputRoute other = XInputRoute::None;
	std::thread([&] {
		const ReentryScope scope;
		other = scope.Route();
	}).join();
	CHECK(other == XInputRoute::Primary);
}
