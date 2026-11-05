// SFV's game under Ember's runtime (SfvGame.hxx). Each method calls the
// sfv-ember module that already does its work. Where no SFV module does it
// yet, the method answers as a game without it would, and a comment names
// what is missing (EMBER_LIBRARY.md section 10 lists them).

#include "ember/SfvGame.hxx"

#include "core/RollbackSession.hxx"
#include "ember/SfvCatalog.hxx"
#include "mod/GameScene.hxx"
#include "mod/HelperLink.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/MatchResult.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/VersusLaunch.hxx"

#include "sf4e/sf4e__AbsentGameFeatures.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "sf4e/sf4e__RuntimeSession.hxx"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

using namespace ember::game;

BattleInPlay s_battle;

// The fight's way out for the runtime (Retire, Abort, Fail) is NetplayMode's
// synchronized exit to the main menu, which the runtime cannot reach until
// the mod's netplay mode runs under it.
void ExitNotWired(const char* what, const char* reason)
{
	mod::Log("ember: %s (%s) asked for; the exit to the main menu is not wired to the runtime yet", what, reason);
}

// The game's own judgement of the match (MatchResult), as the room counts it.
sf4e::room::MatchResult RoomResult(const mod::MatchResult& result)
{
	if (!result.decided) return sf4e::room::MatchResult::Cancel;
	return result.winner == 0 ? sf4e::room::MatchResult::P1Win
		: result.winner == 1  ? sf4e::room::MatchResult::P2Win
							  : sf4e::room::MatchResult::Draw;
}

class SfvGameState final : public GameState {
public:
	const std::string& BuildId() const override { return build_; }
	bool MainMenuInFront() const override { return mod::MainMenuInFront(); }
	std::string ForegroundScreen() const override { return mod::CurrentSceneName(); }
	// Empty until the game's language has a reader (a contract issue is queued).
	std::string Language() const override { return {}; }
	// No reader of the player's SFV content yet: nothing is reported locked.
	sf4e::selection::Availability FighterAvailability(int) const override { return {}; }
	// No reader of the game's own random generator yet.
	std::uint32_t Random() override { return 0; }
	// No SFV sound module yet.
	bool PlayChallengerCall(float) override { return false; }
	// No SFV module closes the game; SFV Ember installs no updates.
	void CloseGame() override {}

private:
	const std::string build_ = mod::kMatchBuild;
};

class SfvBattle final : public Battle {
public:
	Entry Enter() override
	{
		const auto& netplay = ember::RuntimeSession::netplay;
		if (!netplay) return Entry::Deferred;
		const auto& lobby = netplay->client._lobbyData;
		const auto& match = netplay->client._matchData;
		RoomMatch room;
		room.picks[0] = sf4e::selection::FromNative(match.chara[0]);
		room.picks[1] = sf4e::selection::FromNative(match.chara[1]);
		room.stage = match.stageID;
		room.rounds = lobby.roundCount;
		room.roundSeconds = lobby.roundTime.integral;
		const std::optional<mod::MatchPicks> picks = LaunchPicks(room);
		if (!picks) {
			mod::Log("ember: the room's match is not one SFV can launch (fighters %d/%d, stage %lld, %d rounds of %d s)",
				room.picks[0].fighter, room.picks[1].fighter, (long long)room.stage, room.rounds, room.roundSeconds);
			return Entry::Rejected;
		}
		// A room's game ends back at the main menu, where its next match launches.
		mod::SetMatchEndChoice(mod::MatchEndChoice::MainMenu);
		mod::LaunchOnMainMenu(
			*picks, [] { return ember::RuntimeSession::netplay != nullptr; },
			// The room is not told yet; NetplayMode's match start reports it in the next step.
			[](const std::string& why) { mod::Log("ember: the room's battle could not be launched: %s", why.c_str()); });
		return Entry::Entered;
	}
	// NetplayMode starts its rollback session on its helper link's endpoints, not on the room's sync yet.
	void OnSessionSynced() override {}
	// Nothing is armed for the next battle: a launch waiting for the main menu stops with the room link.
	void DropNextBattle() override {}

	bool Live() const override { return s_battle.session && s_battle.session->Live(); }
	void Retire(const char* label) override { ExitNotWired("retire", label); }
	void Abort(const char* reason, sf4e::NoticeSeverity, bool) override { ExitNotWired("abort", reason); }
	void Fail() override { ExitNotWired("failure", "the session cannot go on"); }

	// The match ended and waits for MatchEnd's decision on a confirmed frame.
	bool ResultHeld() const override { return mod::EndedVersusBattlePending(); }
	// MatchEnd keeps SFV's results menu closed in a linked match.
	bool ResultScreenShown() const override { return false; }
	sf4e::room::MatchResult GameResult() const override
	{
		const mod::SfvAdapter* game = s_battle.game;
		return game && game->BattleData() ? RoomResult(mod::ReadMatchResult(game->BattleData()))
										  : sf4e::room::MatchResult::Cancel;
	}
	// Both copies take MatchEnd's one choice: both rematch, or neither does.
	unsigned RematchChoices() const override
	{
		return mod::EndedVersusBattlePending() && mod::GetMatchEndChoice() == mod::MatchEndChoice::Rematch ? 3u : 0u;
	}
	// No SFV module counts the games played in place yet.
	unsigned GamesPlayedInPlace() const override { return 0; }

	LinkSample Sample() const override
	{
		// The connection warning, prediction stall, disconnect countdown and
		// applied delay stay unknown: RollbackSession does not report them yet.
		LinkSample sample;
		const RollbackSession* session = s_battle.session;
		if (!session || !session->Live()) return sample;
		RollbackSession::Rift rift{};
		if (session->ReadRift(&rift)) sample.pingMs = rift.pingMs;
		sample.localFighter = true; // a session is two players, this copy one of them
		return sample;
	}
	// RollbackSession does not report its disconnect countdown yet.
	int DisconnectCountdownMs() const override { return -1; }
	// RollbackSession counts rollbacks for the whole session, not the recent ones.
	unsigned RecentRollbackFrames() const override { return 0; }
	// No record of the session's pings yet.
	sf4e::GamePing::Summary PingSummary() const override { return {}; }
	// NetplayMode's FramePacer paces the session and resets with each battle.
	void PollTelemetry() override {}
	void ResetPacing() override {}

	// No spectators: a RollbackSession plays two players and nothing else.
	std::size_t SpectatorStreams() const override { return 0; }
	bool DrainingSpectators() const override { return false; }
	void SetDrainingSpectators(bool) override {}
	bool SpectatorBacklog(int&) const override { return false; }

	// The desync check holds nothing back (SfvVerification), and the session
	// keeps its own hashes until the next battle.
	int VerificationHeldAt() const override { return -1; }
	void ClearVerification() override {}
	bool FightRunning(int& simulatedFrame) const override
	{
		const RollbackSession* session = s_battle.session;
		const bool running = session && session->Live() && session->Running() && !session->Failed() && s_battle.game &&
			s_battle.game->Fighting();
		if (running) simulatedFrame = session->Frame();
		return running;
	}
};

// SFV's pads are XInput pads (VirtualPads hooks XInputGetState), and the
// battle plays the input the game made from this copy's pad 0 (NetplayMode).
// No SFV module lists the pads, assigns them to sides, or sets them up yet:
// those answer as a game with no pads would.
class SfvInput final : public Input {
public:
	std::vector<sf4e::input::Device> ReadDevices(bool) override { return {}; }
	void ScanKeyboard(std::vector<sf4e::input::Device>&) override {}
	sf4e::input::Device MenuDevice(const std::vector<sf4e::input::Device>&) override { return {}; }
	void LogInventory(const std::vector<sf4e::input::Device>&) override {}
	bool AssignToSide(const sf4e::input::Device&, int, bool) override { return false; }
	void ReleaseFromSide(int) override {}
	// The bound battle's input from this copy's pad, in GameInput's bits.
	bool ReadAssignedInput(const sf4e::input::Device&, int, unsigned& mapped, unsigned& raw) override
	{
		mapped = raw = s_battle.game ? s_battle.game->Sampled(0) : 0;
		return s_battle.game != nullptr;
	}
	bool ReadController(int, int, unsigned& held, unsigned* physical) override
	{
		held = 0;
		if (physical) *physical = 0;
		return false;
	}
	// The XInput layout (A confirms, B goes back), the default of MenuButtons.
	sf4e::input::MenuButtons MenuButtons(int, int) override { return {}; }
	std::vector<sf4e::gamepads::NativeController> NativeControllers() override { return {}; }
	// The overlay clears the game's pad state itself while it is open.
	bool MenuTakesPads() override { return false; }

	sf4e::Gamepads::LayerFrame ReadPadLayer() override { return {}; }
	std::optional<sf4e::gamepads::Identity> PadInSlot(int) override { return std::nullopt; }
	sf4e::gamepads::PromptBrand BrandForDevice(int, int) override { return {}; }
	void SetLayouts(std::vector<sf4e::gamepads::StoredLayout>) override {}
	void SetupPrompt(const sf4e::gamepads::Identity&) override {}
	void BeginSetup(const sf4e::gamepads::Identity&, unsigned) override {}
	void ControlSetup(sf4e::gamepads::SetupCommand, int) override {}
	std::optional<std::string> SaveSetup() override { return std::nullopt; }
};

// No SFV settings store yet: a save is taken and written nowhere, so the
// player's settings last only while the game runs.
class SfvPreferences final : public Preferences {
public:
	Device SavedGameplayDevice() override { return {0, 0, std::nullopt}; }
	std::optional<sf4e::gamepads::Identity> SavedDeviceIdentity() override { return std::nullopt; }
	bool SaveGameplayDevice(int, int, const std::optional<sf4e::gamepads::Identity>&) override { return true; }
	bool Save(const sf4e::netplay::PlayerPreferences&) override { return true; }
	std::uint64_t Queue(const sf4e::netplay::PlayerPreferences&) override { return ++revision_; }
	bool Saved(std::uint64_t revision) override { return revision <= revision_; }
	bool Pending() override { return false; }
	std::string Error() override { return {}; }

private:
	std::uint64_t revision_ = 0;
};

// No SFV module for Steam, the lobby music, the away sound, custom content,
// the log's dropped lines or a self-test yet.
class SfvServices final : public Services {
public:
	sf4e::SteamId LocalSteamId() override { return {}; }
	bool SteamOverlayAvailable() override { return false; }
	bool OpenSteamProfile(const sf4e::SteamId&) override { return false; }
	void UseLobbyMusic(bool) override {}
	bool LobbyMusicAvailable() override { return false; }
	void AnnounceAway(bool, int, const char*) override {}
	sf4e::selection::InstalledContentPtr InstalledContent() override
	{
		return std::make_shared<const sf4e::selection::InstalledContent>();
	}
	unsigned long long LogLinesDropped() override { return 0; }
	bool SelfTestRunning() override { return false; }
	void SelfTestTick(bool, const std::string&) override {}
	void SelfTestInput(int, unsigned&, unsigned&) override {}
};

SfvGameState s_state;
SfvBattle s_battleSide;
SfvInput s_input;
SfvPreferences s_preferences;
SfvServices s_services;
NoTraining s_training;
NoReplays s_replays;
// Every capability off: SFV offers no Training, replays, spectators, pad
// setup, Steam profiles or custom content yet.
const Adapter s_game = {Capabilities{}, s_state, s_battleSide, s_input, s_preferences, s_services, s_training, s_replays};

} // namespace

void SetBattleInPlay(const BattleInPlay& battle)
{
	s_battle = battle;
}

const BattleInPlay& CurrentBattle()
{
	return s_battle;
}

} // namespace sfve::adapter

namespace ember::game {
const Adapter& Game() { return sfve::adapter::s_game; }
} // namespace ember::game
