// SFV's game under Ember's runtime (SfvGame.hxx). Each method calls the
// sfv-ember module that already does its work. Where no SFV module does it
// yet, the method answers as a game without it would, and a comment names
// what is missing (EMBER_LIBRARY.md section 10 lists them).

#include "ember/SfvGame.hxx"

#include "core/RollbackSession.hxx"
#include "ember/RoomBattle.hxx"
#include "ember/SfvCatalog.hxx"
#include "ember/SfvReplays.hxx"
#include "ember/SfvShell.hxx"
#include "ember/SfvTraining.hxx"
#include "mod/GameScene.hxx"
#include "mod/Log.hxx"
#include "mod/MatchEnd.hxx"
#include "mod/MatchResult.hxx"
#include "mod/SfvAdapter.hxx"
#include "mod/VersusLaunch.hxx"
#include "mod/VirtualPads.hxx"

#include "common/InstalledContent.hxx"
#include "common/Localization.hxx"
#include "common/MatchTelemetry.hxx"
#include "common/PadKind.hxx"
#include "netplay/PlayerPreferencesJson.hxx"
#include "netplay/SettingsStore.hxx"
#include "netplay/SettingsWriter.hxx"
#include "sf4e/sf4e__AbsentGameFeatures.hxx"
#include "sf4e/sf4e__GameAdapter.hxx"
#include "sf4e/sf4e__RuntimeEvents.hxx"
#include "sf4e/sf4e__RuntimeSession.hxx"

#include <windows.h>
#include <bcrypt.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sfve::adapter {

namespace {

using namespace ember::game;

BattleInPlay s_battle;
bool s_synced = false;
LaunchArming s_launch;
// The bound battle's pings (SF4's MatchTelemetry), from its session's start.
sf4e::MatchTelemetry s_telemetry;
// P1's session after its battle closed, kept for its watchers while the
// runtime drains them (SetWatcherDrain), and the runtime's drain flag.
RollbackSession* s_drain = nullptr;
bool s_draining = false;

// The bound battle's session while it is live.
const RollbackSession* LiveSession()
{
	return s_battle.session && s_battle.session->Live() ? s_battle.session : nullptr;
}

// The session kept for the watchers while it is live.
RollbackSession* LiveDrain()
{
	return s_drain && s_drain->Live() ? s_drain : nullptr;
}

// The kept session ends: the runtime retired it, ended its drain, or the
// player's session ended with it (as SF4's RetireGgpoSession closes it). The
// mod frees it on its next tick (NetplayMode::PollBattle).
void EndDrain(const char* why)
{
	RollbackSession* drain = LiveDrain();
	if (!drain) return;
	mod::Log("ember: the session kept for the watchers closes (%s; %zu stream(s) left)", why, drain->WatcherStreams());
	drain->Close();
}

// GGPO's connection warning, but none once the game's result is taken, as
// SF4 hears no interruption then: the other copy may leave the WIN first.
bool LinkWarned(const RollbackSession& session)
{
	return session.ConnectionWarning() && !(s_battle.room && s_battle.room->ResultTaken());
}

// The fight's way out for the runtime (Retire, Abort, Fail): NetplayMode
// ends the session and leaves for the main menu. With no battle in play
// there is nothing to end.
void ExitBattle(const char* what, const char* reason)
{
	mod::Log("ember: %s (%s)%s", what, reason ? reason : "", s_battle.exit ? "" : " with no battle in play");
	if (s_battle.exit) s_battle.exit(what);
}

class SfvGameState final : public GameState {
public:
	const std::string& BuildId() const override { return MatchBuildId(); }
	bool MainMenuInFront() const override { return mod::MainMenuInFront(); }
	std::string ForegroundScreen() const override { return mod::CurrentSceneName(); }
	// Empty until the game's language has a reader (a contract issue is queued).
	std::string Language() const override { return {}; }
	// Whether this copy's player may play the fighter, from SFV's catalog
	// (its OwnedAvailability: installed here and, with Ember's ownership gate
	// on, a fighter the game lets them pick). SFV's catalog rules judge
	// costumes and colours from their own table (EMBER_LIBRARY.md
	// section 23).
	sf4e::selection::Availability FighterAvailability(int fighter) const override
	{
		return OwnedAvailability(fighter);
	}
	// The host's match seed and its Random rolls. No reader of the game's own
	// generator yet, so the system's serves.
	std::uint32_t Random() override
	{
		std::uint32_t value = 0;
		BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
		return value;
	}
	// No SFV sound module yet.
	bool PlayChallengerCall(float) override { return false; }
	// No SFV module closes the game; SFV Ember installs no updates.
	void CloseGame() override {}
};

class SfvBattle final : public Battle {
public:
	Entry Enter() override
	{
		const std::optional<RoomMatch> now = RoomMatchNow();
		if (!now) return Entry::Deferred;
		const RoomMatch& room = *now;
		std::optional<mod::MatchPicks> picks = LaunchPicks(room);
		if (!picks) {
			mod::Log("ember: the room's match is not one SFV can launch (fighters %d/%d, stage %lld, %d rounds of %d s)",
				room.picks[0].fighter, room.picks[1].fighter, (long long)room.stage, room.rounds, room.roundSeconds);
			return Entry::Rejected;
		}
		mod::Log("ember: entering the room's match: %s vs %s on %s, %d rounds of %d s%s", FighterCode(room.picks[0].fighter).c_str(),
			FighterCode(room.picks[1].fighter).c_str(), StageCode(room.stage).c_str(), room.rounds, room.roundSeconds,
			room.practice ? ", a Practice match" : "");
		const LaunchArming::Ticket ticket = s_launch.Arm();
		// The battle this launch loads is the room's (RoomBattle), and no other.
		picks->linked = true;
		// A launch that fails leaves the game at the main menu with no battle:
		// the player hears why, and the runtime hears this copy's battle is
		// not coming, so the room ends the game rather than wait for it.
		mod::LaunchOnMainMenu(
			*picks, [ticket] { return s_launch.Armed(ticket); },
			[](const std::string& why) {
				mod::Log("ember: the room's battle could not be launched: %s", why.c_str());
				sf4e::NetplayFacade::PushAlert(sf4e::loc::T("runtime.authorized_connection_unavailable"));
				sf4e::NetplayFacade::NotifyRuntimeBattleClosedWithoutSession();
			});
		return Entry::Entered;
	}
	void OnSessionSynced() override
	{
		mod::Log("ember: every member's battle has loaded");
		s_synced = true;
	}
	// A launch still waiting for the main menu does not go ahead.
	void DropNextBattle() override { s_launch.Drop(); }

	// The battle's session, or P1's kept for its watchers after the battle.
	bool Live() const override { return LiveSession() || LiveDrain(); }
	// With a battle in play, its way out; after it, the kept session closes.
	void Retire(const char* label) override
	{
		if (LiveDrain() && !LiveSession()) return EndDrain(label);
		ExitBattle("retire", label);
	}
	void Abort(const char* reason, sf4e::NoticeSeverity severity, bool) override
	{
		if (reason && *reason) sf4e::NetplayFacade::PushAlert(reason, severity);
		if (LiveDrain() && !LiveSession()) return EndDrain(reason);
		ExitBattle("abort", reason);
	}
	void Fail() override
	{
		if (LiveDrain() && !LiveSession()) return EndDrain("the session cannot go on");
		ExitBattle("failure", "the session cannot go on");
	}

	// SFV's results menu stays closed in a room's battle (MatchEnd); its WIN
	// is the mod's held frames (RoomBattle, EMBER_LIBRARY.md section 19): the
	// result held there for the post-match prompt, and the WIN itself.
	bool ResultHeld() const override { return s_battle.room && s_battle.room->ResultHeld(); }
	bool ResultScreenShown() const override { return s_battle.room && s_battle.room->ResultScreenShown(); }
	// The result taken on the match end's confirmed frame; Abort before that
	// and once a rematch went on from it.
	sf4e::room::MatchResult GameResult() const override { return RoomGameResult(); }
	// Each side's Rematch as the held frames' confirmed inputs carry it.
	unsigned RematchChoices() const override { return s_battle.room ? s_battle.room->RematchChoices() : 0; }
	// SFV's held frames carry a decline (RematchDecision's DeclineBit), which
	// ends the WIN on the frame it is read.
	unsigned RematchDeclines() const override { return s_battle.room ? s_battle.room->RematchDeclines() : 0; }
	unsigned GamesPlayedInPlace() const override { return s_battle.room ? s_battle.room->GamesInPlace() : 0; }

	// The live session's link, as SF4 Ember's adapter fills it from its own
	// (sf4e__Sf4Game.cxx): RollbackSession reports the same GGPO events and
	// stats with the same meanings, and the ping is the telemetry's, polled
	// every 250 ms and none while the link is warned. A watcher has no ping
	// (SF4's MatchTelemetry keeps none for a spectator); it shows the delay
	// the fighters were granted and how far it is behind P1's stream.
	LinkSample Sample() const override
	{
		LinkSample sample;
		const RollbackSession* session = LiveSession();
		if (!session) return sample;
		sample.connectionWarning = LinkWarned(*session);
		sample.predictionStalled = session->PredictionStalled();
		sample.disconnectCountdownMs = DisconnectCountdownMs();
		if (session->Watching()) {
			sample.spectator = true;
			sample.spectatorBacklog = session->WatchedBacklog();
			sample.appliedDelay = s_battle.room ? s_battle.room->WatchedDelay() : -1;
			sample.ownDelays = s_battle.room && s_battle.room->WatchedOwnDelays();
			return sample;
		}
		sample.pingMs = s_telemetry.Ping(GetTickCount64());
		sample.appliedDelay = session->AppliedDelay();
		sample.localFighter = true; // one of the session's two players
		return sample;
	}
	int DisconnectCountdownMs() const override
	{
		const RollbackSession* session = LiveSession();
		return session && LinkWarned(*session) ? session->DisconnectCountdownMs() : -1;
	}
	// The deepest rollback of the last second.
	unsigned RecentRollbackFrames() const override
	{
		const RollbackSession* session = LiveSession();
		return session ? session->RecentRollbackFrames() : 0;
	}
	// The pings are kept (s_telemetry) but not handed on: Auto delay's record
	// of the games played stays off for SFV until it is reviewed for SFV's
	// session, which only reads its delay at the start.
	sf4e::GamePing::Summary PingSummary() const override { return {}; }
	void PollTelemetry() override
	{
		const RollbackSession* session = LiveSession();
		if (!session || session->Watching()) return;
		const auto now = GetTickCount64();
		if (!s_telemetry.PollDue(now)) return;
		RollbackSession::Rift rift{};
		s_telemetry.Sample(now, !LinkWarned(*session) && session->ReadRift(&rift) ? rift.pingMs : -1);
	}
	// NetplayMode's FramePacer paces the session and resets with each battle.
	void ResetPacing() override {}

	// P1's watcher streams, from the battle's session or the one kept for
	// them after it (the runtime's drain, which it sets and ends here; its
	// end closes the kept session, so none outlives the drain).
	std::size_t SpectatorStreams() const override
	{
		if (const RollbackSession* session = LiveSession()) return session->WatcherStreams();
		const RollbackSession* drain = LiveDrain();
		return drain ? drain->WatcherStreams() : 0;
	}
	bool DrainingSpectators() const override { return s_draining; }
	void SetDrainingSpectators(bool draining) override
	{
		s_draining = draining;
		if (!draining) EndDrain("the drain ended");
	}
	// A watcher's frames received from P1 and not played yet.
	bool SpectatorBacklog(int& frames) const override
	{
		const RollbackSession* session = LiveSession();
		frames = session ? session->WatchedBacklog() : -1;
		return frames >= 0;
	}

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

// The pad the battle plays: the game makes its input from this copy's pad 0,
// whichever side it fights on (NetplayMode).
constexpr int kGameplaySlot = 0;

// SFV's pads are XInput pads (VirtualPads hooks XInputGetState). The devices
// are the slots as the game last read them; the game's pad 0 is the gameplay
// device and fights for either side. No SFV module sets pads up or lays out
// their buttons yet: those answer as a game with no pad layer would.
class SfvInput final : public Input {
public:
	std::vector<sf4e::input::Device> ReadDevices(bool) override
	{
		std::vector<sf4e::input::Device> devices;
		for (int slot = 0; slot < mod::kPadSlots; slot++) {
			const mod::PadSeen seen = mod::LastPadState(slot);
			if (!seen.connected) continue;
			sf4e::input::Device device;
			device.type = sf4e::input::PadXInput;
			device.index = slot;
			device.name = "Controller " + std::to_string(slot + 1); // what Same() and the shell's names expect
			device.connected = true;
			device.buttons = seen.buttons;
			devices.push_back(device);
		}
		return devices;
	}
	// SFV Ember lists no keyboard device yet.
	void ScanKeyboard(std::vector<sf4e::input::Device>&) override {}
	sf4e::input::Device MenuDevice(const std::vector<sf4e::input::Device>& devices) override
	{
		for (const sf4e::input::Device& device : devices) {
			if (device.index == kGameplaySlot) return device;
		}
		return {};
	}
	void LogInventory(const std::vector<sf4e::input::Device>& devices) override
	{
		if (devices.size() == loggedDevices_) return;
		loggedDevices_ = devices.size();
		mod::Log("ember: %zu pad(s) connected", devices.size());
	}
	// The game's pad 0 plays either side; another pad cannot (the battle reads pad 0).
	bool AssignToSide(const sf4e::input::Device& device, int, bool) override
	{
		return device.connected && device.type == sf4e::input::PadXInput && device.index == kGameplaySlot;
	}
	void ReleaseFromSide(int) override {}
	// The bound battle's input from this copy's pad, in GameInput's bits.
	bool ReadAssignedInput(const sf4e::input::Device&, int, unsigned& mapped, unsigned& raw) override
	{
		mapped = raw = s_battle.game ? s_battle.game->Sampled(0) : 0;
		return s_battle.game != nullptr;
	}
	// A pad's menu buttons as the slot was last read, before the overlay
	// hides it from the game (VirtualPads' record): the D-pad, or the left
	// stick, as the directions (1 up, 2 down, 4 left, 8 right), and the
	// buttons in Ember's XInput layout. Ember's shell navigates by these.
	bool ReadController(int deviceType, int deviceIndex, unsigned& held, unsigned* physical) override
	{
		held = 0;
		if (physical) *physical = 0;
		if (deviceType != sf4e::input::PadXInput) return false;
		const mod::PadSeen seen = mod::LastPadState(deviceIndex);
		if (!seen.connected) return false;
		held = (seen.buttons & 0xFu) | seen.stick;
		if (physical) *physical = sf4e::input::xinput::Physical(seen.buttons);
		return true;
	}
	// The XInput layout (A confirms, B goes back), the default of MenuButtons.
	sf4e::input::MenuButtons MenuButtons(int, int) override { return {}; }
	std::vector<sf4e::gamepads::NativeController> NativeControllers() override { return {}; }
	// The shell holds the pads (SfvShell), and the overlay hides them from the game.
	bool MenuTakesPads() override { return ShellTakesPads(); }

	sf4e::Gamepads::LayerFrame ReadPadLayer() override { return {}; }
	std::optional<sf4e::gamepads::Identity> PadInSlot(int) override { return std::nullopt; }
	sf4e::gamepads::PromptBrand BrandForDevice(int, int) override { return {}; }
	void SetLayouts(std::vector<sf4e::gamepads::StoredLayout>) override {}
	void SetupPrompt(const sf4e::gamepads::Identity&) override {}
	void BeginSetup(const sf4e::gamepads::Identity&, unsigned) override {}
	void ControlSetup(sf4e::gamepads::SetupCommand, int) override {}
	std::optional<std::string> SaveSetup() override { return std::nullopt; }

private:
	std::size_t loggedDevices_ = ~std::size_t{0};
};

// The player's settings go to the library's settings store, in SFV Ember's
// folder beside the mod (SFVEmber\settings.json: ProductIdentity's
// settingsBesideModule), from which the runtime reads them back at start.
// Copies on one PC share the file; its lock serialises them and the last
// save wins. The writer's thread starts with the first save and stops at the
// game's exit (StopPreferences), never at DLL detach, where its lock could be
// held by a thread already gone. The gameplay device is always the game's
// pad 0.
class SfvPreferences final : public Preferences {
public:
	Device SavedGameplayDevice() override { return {sf4e::input::PadXInput, kGameplaySlot, std::nullopt}; }
	std::optional<sf4e::gamepads::Identity> SavedDeviceIdentity() override { return std::nullopt; }
	bool SaveGameplayDevice(int, int, const std::optional<sf4e::gamepads::Identity>&) override { return true; }
	bool Save(const sf4e::netplay::PlayerPreferences& preferences) override { return Queue(preferences) != 0; }
	std::uint64_t Queue(const sf4e::netplay::PlayerPreferences& preferences) override
	{
		if (stopped_ || !preferences.Valid()) return 0;
		if (!writer_) writer_ = new sf4e::netplay::SettingsWriter(sf4e::netplay::SettingsStore::DefaultDirectory());
		return writer_->QueueLauncher(sf4e::netplay::PlayerPreferencesJson(preferences));
	}
	bool Saved(std::uint64_t revision) override { return writer_ && revision && writer_->SavedLauncherRevision() >= revision; }
	bool Pending() override { return writer_ && writer_->GetStatus().pending; }
	std::string Error() override { return writer_ ? writer_->GetStatus().error : std::string(); }

	// Writes what is still queued, then ends the writer's thread. Kept, not
	// deleted: the process ends next.
	void Stop()
	{
		stopped_ = true;
		if (!writer_) return;
		writer_->Stop();
		const auto status = writer_->GetStatus();
		if (status.pending) mod::Log("ember: the settings could not be saved before exit: %s", status.error.c_str());
	}

private:
	sf4e::netplay::SettingsWriter* writer_ = nullptr; // game thread
	bool stopped_ = false;
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
// Watching a table's games (EMBER_LIBRARY.md section 25), the replays of
// the room battles SFV Ember recorded (section 26: Watch now, Keep and Show
// in folder; SFV has no replay list of its own to add one to, no video
// export, replay meter or inputs reader yet) and Training (section 27: SFV's
// own Training mode, SfvTraining); SFV offers no pad setup, Steam profiles
// or custom content yet.
Capabilities SfvCapabilities()
{
	Capabilities caps;
	caps.spectators = true;
	caps.replays = true;
	caps.training = true;
	return caps;
}
const Adapter s_game = {SfvCapabilities(), s_state, s_battleSide, s_input, s_preferences, s_services,
	SfvTrainingService(), SfvReplays()};

} // namespace

std::optional<RoomMatch> RoomMatchNow()
{
	const auto& netplay = ember::RuntimeSession::netplay;
	if (!netplay) return std::nullopt;
	const auto& lobby = netplay->client._lobbyData;
	const auto& match = netplay->client._matchData;
	RoomMatch room;
	room.picks[0] = sf4e::selection::FromNative(match.chara[0]);
	room.picks[1] = sf4e::selection::FromNative(match.chara[1]);
	room.stage = match.stageID;
	room.rounds = lobby.roundCount;
	room.roundSeconds = lobby.roundTime.integral;
	room.practice = lobby.trainingMode; // a Practice room's (TRAINING_OVER_ROLLBACK.md, step 2)
	return room;
}

void SetWatcherDrain(RollbackSession* session)
{
	s_drain = session;
}

bool PracticeBattleInPlay()
{
	return s_battle.room && s_battle.room->Practice() && LiveSession();
}

void SetBattleInPlay(const BattleInPlay& battle)
{
	s_battle = battle;
	s_synced = false;
	s_telemetry.Reset();
}

bool SessionSynced()
{
	return s_synced;
}

const BattleInPlay& CurrentBattle()
{
	return s_battle;
}

void StopPreferences()
{
	s_preferences.Stop();
}

} // namespace sfve::adapter

namespace ember::game {
const Adapter& Game() { return sfve::adapter::s_game; }
} // namespace ember::game
