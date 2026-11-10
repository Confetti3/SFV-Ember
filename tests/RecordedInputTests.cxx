// What a battle's ticks record for each player's pad (SfvAdapter, mod/
// GameInput.hxx): the PC's own pads outside any session, the session's
// inputs while it advances the battle, and nothing pressed while a room's or
// a replay's battle loads. Pad 0 is the local player's on every copy,
// whichever side they play, so a button held while the battle loaded went
// into P1's ring on that copy only, and two copies started their session
// with different P1 input records and command parts (2026-11-10).

#include <doctest/doctest.h>

#include "mod/SfvAdapter.hxx"
#include "mod/SfvBattleLayouts.hxx"

#include <stdint.h>
#include <string.h>

using sfve::FrameInputs;
using sfve::PackedInput;
using sfve::mod::RecordedInputBits;
using sfve::mod::SfvAdapter;
namespace input = sfve::mod::input;
namespace iu = sfve::mod::sfv::inputunit;

namespace {

constexpr size_t kInputUnitSize = iu::kCounters + iu::kCountersSize;

// One copy's battle as the input unit's tick runs it: the input source sets
// this tick's pad values, the record the mod hooks (rva 0x43B310) sees them,
// then writes each pad's value into its ring at the record frame.
struct Battle {
	SfvAdapter adapter;
	uint8_t frame[0x1D0] = {};
	alignas(8) uint8_t unit[kInputUnitSize] = {};
	uint32_t pads[sfve::kPlayers] = {};
	int recordFrame = 0;

	uint32_t* PadValues() { return reinterpret_cast<uint32_t*>(unit + iu::kPadValues); }
	const uint8_t* Ring(int pad) const { return unit + iu::kRings + iu::kRingStride * (uint32_t)pad; }
};

Battle* s_battle;

void Tick(uint8_t*)
{
	Battle& b = *s_battle;
	for (int p = 0; p < sfve::kPlayers; p++) b.PadValues()[p] = b.pads[p];
	b.adapter.OnInputRecord(b.unit);
	for (int p = 0; p < sfve::kPlayers; p++) {
		auto* slot = reinterpret_cast<uint32_t*>(b.unit + iu::kRings + iu::kRingStride * (uint32_t)p +
			iu::kSlotSize * ((uint32_t)b.recordFrame % iu::kRingSlots));
		slot[0] = 7; // the frame tag
		slot[1] = (uint32_t)b.recordFrame;
		slot[iu::kSlotValue / 4] = b.PadValues()[p];
		slot[3] = 1;
	}
	b.recordFrame++;
}

void Start(Battle& b)
{
	s_battle = &b;
	b.adapter.SeeBattleFrameConstructed(b.frame);
	b.adapter.SeeBattleFrame(b.frame, Tick);
}

// The battle's ticks before it binds: RunOriginal, as BattleDriver runs them.
void Load(Battle& b, bool launched, int ticks)
{
	s_battle = &b;
	for (int i = 0; i < ticks; i++) b.adapter.RunOriginal(launched);
}

} // namespace

TEST_CASE("a recorded input is the pad's, the session's, or nothing pressed")
{
	CHECK(input::RecordedInput(input::PadSource::Pads, 0x210, 0x8) == 0x210);
	CHECK(input::RecordedInput(input::PadSource::Session, 0x210, 0x8) == 0x8);
	CHECK(input::RecordedInput(input::PadSource::Neutral, 0x210, 0x8) == 0);
}

TEST_CASE("a launched battle records no pad pressed while it loads, and the pad is still read")
{
	Battle b;
	Start(b);
	b.pads[0] = input::kHP | input::kLP;
	Load(b, true, 3);
	CHECK(RecordedInputBits(b.unit, 0) == 0);
	CHECK(RecordedInputBits(b.unit, 1) == 0);
	CHECK(b.adapter.RecordedInputs(0) == 0);
	CHECK(b.adapter.Sampled(0) == (input::kHP | input::kLP));
}

TEST_CASE("a battle of the player's own records the pads")
{
	Battle b;
	Start(b);
	b.pads[0] = input::kHP;
	b.pads[1] = input::kDown;
	Load(b, false, 2);
	CHECK(RecordedInputBits(b.unit, 0) == input::kHP);
	CHECK(RecordedInputBits(b.unit, 1) == input::kDown);
}

TEST_CASE("a session's frame records the session's inputs, and the pads play again after it")
{
	Battle b;
	Start(b);
	b.pads[0] = input::kMK;
	Load(b, true, 1);
	FrameInputs inputs;
	inputs.p[0] = input::kLP;
	inputs.p[1] = input::kHK;
	b.adapter.Advance(inputs, false);
	CHECK(RecordedInputBits(b.unit, 0) == input::kLP);
	CHECK(RecordedInputBits(b.unit, 1) == input::kHK);
	CHECK(b.adapter.Sampled(0) == input::kMK);
	b.adapter.RunOriginal();
	CHECK(RecordedInputBits(b.unit, 0) == (input::kLP | input::kMK));
}

TEST_CASE("two copies whose own players hold buttons while their battle loads record the same inputs")
{
	// Each copy's pad 0 is its own player's: P1 on one, P2 on the other.
	Battle p1Copy, p2Copy;
	Start(p1Copy);
	Start(p2Copy);
	p1Copy.pads[0] = input::kHP | input::kHK;
	p2Copy.pads[0] = input::kDown | input::kLeft;
	Load(p1Copy, true, 2);
	Load(p2Copy, true, 2);
	CHECK(memcmp(p1Copy.unit + iu::kRings, p2Copy.unit + iu::kRings, iu::kRingStride * sfve::kPlayers) == 0);

	// Before the fix: the ticks recorded each PC's own pad as P1's.
	Battle mine, theirs;
	Start(mine);
	Start(theirs);
	mine.pads[0] = input::kHP | input::kHK;
	theirs.pads[0] = input::kDown | input::kLeft;
	Load(mine, false, 2);
	Load(theirs, false, 2);
	CHECK(memcmp(mine.Ring(0), theirs.Ring(0), iu::kRingStride) != 0);
	CHECK(memcmp(mine.Ring(1), theirs.Ring(1), iu::kRingStride) == 0);
}
