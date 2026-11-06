// Which battle a battle frame update belongs to (mod/BattleIdentity), and the
// adapter that numbers the battles BattleDriver binds: a battle is its
// frame's construction, not its address. The game frees a battle's frame at
// its end and the next battle's allocation can return the same block; one
// copy of a two-copy run then never bound its second battle and the peer
// waited for it.

#include <doctest/doctest.h>

#include "mod/BattleIdentity.hxx"
#include "mod/SfvAdapter.hxx"

#include <stdint.h>

using sfve::mod::BattleIdentity;
using sfve::mod::SfvAdapter;

namespace {

void Update(uint8_t*) {}

} // namespace

TEST_CASE("two battles whose frames share an address are two battles")
{
	alignas(16) uint8_t frame[0x1D0] = {};
	BattleIdentity identity;
	identity.Constructed(frame);
	CHECK(identity.See(frame));
	CHECK(identity.Battle() == 1);
	for (int i = 0; i < 100; i++) CHECK_FALSE(identity.See(frame)); // rounds, training resets: the same frame
	// The battle ends (its frame freed) and the next one's frame is built in the same block.
	identity.Constructed(frame);
	CHECK(identity.See(frame));
	CHECK(identity.Battle() == 2);
	CHECK_FALSE(identity.See(frame));
	CHECK(identity.Battle() == 2);
}

TEST_CASE("a battle frame at another address is another battle, and the last one's late updates stay its own")
{
	alignas(16) uint8_t first[0x1D0] = {};
	alignas(16) uint8_t second[0x1D0] = {};
	BattleIdentity identity;
	identity.Constructed(first);
	CHECK(identity.See(first));
	// The next frame is built before the last update of the first one.
	identity.Constructed(second);
	CHECK_FALSE(identity.See(first));
	CHECK(identity.Battle() == 1);
	CHECK(identity.See(second));
	CHECK(identity.Battle() == 2);
	CHECK_FALSE(identity.See(second));
}

TEST_CASE("constructions between two updates make one new battle")
{
	alignas(16) uint8_t frame[0x1D0] = {};
	BattleIdentity identity;
	identity.Constructed(frame);
	identity.Constructed(frame);
	CHECK(identity.See(frame));
	CHECK_FALSE(identity.See(frame));
	CHECK(identity.Battle() == 1);
	CHECK(identity.Constructions() == 2);
}

TEST_CASE("with no construction seen, a battle is told by its frame's address")
{
	alignas(16) uint8_t first[0x1D0] = {};
	alignas(16) uint8_t second[0x1D0] = {};
	BattleIdentity identity;
	CHECK(identity.Battle() == 0);
	CHECK(identity.See(first));
	CHECK_FALSE(identity.See(first));
	CHECK(identity.See(second));
	CHECK(identity.Battle() == 2);
	// A construction where the hookless first battle's frame was still starts a battle.
	identity.Constructed(second);
	CHECK(identity.See(second));
	CHECK(identity.Battle() == 3);
}

// BattleDriver gives the battle mode a new battle (and binds it, so the room
// hears it loaded) whenever SfvAdapter::Battle changes; the adapter forgets
// the last battle's layout and units then. Bind itself needs the game.
TEST_CASE("the adapter binds two battles at one address as two battles")
{
	alignas(16) uint8_t frame[0x1D0] = {};
	alignas(16) uint8_t rollbackUnit[0x200] = {};
	alignas(16) static uint8_t inputUnit[0x2200] = {};
	SfvAdapter adapter;
	CHECK(adapter.Battle() == 0);
	adapter.SeeBattleFrameConstructed(frame);
	adapter.SeeBattleFrame(frame, Update);
	const uint64_t firstBattle = adapter.Battle();
	CHECK(firstBattle == 1);
	CHECK(adapter.BattleFrame() == frame);
	// The first battle's units, as its ticks report them (binding it needs the game).
	adapter.SeeRollbackUnit(rollbackUnit);
	adapter.OnInputRecord(inputUnit);
	adapter.SeeBattleFrame(frame, Update);
	CHECK(adapter.Battle() == firstBattle);

	adapter.SeeBattleFrameConstructed(frame);
	adapter.SeeBattleFrame(frame, Update);
	CHECK(adapter.Battle() == firstBattle + 1);
	CHECK(adapter.BattleFrame() == frame);
	CHECK_FALSE(adapter.Bound());
	// The last battle's units are forgotten: the new battle waits for its own
	// (with the old ones kept, Bind would go on to look them up in the game).
	CHECK_FALSE(adapter.Bind());
	CHECK(adapter.Layout().Regions().empty());
}
