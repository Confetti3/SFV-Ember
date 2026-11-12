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

#include "common/RoomRules.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"

#include <string>

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
	CHECK(game.caps.skipScenes); // docs/design/beta8/autoskip.md
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
