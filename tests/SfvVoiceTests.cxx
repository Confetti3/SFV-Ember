// A fighter's voice language (the per-fighter record's +61, SFV_PORT.md's
// Launch), checked without the game: the pick's codec for it (the shared
// pick's win quote slot), what a room's match launches for each player
// (their own, the same on both copies), what a launch only this copy plays
// takes (this copy's options), the record's byte, the battle setting's
// read-back, and the store of this copy's own voices that Ready reads.

#include <doctest/doctest.h>

#include "CatalogFixture.hxx"

#include "ember/SfvCatalog.hxx"
#include "ember/SfvPick.hxx"
#include "ember/SfvPickSpace.hxx"
#include "mod/GameMemory.hxx"
#include "mod/SfvOffsets.hxx"
#include "mod/VersusLaunch.hxx"

#include "common/SelectionPick.hxx"
#include "sf4e/sf4e__NetplayFacade.hxx"

#include <cstdint>
#include <optional>
#include <string>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::catalog_fixture;
using sf4e::selection::Pick;

namespace {

namespace jump = mod::sfv::jump;

Pick WithVoice(Pick pick, int voice)
{
	picks::SetVoice(pick, voice);
	return pick;
}

} // namespace

TEST_CASE("a pick carries a voice in its win quote slot, and a pick that carries none reads as none")
{
	// The pick's numbers are the game's own (EKWVoiceLanguage).
	CHECK(picks::kVoiceJapanese == jump::kVoiceJapanese);
	CHECK(picks::kVoiceEnglish == jump::kVoiceEnglish);
	Pick pick;
	// The slot's default, what 0.1.0-beta.3 and earlier send: no voice.
	CHECK_FALSE(picks::VoiceIn(pick));
	CHECK(picks::VoiceOf(pick) == picks::kVoiceEnglish);
	picks::SetVoice(pick, picks::kVoiceJapanese);
	CHECK(picks::VoiceIn(pick) == picks::kVoiceJapanese);
	CHECK(picks::VoiceOf(pick) == picks::kVoiceJapanese);
	CHECK(pick.winQuote == 1);
	picks::SetVoice(pick, picks::kVoiceEnglish);
	CHECK(picks::VoiceIn(pick) == picks::kVoiceEnglish);
	CHECK(pick.winQuote == 2);
	// Neither of the game's two is none, and so is any other slot value.
	picks::SetVoice(pick, 2);
	CHECK_FALSE(picks::VoiceIn(pick));
	CHECK(pick.winQuote == 0);
	picks::SetVoice(pick, picks::kVoiceJapanese);
	picks::SetVoice(pick, -1);
	CHECK_FALSE(picks::VoiceIn(pick));
	for (int slot : {3, 10, 255, -1}) {
		pick.winQuote = slot;
		CHECK_FALSE(picks::VoiceIn(pick));
	}
	// It travels as the wire's byte, and back.
	sf4e::NetplayFacade::RuntimeCommand command;
	sf4e::selection::ToNative(WithVoice(PickOf(1), picks::kVoiceJapanese), command.character);
	CHECK(picks::VoiceIn(sf4e::selection::FromNative(command.character)) == picks::kVoiceJapanese);
}

TEST_CASE("the voice is no part of a pick's fitting or validity")
{
	FillTestCatalog();
	const Pick japanese = WithVoice(PickOf(1), picks::kVoiceJapanese);
	CHECK(ember::Catalog().validPick(japanese, false));
	Pick fitted = japanese;
	CHECK_FALSE(ember::Catalog().normalizePick(fitted, false, nullptr, nullptr));
	CHECK(picks::VoiceIn(fitted) == picks::kVoiceJapanese);
	CHECK(picks::SameFitting(japanese, PickOf(1)));
}

TEST_CASE("a room's match launches each player's own voice, and English for a copy that sent none")
{
	FillTestCatalog();
	RoomMatch match = MatchOf(0, 1, 2);
	picks::SetVoice(match.picks[0], picks::kVoiceJapanese);
	picks::SetVoice(match.picks[1], picks::kVoiceEnglish);
	std::optional<mod::MatchPicks> launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK(launched->players[0].voice == jump::kVoiceJapanese);
	CHECK(launched->players[1].voice == jump::kVoiceEnglish);
	// An older copy's pick carries none: both copies launch the game's
	// default rather than each its own options.
	match.picks[1].winQuote = 0;
	launched = LaunchPicks(match).picks;
	REQUIRE(launched);
	CHECK(launched->players[1].voice == jump::kVoiceEnglish);
	CHECK(launched->players[1].voice != mod::kVoiceOfOptions);
}

TEST_CASE("a pick that carries no voice launches with this copy's options, one that does with its own")
{
	FillTestCatalog();
	// Training's picks and its dummy are the stored ones, which carry none.
	CHECK(FighterPickOf(PickOf(0)).voice == mod::kVoiceOfOptions);
	CHECK(FighterPickOf(WithVoice(PickOf(0), picks::kVoiceJapanese)).voice == jump::kVoiceJapanese);
	CHECK(mod::FighterPick{}.voice == mod::kVoiceOfOptions);

	mod::FighterPick pick;
	// This copy's options, where the game gave one of its two.
	CHECK(mod::VoiceToWrite(pick, jump::kVoiceJapanese) == jump::kVoiceJapanese);
	CHECK(mod::VoiceToWrite(pick, jump::kVoiceEnglish) == jump::kVoiceEnglish);
	// No options to read, or a number the battle does not take: the game's
	// default, never the record's own byte.
	CHECK(mod::VoiceToWrite(pick, std::nullopt) == jump::kVoiceEnglish);
	CHECK(mod::VoiceToWrite(pick, 3) == jump::kVoiceEnglish);
	// The player's own voice is never replaced by this copy's options.
	pick.voice = jump::kVoiceJapanese;
	CHECK(mod::VoiceToWrite(pick, jump::kVoiceEnglish) == jump::kVoiceJapanese);
	pick.voice = jump::kVoiceEnglish;
	CHECK(mod::VoiceToWrite(pick, jump::kVoiceJapanese) == jump::kVoiceEnglish);
}

TEST_CASE("the launch writes the voice into the per-fighter record's +61 byte, and nothing next to it")
{
	CHECK(jump::kCharVoice == 61);
	CHECK(jump::kSettingVoice == 240);
	alignas(8) std::uint8_t chara[jump::kCharPkgSize];
	for (std::uint8_t& byte : chara) byte = 0xCD;
	mod::FighterPick pick;
	pick.voice = jump::kVoiceEnglish;
	mod::FillCharPkg(chara, pick);
	CHECK(chara[61] == jump::kVoiceEnglish);
	CHECK(chara[60] == 0xCD);
	CHECK(chara[62] == 0xCD);
	pick.voice = jump::kVoiceJapanese;
	mod::FillCharPkg(chara, pick);
	CHECK(chara[61] == jump::kVoiceJapanese);
	// Only one of the game's two is written: the launch gives FillCharPkg
	// VoiceToWrite's (WriteFighter), never kVoiceOfOptions itself.
	chara[61] = 0xCD;
	pick.voice = mod::kVoiceOfOptions;
	mod::FillCharPkg(chara, pick);
	CHECK(chara[61] == 0xCD);
	pick.voice = 2;
	mod::FillCharPkg(chara, pick);
	CHECK(chara[61] == 0xCD);
	CHECK(mod::VoiceName(jump::kVoiceJapanese) == "japanese");
	CHECK(mod::VoiceName(jump::kVoiceEnglish) == "english");
	CHECK(mod::VoiceName(-1) == "-1");
}

TEST_CASE("a loaded voice that is not the launched one is a different look, where both are known")
{
	const mod::PlayerLook launched{"RYU", 4, 1, jump::kVoiceEnglish};
	CHECK_FALSE(mod::LookMismatch(launched, {"RYU", 4, 1, jump::kVoiceEnglish}));
	CHECK(mod::LookMismatch(launched, {"RYU", 4, 1, jump::kVoiceJapanese}));
	// A side whose voice is not known compares by its look alone.
	CHECK_FALSE(mod::LookMismatch(launched, {"RYU", 4, 1}));
	CHECK_FALSE(mod::LookMismatch({"RYU", 4, 1}, {"RYU", 4, 1, jump::kVoiceJapanese}));
}

TEST_CASE("Ready carries this copy's player's voice for the pick's fighter, from the game's options")
{
	FillTestCatalog();
	// The fixture's fighters: RYU, CNL, KEN, CMY, BSN. CNL in Japanese, KEN
	// with no entry the read gave, CMY with a number the battle does not take.
	RefreshOwnVoices([](const std::string& code) -> std::optional<int> {
		if (code == "CNL") return picks::kVoiceJapanese;
		if (code == "KEN") return std::nullopt;
		if (code == "CMY") return 3;
		return picks::kVoiceEnglish;
	});
	CHECK(OwnVoice(0) == picks::kVoiceEnglish);
	CHECK(OwnVoice(1) == picks::kVoiceJapanese);
	CHECK(OwnVoice(2) == picks::kVoiceEnglish);
	CHECK(OwnVoice(3) == picks::kVoiceEnglish);
	CHECK(OwnVoice(-1) == picks::kVoiceEnglish);
	CHECK(OwnVoice(99) == picks::kVoiceEnglish);

	const PlayerPick before = CurrentPick();
	SetPick({PickOf(1), before.stage});
	sf4e::NetplayFacade::RuntimeCommand command;
	AddPickTo(command);
	const Pick sent = sf4e::selection::FromNative(command.character);
	CHECK(sent.fighter == 1);
	CHECK(picks::VoiceIn(sent) == picks::kVoiceJapanese);
	// The stored pick itself carries none: only what Ready sends does.
	CHECK_FALSE(picks::VoiceIn(CurrentPick().pick));

	// A change in the game's options reaches the next Ready.
	RefreshOwnVoices([](const std::string&) -> std::optional<int> { return picks::kVoiceEnglish; });
	AddPickTo(command);
	CHECK(picks::VoiceIn(sf4e::selection::FromNative(command.character)) == picks::kVoiceEnglish);
	SetPick(before);
}
