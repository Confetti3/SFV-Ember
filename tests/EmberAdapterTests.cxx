// SFV's game adapter for Ember (SfvGame), checked without the game: what it
// offers and how it answers with no battle in play, P1's session kept for its
// watchers, and the arming of a room's launch. The catalog, the pick rules,
// the product, the game folder, the pictures and the desync check have tests
// of their own (SfvCatalogTests, SfvPickRulesTests, ProductIdentityTests,
// GameLocatorTests, SelectionArtTests, SfvVerificationTests), and the
// catalog's life another test program (CatalogLifecycleTests).

#include <doctest/doctest.h>

#include "LoopbackPair.hxx"
#include "ember/SfvGame.hxx"
#include "ember/SfvReplays.hxx"
#include "ember/SfvTraining.hxx"

#include "common/InputDelay.hxx"
#include "common/RoomRules.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <initializer_list>
#include <string>
#include <vector>

using namespace sfve;
using namespace sfve::adapter;
using namespace sfve::testbed;

TEST_CASE("a room's launch goes ahead only while its entry is armed")
{
	LaunchArming arming;
	CHECK_FALSE(arming.Armed(LaunchArming::Ticket::None));
	const LaunchArming::Ticket first = arming.Arm();
	CHECK(arming.Armed(first));
	arming.Drop(); // the player left the room
	CHECK_FALSE(arming.Armed(first));
	const LaunchArming::Ticket second = arming.Arm();
	CHECK((second != first));
	CHECK(arming.Armed(second));
	CHECK_FALSE(arming.Armed(first));
	const LaunchArming::Ticket third = arming.Arm(); // a later entry
	CHECK_FALSE(arming.Armed(second));
	CHECK(arming.Armed(third));
}

TEST_CASE("the game offers watching, replays and Training, and answers as no battle while none is in play")
{
	const ember::game::Adapter& game = ember::game::Game();
	CHECK(game.caps.training); // EMBER_LIBRARY.md section 27
	CHECK(game.caps.tinyFighters); // EMBER_LIBRARY.md section 59
	// No Skip winner scenes rule: a scene skip is only a player's press, so the
	// library leaves the room row out (docs/design/beta9/skip.md).
	CHECK_FALSE(game.caps.skipScenes);
	CHECK(game.caps.hitboxes);     // docs/design/beta8/hitboxes.md: Training select's Hitboxes row
	// Replays (EMBER_LIBRARY.md section 26): Watch now, Keep and Show in
	// folder; no game list of SFV's own, video, meter or inputs reader.
	CHECK(game.caps.replays);
	CHECK_FALSE(game.caps.replayGameList);
	CHECK_FALSE(game.caps.replayVideo);
	CHECK_FALSE(game.caps.replayMeter);
	CHECK_FALSE(game.caps.replayInputs);
	CHECK(game.caps.spectators); // EMBER_LIBRARY.md section 25
	CHECK(game.battle.SpectatorStreams() == 0u);
	CHECK_FALSE(game.battle.DrainingSpectators());
	int backlog = 0;
	CHECK_FALSE(game.battle.SpectatorBacklog(backlog));
	CHECK(backlog == -1);
	CHECK_FALSE(game.caps.padSetup);
	CHECK_FALSE(game.caps.steamProfile);
	CHECK_FALSE(game.caps.customContent);
	// None of the features whose Settings rows only SF4 Ember has (EMBER_LIBRARY.md section 35)
	// but the shortcut hint, which SFV's overlay draws (section 42).
	const ember::SettingsFeatures& features = game.caps.settingsFeatures;
	CHECK_FALSE(features.inputDisplayFixes);
	CHECK_FALSE(features.padLayer);
	CHECK_FALSE(features.keyboard);
	CHECK_FALSE(features.challengerCall);
	CHECK_FALSE(features.lobbyMusic);
	CHECK_FALSE(features.characterPictures);
	CHECK(features.shortcutHint);
	CHECK(game.state.Language().empty());
	CHECK_FALSE(game.state.MainMenuInFront()); // no scene manager seen
	CHECK(game.state.ForegroundScreen().empty());
	CHECK(game.battle.Enter().outcome == ember::game::Battle::Entry::Outcome::Deferred); // no room
	CHECK_FALSE(game.battle.Live());
	CHECK_FALSE(game.battle.ResultHeld());
	CHECK(game.battle.GameResult() == sf4e::room::MatchResult::Abort); // no match end taken
	game.battle.DropNextBattle(); // nothing armed
	CHECK(game.battle.RematchChoices() == 0u);
	int frame = -1;
	CHECK_FALSE(game.battle.FightRunning(frame));
	CHECK(frame == -1);
	CHECK(game.battle.Sample().pingMs == -1);
	// No battle's pings or rollbacks to hand on.
	CHECK(game.battle.PingSummary().samples == 0u);
	CHECK(game.battle.RollbackTotals().rollbacks == 0u);
	CHECK(game.battle.RollbackTotals().frames == 0u);
	unsigned mapped = 1, raw = 1;
	CHECK_FALSE(game.input.ReadAssignedInput({}, 0, mapped, raw));
	CHECK(mapped == 0u);
	CHECK(raw == 0u);
	// The replay folder is SFV Ember's own; nothing plays.
	CHECK(game.replays.Ready());
	CHECK(game.replays.ArchivePath() == sfve::adapter::ReplayFolder());
	CHECK_FALSE(game.replays.Playback().playback);
	CHECK(game.replays.PlaybackLanes() == nullptr);
	// No Training battle runs (no scene manager seen): nothing to follow, no
	// battle for a call, no Practice match.
	CHECK_FALSE(game.training.ControlsAvailable());
	CHECK(game.training.ReadPlace().generation == 0u);
	CHECK_FALSE(game.training.MatchPracticeActive());
	CHECK_FALSE(ReadTrainingView().available);
	CHECK_FALSE(TrainingPaused());
}

TEST_CASE("Auto delay recommends with SFV's own bands, SF4's left as they were")
{
	// EMBER_LIBRARY.md section 66: one frame through 100 ms, then one more per 33 ms.
	const sf4e::InputDelayBands bands = ember::game::Game().delayBands;
	CHECK(bands.firstBandMs == 100);
	CHECK(bands.bandStepMs == 33);
	struct Edge { int rttMs, delay; };
	for (const Edge edge : {Edge{0, 1}, Edge{100, 1}, Edge{101, 2}, Edge{133, 2}, Edge{134, 3}, Edge{166, 3}, Edge{167, 4},
		Edge{199, 4}, Edge{200, 5}, Edge{1000, 5}})
		CHECK(sf4e::InputDelayBand(bands, edge.rttMs) == edge.delay);
	// The field match (Brazil to NA, ~150 ms): SFV readies Balanced at 3 frames,
	// where SF4's bands, unchanged, still give 2; Smooth 4.
	const sf4e::RoundTrip field{150, 160};
	CHECK(sf4e::RecommendedInputDelay(bands, field) == 3);
	CHECK(sf4e::RecommendedInputDelay(sf4e::Sf4InputDelayBands, field) == 2);
	CHECK(sf4e::AutoInputDelay(sf4e::RecommendedInputDelay(bands, field), sf4e::AutoDelayStyle::Smooth) == 4);
	// A tail one band worse is tolerated; two bands worse raises it.
	CHECK(sf4e::RecommendedInputDelay(bands, sf4e::RoundTrip{120, 166}) == 2);
	CHECK(sf4e::RecommendedInputDelay(bands, sf4e::RoundTrip{120, 167}) == 3);
	// Balanced holds at its 3 frames past 166 ms; a chosen delay goes higher.
	CHECK(sf4e::AutoInputDelay(sf4e::RecommendedInputDelay(bands, sf4e::RoundTrip{190, 190})) == sf4e::AutoInputDelayMaximum);
}

TEST_CASE("the keyboard plays when no pad 0 is there, and pad 0 when it is")
{
	using sf4e::input::Device;
	ember::game::Input& input = ember::game::Game().input;
	const auto pad = [](int slot) {
		Device device;
		device.type = sf4e::input::PadXInput;
		device.index = slot;
		device.name = "Controller " + std::to_string(slot + 1);
		device.connected = true;
		return device;
	};
	// Whatever pads the other tests left read, the keyboard is always listed.
	const std::vector<Device> listed = input.ReadDevices(false);
	REQUIRE_FALSE(listed.empty());
	const Device& keyboard = listed.back();
	CHECK(keyboard.type == sf4e::input::PadKeyboard);
	CHECK(keyboard.index == 0);
	CHECK(keyboard.connected);
	// A keyboard-only player has a device, so a room can be opened and a seat readied.
	const Device alone = input.MenuDevice({keyboard});
	CHECK(alone.connected);
	CHECK(alone.type == sf4e::input::PadKeyboard);
	CHECK(input.AssignToSide(alone, 0, true));
	CHECK(input.AssignToSide(alone, 1, true));
	// Another pad than pad 0 does not take the keyboard's place: the battle reads pad 0.
	CHECK(input.MenuDevice({pad(1), keyboard}).type == sf4e::input::PadKeyboard);
	CHECK_FALSE(input.AssignToSide(pad(1), 0, true));
	// Pad 0 plugged in is the gameplay device, wherever the list puts it.
	const Device withPad = input.MenuDevice({keyboard, pad(0)});
	CHECK(withPad.type == sf4e::input::PadXInput);
	CHECK(withPad.index == 0);
	CHECK(input.AssignToSide(withPad, 0, true));
	// Nothing listed is no device, and a device gone is assigned to no side.
	CHECK_FALSE(input.MenuDevice({}).connected);
	Device gone = keyboard;
	gone.connected = false;
	CHECK_FALSE(input.AssignToSide(gone, 0, true));
	// The keyboard drives no menu as a pad: Ember's menus read its keys themselves.
	unsigned held = 1;
	CHECK_FALSE(input.ReadController(sf4e::input::PadKeyboard, 0, held, nullptr));
	CHECK(held == 0u);
}

TEST_CASE("P1's session kept for its watchers is the battle's until the runtime's drain ends")
{
	const ember::game::Adapter& game = ember::game::Game();
	MockFighter fighter;
	RollbackSession session(fighter);
	const uint16_t base = LoopbackPortBase();
	RollbackSession::Config config;
	config.localPlayer = 0;
	config.localPort = base;
	config.remotePort = (uint16_t)(base + 1);
	config.watchers.push_back({2, (uint16_t)(base + 2)});
	std::string error;
	REQUIRE_MESSAGE(session.Start(config, &error), error);
	session.KeepForWatchers();
	SetWatcherDrain(&session);
	// No battle in play, yet the session is live: the runtime holds the
	// room's receipt open and asks it how many watchers it still streams to.
	CHECK(game.battle.Live());
	CHECK(game.battle.SpectatorStreams() == 1u);
	game.battle.SetDrainingSpectators(true);
	CHECK(game.battle.DrainingSpectators());
	SUBCASE("the runtime retires it")
	{
		game.battle.Retire("deferred_close");
		CHECK_FALSE(session.Live());
	}
	SUBCASE("the drain ends another way (the next battle, a shutdown)")
	{
		game.battle.SetDrainingSpectators(false);
		CHECK_FALSE(session.Live());
	}
	CHECK_FALSE(game.battle.Live());
	CHECK(game.battle.SpectatorStreams() == 0u);
	game.battle.SetDrainingSpectators(false);
	SetWatcherDrain(nullptr);
}
