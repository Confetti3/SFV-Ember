// The game's costume codes (EMBER_LIBRARY.md section 55): the buttons a
// player holds as the battle loads, which the input unit keeps for its
// fighters (Urien's LP+MK+HP). A room's match carries each player's choice in
// the shared pick's personal action slot, and both copies load the battle
// holding both players' codes, or nothing when either copy knows no codes
// (0.1.0-beta.4 and earlier), as section 53 has it. Checked without the game:
// the pick's codec, the launch both copies make of it, what Ready sends, the
// ticks a launched battle runs before it binds and the snapshot the bind
// presets (a replay's header: ReplayTests).

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "mod/GameInput.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/SfvBattleLayouts.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;
namespace input = sfve::mod::input;
namespace iu = sfve::mod::sfv::inputunit;

namespace {

Pick WithCode(Pick pick, int index)
{
	picks::SetLoadCode(pick, index);
	return pick;
}

constexpr std::uint32_t kUrien = input::kLP | input::kMK | input::kHP;

// One copy's battle as the input unit's tick runs it (RecordedInputTests'),
// with the unit reaching past its held-at-load snapshot.
constexpr size_t kUnitSize = iu::kHeldAtLoad + 4 * 4;

struct Battle {
	mod::SfvAdapter adapter;
	uint8_t frame[0x1D0] = {};
	alignas(8) uint8_t unit[kUnitSize] = {};
	uint32_t pads[kPlayers] = {};
	int recordFrame = 0;

	uint32_t* PadValues() { return reinterpret_cast<uint32_t*>(unit + iu::kPadValues); }
	uint32_t HeldAtLoad(int pad) const
	{
		uint32_t value = 0;
		std::memcpy(&value, unit + iu::kHeldAtLoad + 4 * pad, 4);
		return value;
	}
	uint32_t Taken() const
	{
		uint32_t value = 0;
		std::memcpy(&value, unit + iu::kHeldAtLoadTaken, 4);
		return value;
	}
};

Battle* s_battle;

void Tick(uint8_t*)
{
	Battle& b = *s_battle;
	for (int p = 0; p < kPlayers; p++) b.PadValues()[p] = b.pads[p];
	b.adapter.OnInputRecord(b.unit);
	for (int p = 0; p < kPlayers; p++) {
		auto* slot = reinterpret_cast<uint32_t*>(b.unit + iu::kRings + iu::kRingStride * (uint32_t)p +
			iu::kSlotSize * ((uint32_t)b.recordFrame % iu::kRingSlots));
		slot[0] = 7;
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

} // namespace

TEST_CASE("a pick carries a costume code in its personal action slot, with the marker that says the copy knows codes")
{
	// The codes' buttons are the game's input bits.
	CHECK(picks::kLoadCodes[0].buttons == 0);
	CHECK(picks::kLoadCodes[1].buttons == kUrien);
	CHECK(picks::kLoadCodes[1].buttons == 0x290);
	CHECK(picks::kLoadCodes[2].buttons == (input::kUp | input::kLP | input::kMP | input::kHP | input::kLK));
	Pick pick;
	// The slot's default, what 0.1.0-beta.4 and earlier send: no code known.
	CHECK_FALSE(picks::LoadCodeIn(pick));
	picks::SetLoadCode(pick, 0);
	CHECK(picks::LoadCodeIn(pick) == 0);
	CHECK(pick.personalAction == picks::kLoadCodeMarker);
	picks::SetLoadCode(pick, 1);
	CHECK(picks::LoadCodeIn(pick) == 1);
	CHECK(picks::LoadCodeButtons(1) == kUrien);
	// An index out of range carries none, with the marker.
	picks::SetLoadCode(pick, picks::kLoadCodeCount);
	CHECK(picks::LoadCodeIn(pick) == 0);
	picks::SetLoadCode(pick, -1);
	CHECK(picks::LoadCodeIn(pick) == 0);
	CHECK(picks::LoadCodeButtons(-1) == 0);
	CHECK(picks::LoadCodeButtons(picks::kLoadCodeCount) == 0);
	// A slot without the marker, or with an index this copy does not know, is none known.
	for (int slot : {1, 9, 0x7F, 0x80 | 0x7F, 255}) {
		pick.personalAction = slot;
		CHECK_FALSE(picks::LoadCodeIn(pick));
	}
	// It travels as the wire's byte, and back.
	sf4e::NetplayFacade::RuntimeCommand command;
	sf4e::selection::ToNative(WithCode(PickOf(1), 1), command.character);
	CHECK(picks::LoadCodeIn(sf4e::selection::FromNative(command.character)) == 1);
}

TEST_CASE("the costume code is no part of a pick's fitting or validity")
{
	FillTestCatalog();
	const Pick coded = WithCode(PickOf(1), 1);
	CHECK(ember::Catalog().validPick(coded, false));
	Pick fitted = coded;
	CHECK_FALSE(ember::Catalog().normalizePick(fitted, false, nullptr, nullptr));
	CHECK(picks::LoadCodeIn(fitted) == 1);
	CHECK(picks::SameFitting(coded, PickOf(1)));
}

TEST_CASE("a room's match launches both players' codes only when both copies know codes")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(0, 1, 2);
	match.picks[0] = WithCode(match.picks[0], 1);
	match.picks[1] = WithCode(match.picks[1], 0);
	std::optional<mod::MatchPicks> launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK(launched->players[0].loadCode == kUrien);
	CHECK(launched->players[1].loadCode == 0);
	// An older copy's pick knows none: both copies load with nothing held,
	// as that copy does, whatever this one's player chose.
	match.picks[1].personalAction = 0;
	launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK(launched->players[0].loadCode == mod::kNoLoadCode);
	CHECK(launched->players[1].loadCode == mod::kNoLoadCode);
	// Training's and the pipe's picks carry none.
	CHECK(mod::FighterPick{}.loadCode == mod::kNoLoadCode);
}

TEST_CASE("Ready carries the player's costume code, and the marker for none")
{
	FillTestCatalog();
	const PlayerPick before = CurrentPick();
	SetPick({PickOf(1), before.stage});
	sf4e::NetplayFacade::RuntimeCommand command;
	AddPickTo(command);
	CHECK(picks::LoadCodeIn(sf4e::selection::FromNative(command.character)) == 0);
	SetPick({WithCode(PickOf(1), 1), before.stage});
	// The stored pick keeps the choice; fitting it leaves it.
	CHECK(picks::LoadCodeIn(CurrentPick().pick) == 1);
	AddPickTo(command);
	CHECK(picks::LoadCodeIn(sf4e::selection::FromNative(command.character)) == 1);
	SetPick(before);
}

TEST_CASE("a launched battle with codes records each player's code while it loads, the same on every copy")
{
	CHECK(input::RecordedInput(input::PadSource::Neutral, 0x210, 0x8, kUrien) == kUrien);
	CHECK(input::RecordedInput(input::PadSource::Pads, 0x210, 0x8, kUrien) == 0x210);
	CHECK(input::RecordedInput(input::PadSource::Session, 0x210, 0x8, kUrien) == 0x8);
	const std::array<PackedInput, kPlayers> codes = {kUrien, 0};
	// Each copy's pad 0 is its own player's, holding whatever they hold.
	Battle p1Copy, p2Copy;
	Start(p1Copy);
	Start(p2Copy);
	p1Copy.pads[0] = input::kHK;
	p2Copy.pads[0] = input::kDown;
	for (Battle* b : {&p1Copy, &p2Copy}) {
		s_battle = b;
		for (int i = 0; i < 3; i++) b->adapter.RunOriginal(true, &codes);
	}
	CHECK(mod::RecordedInputBits(p1Copy.unit, 0) == kUrien);
	CHECK(mod::RecordedInputBits(p1Copy.unit, 1) == 0);
	CHECK(std::memcmp(p1Copy.unit + iu::kRings, p2Copy.unit + iu::kRings, iu::kRingStride * kPlayers) == 0);
	// The codes last only for those ticks: the player's own battle records the pads again.
	s_battle = &p1Copy;
	p1Copy.adapter.RunOriginal();
	CHECK(mod::RecordedInputBits(p1Copy.unit, 0) == (kUrien | input::kHK));
	// A battle of the player's own takes no codes even when given some.
	Battle own;
	Start(own);
	own.pads[0] = input::kMP;
	own.adapter.RunOriginal(false, &codes);
	CHECK(mod::RecordedInputBits(own.unit, 0) == input::kMP);
}

TEST_CASE("the bind presets the held-at-load snapshot with the codes and marks it taken")
{
	Battle b;
	Start(b);
	b.adapter.RunOriginal(true);
	// The game had taken its own snapshot (nothing held, section 53's ticks).
	std::memset(b.unit + iu::kHeldAtLoad, 0, 16);
	const uint32_t one = 1;
	std::memcpy(b.unit + iu::kHeldAtLoadTaken, &one, 4);
	b.adapter.PresetHeldAtLoad({kUrien, input::kUp | input::kLP});
	CHECK(b.HeldAtLoad(0) == kUrien);
	CHECK(b.HeldAtLoad(1) == (input::kUp | input::kLP));
	CHECK(b.Taken() == 1);
	// Not taken yet: taken now, so the game never takes it from a session's frame.
	Battle fresh;
	Start(fresh);
	fresh.adapter.RunOriginal(true);
	fresh.adapter.PresetHeldAtLoad({0, kUrien});
	CHECK(fresh.Taken() == 1);
	CHECK(fresh.HeldAtLoad(0) == 0);
	CHECK(fresh.HeldAtLoad(1) == kUrien);
	// Pads 2 and 3 are left as they were.
	CHECK(fresh.HeldAtLoad(2) == 0);
	CHECK(fresh.HeldAtLoad(3) == 0);
	// No input unit seen yet: nothing to write.
	mod::SfvAdapter none;
	none.PresetHeldAtLoad({kUrien, kUrien});
}
