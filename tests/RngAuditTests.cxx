// The random draw audit (mod/RngAudit.hxx): where a generator lies against
// the rollback state, and that its draw is a verified site.

#include <doctest/doctest.h>

#include "mod/RngAudit.hxx"
#include "mod/SfvOffsets.hxx"

#include <iterator>

using sfve::StateLayout;
using sfve::mod::rng_audit::Place;
using sfve::mod::rng_audit::Ranges;

TEST_CASE("a generator is in the layout, the global one, or outside; only one whose vtable word is in its region is reseeded")
{
	// A fighter's pair as the game snapshots it ({vtable, state} then the
	// draw), a region that holds only another generator's state, and memory
	// the layout does not hold.
	alignas(8) uint8_t pair[24] = {};
	alignas(8) uint8_t object[16] = {};
	alignas(8) uint8_t elsewhere[16] = {};
	alignas(8) uint8_t global[16] = {};
	StateLayout layout;
	layout.Add("CharaUnit[0]+0xC40", pair, sizeof(pair));
	layout.Add("stateonly", object + sfve::mod::sfv::rng::kState, 4);
	Ranges ranges;
	ranges.Set(layout, reinterpret_cast<uintptr_t>(global));

	const Ranges::Found fighter = ranges.Of(reinterpret_cast<uintptr_t>(pair));
	CHECK(fighter.place == Place::InLayout);
	CHECK(fighter.reseeded);
	CHECK(ranges.Name(fighter.region) == "CharaUnit[0]+0xC40");

	const Ranges::Found stateOnly = ranges.Of(reinterpret_cast<uintptr_t>(object));
	CHECK(stateOnly.place == Place::InLayout);
	CHECK_FALSE(stateOnly.reseeded);
	CHECK(ranges.Name(stateOnly.region) == "stateonly");

	CHECK(ranges.Of(reinterpret_cast<uintptr_t>(global)).place == Place::Global);
	CHECK(ranges.Of(reinterpret_cast<uintptr_t>(elsewhere)).place == Place::Outside);
	// A generator whose state would run past a region's end is not in it.
	CHECK(ranges.Of(reinterpret_cast<uintptr_t>(pair) + 16).place == Place::Outside);

	ranges.Clear();
	CHECK(ranges.Empty());
	CHECK(ranges.Of(reinterpret_cast<uintptr_t>(pair)).place == Place::Outside);
}

TEST_CASE("the draw the audit hooks is in the build's verified sites")
{
	using namespace sfve::mod::sfv;
	bool found = false;
	for (const auto& site : kSites) found |= site.rva == kRandomDraw.rva;
	CHECK(found);
	// The LCG step's multiplier (1664525) as the site's bytes hold it.
	CHECK(kRandomDraw.bytes[0] == 0x69);
	CHECK(kRandomDraw.bytes[3] == 0x0D);
	CHECK(kRandomDraw.bytes[4] == 0x66);
	CHECK(kRandomDraw.bytes[5] == 0x19);
}
