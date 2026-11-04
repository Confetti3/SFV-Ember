# Design: Ember as a shared, game-neutral library used by SF4 Ember and SFV Ember

> **Status: proposal (2026-11-04), not started.** The maintainer's direction: Ember on SFV
> reuses Ember's own code through a shared library of game-neutral Ember
> code that SF4 Ember and SFV Ember both build against; the game is an
> adapter and Ember's interface is what players see. The maintainer called it a
> long-term pivot that may still change. Nothing here has been done in the
> Ember repo. Decisions needed are in section 5; SFV branding is in 2.1.


Roots: **S** = `C:\Users\user\Desktop\sf4\sf4-current` (branch `release`); **V** = `C:\Users\user\Desktop\sf4\sfv-ember`, HEAD `53da8f0`. Paths are under `src/` unless they start with a root. Nothing was edited, built or run. This document builds on the four readers' reports and `overlay_plan.txt`. Where I checked something new, I say so.

**Correction to the platform-common reader.** V's `src/overlay/` is not untracked. It was committed at `53da8f0` ("Overlay host for Ember's interface on SFV"). `V/src/overlay/Overlay.hxx:25-37` already exposes `OverlayUi{applyScale, draw}` and `SetOverlayUi`, which is where Ember's shell plugs in on SFV. `git status` in V is clean.

---

## 1. The library boundary

### 1.0 New finding: the runtime is NEAR, not SF4

The game-side reader called `sf4e__NetplayRuntime*`, `NetplayFacade`, `NetplayPresentation` and `RuntimeBridge` (about 4,950 lines) Ember's runtime "reaching straight into SF4". I counted its references to the game, and the coupling is much smaller than that suggests:

- **`UserApp::netplay` (180 uses) and `UserApp::server` (34 uses)** are Ember's own `SessionClient` and `SessionServer`. They are only hung off the game struct as statics (`sf4e/sf4e__UserApp.hxx:15-41`). They are not game state.
- **Game calls through `UserApp`:** only `EnterAuthorizedMatch`, `MatchEntry` and `StartIrohSession` (`sf4e__UserApp.hxx:42-50`).
- **Rollback calls: 16 distinct `fSystem::*` symbols.** They sit in `NetplayFacade.cxx:44-265`, `NetplayPresentation.cxx:49-109` and `NetplayRuntime__Match.cxx:309-311`:
  - `ggpo` (used as "a match is active")
  - `simGate` (phase, fatal, connection warning, prediction stall)
  - `matchTelemetry` (ping, applied delay, own delays, spectator, backlog)
  - `drainingSpectators`, `SpectatorStreamCount`
  - `RetireGgpoSession`, `AbortGgpoMatch`, `AbortCause`
  - `snapshotMap`, `ClearHashCheckpoints`, `ggpoTimeline.Held`
  - `ResetPacing`, `RecentRollbackFrames`, `PollMatchTelemetry`, `LocalFighter`, `DisconnectCountdownMs`
- **Direct game calls: about 20 `Dimps::*` symbols:**
  - menu: `GetRootEvent`, `RootEventReady`, `FindForegroundEvent` (`NetplayRuntime.cxx:62-80`)
  - challenger sound: `PlaySystemCue` (`:663`)
  - close the game: `Main::Close` (`:808`)
  - pads: `Pad::MenuButtons` and `ReadController` (`__Input.cxx:185`, `__Status.cxx:653`), `PADTYPE_RAWINPUT` (`__Room.cxx:158`)
  - ownership: `Selection::ReadAvailability` (`__RandomFighter.cxx:45,82`, `__Room.cxx:347`, `__Status.cxx:642`)
  - the native pick type `ConfirmedCharaConditions` (`__RandomFighter.cxx:14`; `NetplayFacade.hxx:42,121,374-375`)
  - `rSystem` (`__Status.cxx:397`)
- **Includes that tie it to SF4** (`sf4e__NetplayRuntime.hxx`):
  - five Dimps headers (`:18-26`)
  - `../training/TrainingRuntime.hxx` (`:74`)
  - `../common/StageCatalog.hxx` (`:56`)
  - `../platform/LocaleWindows.hxx` (`:61`)
  - `ReplayStore` and `ReplaySender` (`:13`, `:64`)
  - `<spdlog/spdlog.h>` (`:72`); V has no spdlog
  - `../discord/Ticket.hxx` (`:73`)
- **`RuntimeBridge.hxx` is already pure.** It holds snapshot and command mailboxes and includes only `NetplayFacade.hxx` and `BoundedMailbox` (`:6-10`). This shows that a snapshot-and-command boundary between the runtime and its readers already works.

So the runtime becomes neutral once two things happen. First, `netplay` and `server` move into a runtime-owned holder, and SF4's `UserApp` keeps a reference. Second, those two short lists move behind the adapter in §2.

### 1.1 Moves into the library unchanged (NEUTRAL)

**`ui/`:**
- Everything in overlay plan §1b.
- `ApplicationShell__{Create,ShellScreens,Screens,Toasts,Links,Awards,FirstRun,Accessibility,Values,PadSetup,PostMatch}`, `ScreenPanel`, `RuntimeRequest`.
- Room and chat UI: `RoomPanelColumn`, `RoomPanelBoard.hxx`, `RoomFeedback`, `NetworkFeedback`, `LatencyMark`, `ChatPanel`, `ChatTranscript`, `ChatFilter`, `TrainingInRoom`.
- Ember services: `Identity*`, `SignInSites`, `IdentityRows`, `PublicRoomsPanel`, `PublicRoomsSetup`, `RankingsPanel`, `RankingsCards`, `MatchAnnouncements`.
- `UpdatePrompt`, `UpdateChannelPick`, `ProblemReportPanel`, `CountryPicker`, `VersionLine`, `WatchedPicture`, `OverlayLayers`, `ReplayPlaybackHud`, `MenuWalk`, `lab/UiLab`, `backends/`, `fonts/`.

**`session/`:** the 30 NEUTRAL files in the session-netplay reader's table: Admission through TournamentBinding, including `IrohRoom*`, `RoomModel{Actions,Arcade,ServerOwned,Terminal,Timers,Tournament}`, `SessionRecovery` and `SessionTransport`.

**`netplay/`:**
- SessionController, MatchResultOutbox, ProfileRecord, OwedAbort, ParkedIntent, LatencyBoard, MatchRoomFollower, MatchEndRules
- TournamentPlay/Status, WatchPlay, Identity*, MemberView, Rankings*, PublishCadence, BoundedMailbox, JsonFileStore, SettingsWriter, AutoDelayCheck
- InputAssignment, Controller{Inventory,Mappings,Restore}

**`common/`:**
- The platform-common reader's NEUTRAL list (AsyncHttpRequest through XInputMapping).
- The GGPO-only helpers, which V already copies: `sf4e__GgpoGate`, `GgpoAbortLatch`, `PacingController`, `GgpoDisconnectTolerance`, `GgpoTimeline`, `ConfirmedCheckpoint`, `SpectatorPolicy`, `SpectatorCatchUp`, `sf4e__StateHash`, `sf4e__NetUtil`.

**`platform/`:**
- `HelperFrame`, `DurableFile`, `Utf8`, `WineBuiltin`, `Speech.hxx`, `UpdatePolicy`, `ReportHistory`, `ReportUnicodeTables`, `UiPreferencesStore`
- `VideoEncoder`, `VideoLink{Protocol,Serve}`, `VideoTemporary`, `ReplayImportFreshness`, `ReplayRecordingChanges`

**`launcher/`:** `update/*` (ZipExtract, UpdateHandoff, UpdaterArguments, UpdaterRun, ProgressWindow, InstallOffer), `github_release_download`, `RunningGameFocus.hxx`, and the generic part of `GameLocator`.

**Other:**
- `training/`: the pure model only (`TrainingSession`, `FrameMeter`, `MoveInputs`, `Recording*`, `PracticeSettings`). Only S's adapter includes it at first.
- `rust/sf4-net` (the helper) and `src/discord/`, each after the identity parameters in §1.3.
- `cmake/`: `sf4e_embed`, `sf4e_verified_unpack`, `sf4e_build_identity`, `sf4e_tests`, `sf4e_chat_filter`, `sf4e_speech_relay`, `sf4e_helper`, `sf4e_discord`.

### 1.2 Stays in SF4 Ember (the SF4 adapter)

- **Game code:** all of `Dimps/`; the hook half of `sf4e/` (`Game__Battle*`, `GameEvents`, `UserApp` hooks, `Pad`, `Gamepads`, `InputLag`, `Platform`, `BackgroundPlay`, `CustomContent`, `Replay{Store,Playback,Capture}`, `SelfTest*`, `DeveloperOverlay*`, `Steam`, `MenuMusic`); `sf4e__Overlay.*` (it becomes the SF4 shell host); `sidecar/`.
- **Training:** `training/TrainingRuntime.cxx`, plus the UI files `TrainingPanel`, `TrainingMeter`, `TrainingData` and `TrainingShortcuts`.
- **Selection:**
  - `FighterSelector` and `SelectionArt.cxx` (the D3D9 and SF4-path half)
  - `common/{FighterCatalog,FighterMetadata.inc,StageCatalog,StageValue,InstalledContent,SelectionAssetPath,RandomFighter,LobbyMusic}`
- **Language, display and pads:** `GameLanguage`, `SteamKeyValues`, `GameDisplayConfig`, `GamepadSlots`, `DirectInputState`, `GamepadBrand`, `ControllerView`, `TrainingPad`, `TrainingStage`.
- **Replays and rollback state:** the `Replay*` format code, `ReplayExport`, `InPlaceReplays`, the memento code, `NativeMatchResult`, `NativeMenuLock`, `RollbackAudit`.
- **Platform:** `FrameGrab` and `GameDisplaySettings`, `CustomContentFiles`, `LocaleWindows.cxx`, `ReplayFiles` and `Replay*`.
- **Session wire format:** `session/SessionPlainTypes.hxx` and the SF4 half of `sf4e__SessionProtocol`, which become `Sf4MatchCodec` (§2).
- **Launcher and packaging:** the SF4 steps in `launcher/launcher.cxx` (`:95-261`, `:297-424`, `:703-717`, `:755-782`), `scripts/installer/ember-setup.iss`, `scripts/steamdeck/*`, `PackageInventory.inc`.
- **cmake:** `sf4e_copy_selection_art`, `sf4e_replays`, `sf4e_video`.

### 1.3 Needs a small neutral refactor first (SF4 output unchanged)

| Area | Refactor |
|---|---|
| Menu chain | Overlay plan §1a: `SelectionArt` takes a texture uploader; `SpeechPreferences` is split out; `DrawMainPortrait` moves; Speech gets a no-op build |
| Runtime holder | Move `UserApp::Netplay` and the `netplay`/`server` statics (`sf4e__UserApp.hxx:17-41`) into a neutral `RuntimeSession` owned by the runtime. SF4's `UserApp` keeps the names as references |
| Runtime → game | Route the `fSystem` and `Dimps` lists in §1.0 through `game::Battle`, `game::GameState`, `game::Catalog` and `game::Input` (§2). SF4's implementations are today's calls, moved |
| Runtime includes | Move `TrainingRuntime.hxx` behind `Battle::RequestTraining`. Move `ReplayStore`/`ReplaySender` behind `game::Replays` (optional). Keep `Discord/Ticket.hxx` in the library, where it belongs |
| Logging | Add `ember/log.hxx`, a thin macro over spdlog. Either V adds spdlog and fmt to its vcpkg manifest (simplest), or the shim routes to V's `Log.cxx` |
| Pick | `selection::Pick` and `ConfirmedCharaConditions` in `NetplayFacade.hxx:121,186,374-375` become `ember::Pick` plus the catalog (decision §5.1). `fighterAvailability[FighterCount]` becomes a vector |
| Session wire | `GgpoInputBytes = 8` (`MatchAuthority.hxx:11-16`, `.cxx:201`; `IrohMatchSession.cxx:160`) becomes `GameInfo::inputBytes`. `MAX_SF4E_PROTOCOL_USERS` becomes a per-game value. `FixedPoint roundTime` (`sf4e__SessionServer.hxx:98-99`, `SessionClient.hxx:143-151`) becomes `uint16_t`, converted in the SF4 codec |
| Pick checks | `selection::FighterCount`, `CostumeLimit` and `UltraChoices` (`RoomModel.cxx:109-112`, `RoomModelJson.cxx:398-400`, `SessionServer__Handlers.cxx:405-417,568`, `PlayerPreferences.hxx:98,255`) become `Catalog::ValidPick` and the catalog's limits |
| Verification | `sf4e__SessionClient__Verification.cxx` moves to the SF4 adapter: the `StateSnapshot`, `snapshotMap` and `ggpo` reads, and the `rSystem` `RS_ISLEAVING` write (`:109-163`, `:176-333`). The neutral client calls `Battle::HashAt` and `Battle::Leave` |
| Room rules | Rounds and times (`common/RoomRules.hxx:34-46`), `editionSelect` (`:50`) and `RoomQueries.hxx:133-134` read the catalog's rule lists. `editionSelect` becomes a game rule toggle |
| UI catalog sites | About 12 `FindFighter(...)->name` sites, `RosterDisplayOrder`, `UltraLabel`/`CostumeLabel`, `PublicRoomsCards.cxx:325` (hard-coded ids `{1,6,17,24}`), `NameMarks.cxx:43` and `ReplaysPanel.cxx:64,132,188,223` all go through `game::Catalog`/`Art` |
| Capabilities | Split HUD (`MatchHud.cxx:318-323,421-447`), display advice (`ApplicationShell.cxx:39-45,502,591-597`), lobby music (`ApplicationShell__Settings.cxx:213`), Training, Replays and Spectators become flags. SF4 sets all of them to true |
| Identity | Pipe prefix, kernel-object prefixes, `%APPDATA%\sf4e`, `ember://`, update repos, Discord app id, exe names, the window title (`ApplicationShell.cxx:712`) and log names become `ProductIdentity`, with SF4's values equal to today's literals |
| Payload | `sf4e::Payload` (`sf4e/sf4e.hxx:16-29`) splits into a neutral header (args, dump handles, helper/discord bootstraps, `helperError`) and an SF4 block (magic `"SF4B"`, `hSyncEvent`, `NetplayConfig`) |
| Packages | `PackageInventory` becomes mechanism plus per-product data. That breaks the chain `sf4e_package_installer` → `sf4e_selection` (`CMakeLists.txt:401-403`) |
| Server link | The `[selection]` group (`server/roomhost/sources.txt:52-54`) becomes a catalog chosen at link time. Later, a `game` key next to `build_id` (`RoomHost.cxx:64,159-160`) lets one host serve more than one game |

---

## 2. The game-adapter interface

This is one header that reconciles the readers' three sketches. Each comment names the SF4 code that implements the method today.

```cpp
// ember/game/Adapter.hxx
namespace ember {

// ---- Product identity: one per product, set at startup; SF4's values equal today's literals.
struct ProductIdentity {
  const char*    displayName;    // "SF4 Ember Netplay": ApplicationServices.cxx:223, ApplicationShell.cxx:712, version.rc.in:9-11
  const wchar_t* appDataFolder;  // L"sf4e": launcher.cxx:63-68, netplay_persist.cxx:22, sidecar.cxx:30
  const wchar_t* objectPrefix;   // L"Local\\SF4Ember": JoinLinkMailbox.hxx:27-32, GameWindowSection.hxx:20; L"Local\\Ember.Launcher" LauncherInstance.hxx:24
  const wchar_t* pipePrefix;     // L"\\\\.\\pipe\\sf4-net-": HelperProcess.cxx:45, BridgeServer.hxx:19
  const char*    urlScheme;      // "ember": launcher.cxx:536-556 (see §5.4)
  const char*    stableRepo; const char* nightlyRepo;   // UpdateChannel.hxx:11,30
  const char*    discordAppId;   // cmake/discord-sdk-pin.json
  const wchar_t* launcherExe; const wchar_t* updaterExe; const wchar_t* gameModule; const wchar_t* gameExe;
                                 // PackageInventory.inc:3-21, github_release_validation.cxx:176-186, github_release_client.cxx:390
};

namespace game {

// ---- Match-compatibility facts, also given to the room host.
struct GameInfo {
  std::string gameKey;           // "usf4" | "sfv"; in invites, public rooms, bridge
  std::string buildId;           // SF4: Sidecar hash -> sidecarHash (SessionServer.cxx:514); SFV: GameAdapter::BuildId (V core/GameAdapter.hxx:36)
  std::size_t inputBytes;        // SF4 8 (MatchAuthority.hxx:11-16); SFV 4 (V GameAdapter.hxx:23)
  uint8_t     maxProtocolUsers;  // MAX_SF4E_PROTOCOL_USERS (SessionProtocol.hxx:19)
};

// ---- Picks (decision (a) in §5.1): a fixed-size POD, so the room model and wire stay simple.
using FighterId = uint16_t; using DimId = uint8_t;
struct Pick { FighterId fighter = 0; std::array<uint8_t, 6> dim{}; uint8_t handicap = 0; bool random = false; };
// SF4 dims in FighterCatalog.hxx:42-51 order: costume, color, ultra, edition, personalAction, winQuote.
// SFV: costume, color, (V-Trigger if it is a pre-match choice: unchecked).

struct PickDimension { DimId id; const char* rowId; const char* labelKey; bool briefInSeat; };
struct Availability  { /* SF4 selection::Availability; SFV: all available */ };
struct RuleToggle    { const char* key; bool defaultOn; };          // SF4: "rules.edition_select" (MenuRows.hxx:141,184)

class Catalog {               // replaces selection::* for shell, room model, session server, room host
public:
  virtual std::span<const FighterId> RosterDisplayOrder() const = 0;          // Settings.cxx:147-160, selector grid
  virtual std::string FighterName(FighterId) const = 0;                        // ~12 FindFighter(...)->name sites
  virtual std::span<const PickDimension> Dimensions() const = 0;               // "ultra"/"appearance" rows become generic
  virtual std::vector<int> Allowed(const Pick&, DimId, const Availability&) const = 0; // AllowedUltras/AllowedColors, Overlay.cxx:451-477
  virtual std::string ValueLabel(const Pick&, DimId, bool brief) const = 0;   // UltraLabel/ShortUltraLabel/CostumeLabel; RoomPanelTables.cxx:57-76
  virtual bool Normalize(Pick&) const = 0;                                      // Overlay.cxx:356-357
  virtual bool ValidPick(const Pick&, const room::Rules&) const = 0;           // RoomModel.cxx:109-112, Handlers.cxx:405-411
  virtual Pick Roll(const Pick& remembered, const room::Rules&, AvailabilityFn) const = 0; // RandomFighterPick.hxx:47-50
  virtual std::span<const FighterId> Showcase() const = 0;                      // PublicRoomsCards.cxx:325
  // Stages
  virtual std::span<const StageInfo> Stages() const = 0; virtual bool ValidStage(int) const = 0;
  virtual int RandomStageId() const = 0; virtual int ResolveStage(int, uint32_t seed) const = 0; // StageCatalog.hxx:11-65, StageValue.hxx:6-11
  // Rules the game can play
  virtual std::span<const uint8_t> RoundCounts() const = 0;   // {1,3,5,7,15,99} RoomRules.hxx:34-46
  virtual std::span<const uint16_t> RoundTimes() const = 0;   // {30,60,99,300,9999}
  virtual uint16_t PracticeTime() const = 0;                   // RoomQueries.hxx:133-134
  virtual std::span<const RuleToggle> GameRules() const = 0;
};

// The wire codec: SF4's must write today's JSON byte for byte ("charaID", "costume", FixedPoint roundTime ...).
class MatchCodec {
public:
  virtual nlohmann::json EncodePick(const Pick&) const = 0;    // PreBattleSetChara, SessionProtocol.hxx:318-321
  virtual Pick DecodePick(const nlohmann::json&) const = 0;    // MatchData.chara, :99; Handlers.cxx:415-417
  virtual void EncodeLobby(const MatchRules&, nlohmann::json&) const = 0;  // LobbyData roundTime, :84; Rooms.cxx:314-316
};

// ---- Rendering: the library never sees a device.
class TextureUploader {                                         // overlay plan §1a.1
public:
  virtual ImTextureID Create(int w, int h, const void* rgba) = 0; virtual void Release(ImTextureID) = 0;
};
class Art {                                                     // SelectionArt split; SF4 = SelectionArt.cxx, SFV = packaged cutouts or none
public:
  virtual Image Portrait(FighterId, bool large) = 0; virtual Image Character(const Pick&) = 0;
  virtual Image DimensionArt(const Pick&, DimId) = 0; virtual Image Stage(int) = 0;
  virtual Image ClassEmblem(int band) = 0;                      // NameMarks.cxx:43; empty uses Ember's art
};
// The host (not this header) owns the backend: SF4 = D3D9 in sf4e__Overlay.cxx,
// SFV = DX11 in V/src/overlay/Overlay.cxx. Both call shell.Draw() inside an ImGui frame.

// ---- Where the game is (read each tick, filled into RuntimeSnapshot/ShellView).
enum class Scene { Boot, Title, MainMenu, Battle, MatchResult, OfflineTraining, ReplayExport, Other };
class GameState {
public:
  virtual bool Ready() const = 0;                     // Dimps::App::RootEventReady, NetplayRuntime.cxx:73
  virtual Scene CurrentScene() const = 0;             // FindForegroundEvent/GetName :62-80; Status.cxx:330-335. SFV: SceneStateKind
  virtual std::string GameLanguage() const = 0;       // GameLanguage/SteamKeyValues; SFV "" -> ResolveLocale fallback
  virtual std::optional<DisplayAdvice> DisplaySettingsAdvice() const { return {}; }  // GameDisplayConfig; ShellView::gameSettings
  virtual Availability FighterAvailability(FighterId) const = 0;  // Dimps::Selection::ReadAvailability; SFV: null reader
  virtual void PlayCue(Cue) {}                        // PlaySystemCue(HereComesChallenger), NetplayRuntime.cxx:663
  virtual void CloseGame() = 0;                       // Dimps::Platform::Main::Close, :808
  virtual ImTextureID WatchedPicture() { return {}; } // Overlay.cxx:371-376
  virtual void LockNativeMenu(bool locked) {}         // OnModeSelectedOverride, Overlay.cxx:161-176,898; SFV: no-op at first
};

// ---- The battle side: SF4's fSystem, SFV's RollbackSession. The game owns GGPO in both.
struct MatchSetup {                                   // = MatchData + projected LobbyData + IrohMatchSession ports
  uint64_t generation; int localSlot; bool spectator;
  uint32_t seed; int stage; Pick pick[2]; uint8_t inputDelay[2]; bool lowerLag;
  uint8_t roundCount; uint16_t roundTime; bool practice; std::vector<RuleValue> gameRules;
  uint16_t localPort; std::vector<uint16_t> remotePorts;      // IrohMatchSession::RemotePort(member), .hxx:20-28
};
enum class Entry { Deferred, Entered, Rejected };             // sf4e__UserApp.hxx:47-50
enum class EndHow { Retire, Leave, Failure };                  // NetplayRuntime__Match.cxx:309-311
struct LinkSample {                                            // what NetplayPresentation.cxx:49-109 reads
  bool active, running, fatal, connectionWarning, predictionStalled, localFighter;
  int  pingMs, appliedDelay, ownDelays[2], rollbackFrames, disconnectCountdownMs;
  bool spectator; int spectatorBacklog; int spectatorStreams;
};
class Battle {
public:
  virtual bool CanStart() const = 0;                           // at main menu; SFV VersusLaunch "main menu only" (VersusLaunch.hxx:66-71)
  virtual Entry Enter(const MatchSetup&) = 0;                  // UserApp::EnterAuthorizedMatch / StartMatchFromLobby (UserApp.cxx:88-145); SFV LaunchOnMainMenu
  virtual void StartTransport(std::unique_ptr<session::ClientTransport>, uint16_t port) = 0; // UserApp::StartIrohSession
  virtual bool OwnsSocket() const = 0;                         // -> IrohMatchSession::Tick(ggpoOwnsSocket)
  virtual LinkSample Sample() = 0;                             // simGate.*, matchTelemetry.*, PollMatchTelemetry, RecentRollbackFrames, DisconnectCountdownMs, LocalFighter, SpectatorStreamCount
                                                               // SFV: RollbackSession::Stats, ReadRift().pingMs, StepResult (V RollbackSession.hxx:59-116)
  virtual void End(EndHow, std::string_view why) = 0;          // RetireGgpoSession / AbortGgpoMatch(AbortCause)
  virtual void ResetAfterMatch() = 0;                          // ResetPacing, snapshotMap.clear, ClearHashCheckpoints (Facade.cxx:208,231-232)
  virtual void SetDrainingSpectators(bool) {}                  // drainingSpectators (Facade.cxx:44-265); SFV: unsupported
  virtual void OnFatal() = 0;                                  // simGate.OnFatal (Facade.cxx:187)
  // Desync check, called from the neutral SessionClient
  virtual int  ConfirmedFrame() const = 0;                     // ggpo_get_last_confirmed_frame (Verification.cxx:310); SFV LastConfirmedFrame
  virtual int  VerificationHeldAt() const = 0;                 // ggpoTimeline.Held (Facade.cxx:130)
  virtual bool HashAt(int frame, BattleHash&) const = 0;       // BattleHashV2 {overall, flow, chara0, chara1} (SessionProtocol.hxx:375-385); SFV HashAt/RegionHashesAt
  virtual void Leave(LeaveReason) = 0;                         // replaces rSystem RS_ISLEAVING (Verification.cxx:163)
  // Results: MatchResultCapture (MatchResultOutbox.hxx:10-27). SF4 calls it from NativeResult.cxx:76-117
  // once the frame is confirmed. SFV must add a round-judge reader.
  std::function<void(MatchResultCapture)> onResult;
  virtual bool RematchInPlaceOffered() const { return false; } // WIN-screen hold; RematchInput bits (common/RematchInput.hxx:18-35)
  virtual void Rematch() = 0;                                  // instant rematch; SFV MatchEnd::Rematch (MatchEnd.hxx:214-226)
  virtual void RequestTraining(const Pick& p1, const Pick& dummy, int stage) {}  // MainMenu::RequestTraining (NetplayRuntime__Room.cxx:105), TrainingMode::Choose (Overlay.cxx:483-495)
};

// ---- Input. Pads come in as a neutral snapshot; button layout is game data.
struct LayoutFunction { const char* key; uint32_t nativeMask; };
class Input {
public:
  virtual std::span<const LayoutFunction> Attacks() const = 0;  // AttackNames/AttackMasks (ButtonLayoutTypes.hxx:16-19)
  virtual std::span<const LayoutFunction> Macros() const = 0;   // throw/focus/red focus (ButtonLayoutScreen.hxx:7-13)
  virtual uint32_t FightMask() const = 0;
  virtual MenuButtons ReadMenu(PadRef) = 0;                     // Dimps::Pad::MenuButtons (Input.cxx:185); SFV VirtualPads + ui::ControllerButtons
  virtual PadSample ReadPad(PadRef) = 0;                        // Dimps::Pad::ReadController (Status.cxx:653)
  virtual uint64_t ReadMatchInput(int slot) = 0;                // ReadRuntimeMatchInput; SFV adapter.Sampled(0)
  virtual void BindRuntimeInput(const InputAssignment&) {}      // ControlSetup/SetLayouts; SFV minimal
};

// ---- HUD
struct HudAnchors { Rect playerLabel[2]; Rect timer; Rect koSign; float nameOffsetUnit; };
class Hud { public: virtual std::optional<HudAnchors> SplitLayout() const { return {}; } };   // MatchHud.cxx:117-121,318-323,421-447

// ---- Optional services (null object = feature hidden by capability)
class Replays { /* Modes(), Request(replay::Request), ReplayStore list: ReplayRequest.hxx:12-21, sf4e__ReplayStore.hxx:16 */ };
class TrainingView { /* ReadView/Submit: training::View, TrainingRuntime.hxx */ };

struct Capabilities { bool splitHud, displayAdvice, lobbyMusic, training, replays, spectators, nativeMenuLock, classEmblems; };

// ---- What the game hands the library at startup.
struct Adapter {
  ProductIdentity identity; GameInfo info; Capabilities caps;
  Catalog& catalog; MatchCodec& codec; Art* art; TextureUploader& uploader;
  GameState& state; Battle& battle; Input& input; Hud& hud;
  Replays* replays = nullptr; TrainingView* training = nullptr;
};
} // namespace game

// ---- Launcher side (separate library; launcher process, not game process)
namespace launch {
struct GameDescriptor {
  const wchar_t* exeName;                   // L"SSFIV.exe" (GameLocator.hxx:10)
  uint32_t steamAppId;                      // 45760 (launcher.cxx:184, :226-261)
  const wchar_t* defaultFolder;             // GameLocator.hxx:24-31
  bool writeSteamAppIdFile;
  std::span<const wchar_t* const> shadowedDlls;   // GameLocator.hxx:68, launcher.cxx:755-782
};
class Injector {                            // SF4: DetourCreateProcessWithDllsW + payload (launcher.cxx:297-424)
public:                                     // SFV: proxy-install check (V proxy/xinput_proxy.cxx:1-13) or x64 Detours
  virtual bool Start(const GameDescriptor&, const PayloadHeader&, std::span<const std::byte> gameBlock, ProcessInfo&) = 0;
};
struct PayloadHeader { /* args, 3 dump handles, helper/discord HelperBootstrap, helperError (sf4e.hxx:16-29 neutral part) */ };
}
} // namespace ember
```

The library owns:
- the runtime: today's `NetplayRuntime*`, `NetplayFacade`, `NetplayPresentation` and `RuntimeBridge`, renamed in place
- the session client and server
- the shell

It calls only `game::Adapter`. Starting a match and reading its result stay behind `Battle`. The shell itself never touches the game; it only sends `netplay::Command`/`room::Action` and reads the snapshot (`RuntimeRequest.hxx:30,46`; `ApplicationShell.hxx:196-202`).

### 2.1 Product branding (SFV Ember is not SF4 Ember)

The maintainer: SFV builds carry SFV branding. `ProductIdentity` grows the visual
identity the shell draws, so the shared shell never shows SF4's:

```cpp
struct Branding {
  const char* productName;     // "SFV Ember" | "SF4 Ember Netplay" (window title, header, About, notifications)
  const char* shortName;       // "SFV Ember" | "SF4 Ember"
  ImageRef    mark;            // the header mark (S: ember.rgba via sf4e_brand.cmake); SFV: an SFV Ember mark
  ImageRef    menuBackdrop;    // assets/brand/ember-menu-background.png; SFV: its own backdrop
  uint32_t    accent;          // the theme's accent (Ember orange 0xFF8738 today); a game may tint it
  const char* gameName;        // "Street Fighter V" | "Ultra Street Fighter IV", where the shell names the game
};
// ProductIdentity { ...; Branding branding; }
```

Theme keeps Ember's palette; only the accent, the mark, the backdrop and the
names come from the product. The SFV mark and backdrop are new art (the maintainer's
call on the design).

---

## 3. Extraction plan

All work happens on a branch off `release` (e.g. `feat/ember-library`), with one commit per step. After each step:
- configure `SF4E_CORE_TESTS_ONLY=ON` and run its tests
- run the full `ctest`
- run `server/roomhost/build-linux.sh` and `test-linux.sh` (in WSL)
- build with `scripts/build-current.ps1`

`package-team.ps1` runs only at the end. The in-game self-test (`scripts/run-selftest.ps1`) needs the maintainer's approval to launch, and runs at the steps marked ★.

**Existing tests that prove nothing changed:**
- `shell_*_journey_test` (14 files)
- `session_server_*_test` (10 files)
- `iroh_*_test` (`iroh_match_session_test`, `iroh_authorized_match_test`, `iroh_room_integration_test`, `iroh_recovery_integration_test`, `iroh_game_integration_test`)
- `fighter_catalog_test`, `selection_art_test`, `selection_value_test`, `native_selection_availability_test`
- `overlay_{lifecycle,pad_capture,presentation,reset}_test`, `package_installer_test`, `presentation_snapshot_test`, `runtime_room_fixture` users
- labels: `unit ui room ggpo replay training discord helper rust sdk publisher script network` (`cmake/sf4e_tests.cmake:42`)

**Gaps to close first:**
- `ui_render_test` renders through D3D9 and writes images, but compares only a few pixels (`ui_render_test.cxx:68-70,150-169`). There is no golden comparison.
- Nothing checks the wire bytes, so the "SF4 JSON byte for byte" requirement has no test behind it.

| # | Step | Proves it |
|---|---|---|
| 0a | **Wire goldens.** Record today's `SessionProtocol` messages (MatchData, PreBattleSetChara, LobbyData, StateSnapshot, BattleHashV2), `RoomModelJson` and `PlayerPreferences` JSON from fixtures into `tests/fixtures/wire/*.json`, with a test that re-encodes and compares bytes | New `room`/`unit` test, green on unchanged code |
| 0b | **Render goldens.** Store reference images for the main `ui_render_test` screens and compare with a small tolerance. Shell journey tests stay as they are | New check, green on unchanged code |
| 1 | Overlay plan §1a: uploader, SpeechPreferences, DrawMainPortrait, Speech | ui, `selection_art_test`, render goldens |
| 2 | **Identity.** Add `ProductIdentity` with SF4 defaults equal to today's literals; replace the literals listed in §1.3 | All tests. A new unit test checks that each SF4 identity string equals the old literal |
| 3 | **Runtime holder.** `UserApp::Netplay`, `netplay` and `server` move into `ember::RuntimeSession`; `UserApp` keeps references | All tests; `runtime_room_fixture` users; ★ self-test (Online, Room) |
| 4 | **Game adapter, SF4 implementation.** Add `game::Adapter`. Write `Sf4Battle`, `Sf4GameState`, `Sf4Input` and `Sf4Catalog`, whose bodies are the `fSystem`/`Dimps` calls from §1.0 moved verbatim. The runtime calls the adapter, and `NetplayRuntime.hxx` loses its Dimps includes | All tests; `presentation_snapshot_test`, spectator tests; ★ self-test (Match, Room, Tournament, Replay) |
| 5 | **Catalog through the UI and room model.** About 12 `FindFighter` sites, `PublicRoomsCards:325`, `RoomModel:109-112`, `RoomModelJson:398`, `PlayerPreferences:98,255`, `RoomRules` lists | `shell_*_journey`, `fighter_catalog_test`, render goldens, wire goldens |
| 6 | **Session wire neutral.** `GgpoInputBytes` and `MAX_SF4E_PROTOCOL_USERS` come from `GameInfo`. `FixedPoint` and `ConfirmedCharaConditions` live only in `Sf4MatchCodec`. Verification moves to `Sf4Battle`. `Session` stops linking `Dimps` (`CMakeLists.txt:321`), and `session_server` stops linking `sf4e_selection` (it gets a catalog at link time) | **Wire goldens byte-identical**; `session_server_*`; `iroh_*`; Linux room host build; ★ self-test (Online, desync) |
| 7 | **Capabilities.** Split HUD, display advice, lobby music, Training, Replays, Spectators; SF4 sets all true | `shell_*`, `ui_render_match_hud`, render goldens |
| 8 | **Payload and inventory split.** `PayloadHeader` plus an SF4 block with the same byte layout as today. `PackageInventory` becomes mechanism plus SF4 data | `package_installer_test`, `run-package-tests.ps1`, payload size/offset `static_assert`s |
| 9 | **In-tree `ember/` folder.** `git mv` the NEUTRAL set (§1.1 plus the refactored files) into `S/ember/{src,cmake,locales}` with its own `CMakeLists.txt`. Keep the `sf4e` namespace, the `sf4e__` file names and the relative-include layout (overlay plan §0). Old target names stay as `ALIAS` targets | All tests; build receipt; `package-team.ps1`. Binary hashes change because `__FILE__` and PDB paths change, so the test suite and receipt are the proof, not a binary comparison |
| 10 | **Separate repo.** `ember/` becomes its own `ember-core` repository, consumed as a submodule (§4). Also land the `BuildProvenance.ps1` fingerprint fix | Receipt shows the submodule HEAD and its file hashes; full test run |

Steps 1–8 are each a normal-sized review. Step 9 is a mechanical move and step 10 is mechanical too. Land on `release`, then merge into `nightly` (AGENTS.md); see §5.2 on the order.

### When SFV can first show Ember's real shell

**Slice 0 (after step 1).** This is overlay plan M1: theme, `GameMenu` and the SFV panel, driven by `MatchLink`. It is already planned.

**Slice 1 (after steps 4–6 and 9): the first real Ember shell on SFV.** It covers private rooms only: Create room and Join by link → pick → Ready → match starts → back in the room. It needs:
1. V consumes `ember-core` with the runtime, session, rooms and shell (§4).
2. `SfvCatalog` from `GameContent::ReadContentLists()` (`V/mod/GameContent.hxx:12-18`) plus a code-to-index table (`SfvRoster.hxx`, overlay plan §1d), with a null availability reader.
3. `SfvBattle`:
   - `CanStart` means the main-menu scene (`GameScene.hxx` `SceneStateKind`).
   - `Enter` means `VersusLaunch::LaunchOnMainMenu` (`V/mod/VersusLaunch.hxx:64-71`).
   - `Sample` comes from `RollbackSession::Stats` and `ReadRift`.
   - `ConfirmedFrame` and `HashAt` come from `RollbackSession.hxx:174-192`.
   - `RollbackSession` takes its local port and peer address from `MatchSetup` (`127.0.0.1:RemotePort`), as `MatchLink`'s `Endpoints` does today.
4. `SfvGameState`, `SfvInput` (VirtualPads plus `ControllerButtons`), and an `SfvTextureUploader` on DX11 (overlay plan §1d).
5. `ApplicationShell` plugged into `SetOverlayUi` (`V/src/overlay/Overlay.hxx:36-37`).
6. Capabilities: `splitHud`, `displayAdvice`, `lobbyMusic`, `training`, `replays`, `spectators`, `nativeMenuLock` and `classEmblems` all false.
7. A per-frame `TickRuntime` and `PublishPresentationFrame` on SFV's game-thread tick, outside battle as well (game-side reader §3, "Outer tick").

Private rooms do not need the room host: the in-game `SessionServer` (`UserApp::server`) hosts them, and it links SFV's catalog.

**Gaps that remain after Slice 1, in order:**
- **(a)** `onResult`. `MatchEnd` never reads a winner (`MatchEnd.hxx:11-14`, `NetplayMode.cxx:67-70`). Without it, sets, rotation, records and rankings stay off; until then the room treats each game as Cancel.
- **(b)** A rematch decision instead of always-rematch, and generation + 1.
- **(c)** Rounds and time in `MatchPicks`.
- **(d)** Public rooms, which need the room host's `game` key and an SFV build of the room host's catalog.
- **(e)** Spectators. `RollbackSession` handles exactly two players (`:232-233`).

---

## 4. Repo and build mechanics

**Layout of `ember-core`:**

```
ember-core/
  CMakeLists.txt         project(ember); no global flags; each target sets cxx_std_17 and its own warnings
  cmake/                 ember_embed, ember_fonts, ember_locales, ember_brand, ember_version, ember_tests
                         (each takes a SOURCE_DIR argument instead of CMAKE_SOURCE_DIR: sf4e_fonts.cmake:2,
                          sf4e_brand.cmake:3, sf4e_locales.cmake:6)
  src/{common,platform,netplay,session,ui,ui/backends,ui/fonts,launcher/update,discord,game}/
                         same relative layout as S/src today, so includes like ../common/X.hxx do not change
  locales/  assets/{input,brand,flags}/
  rust/sf4-net/          the helper (Cargo, x64 for both)
  tests/                 the neutral tests, with a mock adapter (like V/src/testbed MockFighter)
  server/roomhost/       sources.txt and RoomHost; catalog chosen by game key
```

**Targets** (static, namespaced):
- `ember::common`, `ember::platform`, `ember::localization`, `ember::session`, `ember::rooms`, `ember::runtime`, `ember::ui`, `ember::updates`, `ember::launcher_core`, `ember::discord_presence`, `ember::log`
- `install(EXPORT ember-targets NAMESPACE ember::)` and `emberConfig.cmake`, so a vcpkg port stays possible later

**Consumption: git submodule plus `add_subdirectory`.**
- **S** (x86, `x86-windows-wchar-filenames`, dynamic CRT, C++11–17): `add_subdirectory(ember)`. `build-current.ps1:55` is unchanged, and `CMAKE_HOME_DIRECTORY` is still `sf4-current`, so `Assert-CMakeSource` (`BuildProvenance.ps1:22-28`) passes.
- **V** (x64, `x64-windows-static-md`, C++20): `add_subdirectory(extern/ember-core)`. The submodule replaces `src/shared`, so `SHARED_FROM_EMBER.md` and its drift check are retired. V's dependency manifest adds `fmt`, `spdlog` (or uses the log shim) and the ImGui 1.91.9 pin from overlay plan §1e.
- **Why not a prebuilt binary:** none can serve both, because of the bitness, CRT linkage and language standard. Each consumer builds from source with its own triplet.

**Warnings.** Library targets compile at `/W4` themselves. V must not push `/permissive- /W4` onto them through `add_compile_options` (`V/CMakeLists.txt:19`): either move V's flags to target scope, or have `ember` targets set `/W3` until they are clean. V already lowers `sfve_shared` to `/W3` (`:28`).

**Provenance fix (needed by AGENTS.md).** `Get-SourceFingerprint` (`BuildProvenance.ps1:6-21`) sees the submodule as one gitlink and records it as a constant line, so library edits would not change the receipt. The fix has two parts:
- Add a second pass, `git -C ember ls-files --cached --others --exclude-standard`, with the paths prefixed by `ember/`.
- Record the submodule's HEAD and dirty flag in the receipt.

`package-team.ps1`'s validation then covers the library automatically.

**Versioning and one source of truth:**
- `ember-core` carries its own semver tags (`ember-core v1.0.0` at step 10).
- Each product records the submodule commit in its receipt and release notes.
- Changes are made in `ember-core` first and bumped into S `release` (then merged into `nightly`) and into V.
- A cross-repo change means two commits plus a pointer bump.
- CI in `ember-core` builds both configurations (x86 C++17 dynamic, and x64 C++20 static-md `/W4`), so neither consumer finds out late.

**Nightly.** `sf4-nightly` takes the same submodule. Because `release` is merged into `nightly` after every Stable release, the pointer flows with it. Nightly-only Ember changes go to an `ember-core` branch named `nightly`, and Stable pins a tag.

---

## 5. Risks and open questions for the maintainer

1. **The pick model needs your decision.**
   - **(a)** Each game defines an opaque pick with named dimensions, and the shell renders them generically.
   - **(b)** SFV maps V-Skill and V-Trigger onto SF4's costume, colour, Ultra and edition fields.

   I recommend (a): (b) bakes SF4's words into the room model, the wire format, the locale keys ("ultra", "appearance", "rules.edition_select") and the rankings. Under (a), SF4's dimensions map one-to-one, so SF4's wire output and room JSON stay byte-identical through `Sf4MatchCodec`. Unverified: whether SFV has any pre-match per-round choice like an Ultra; the game-side reader guessed V-Trigger and did not check it in code.
2. **Which channel gets the large move first.** AGENTS.md says land on `release`, then merge into `nightly`. Steps 9 and 10 are a large diff on the shipping line. The alternative is to land steps 0–8 on `release` (small, each reviewed) and do steps 9 and 10 right after a Stable release, so the next merge carries the move once.
3. **Behaviour proof.** Binary hashes will change at step 9. The wire and render goldens (steps 0a and 0b) are new work and must be green before any refactor. The in-game self-test needs your approval to launch each time.
4. **`ember://` with two products.** The last launcher to run owns the scheme (`launcher.cxx:536-556`). The options are a per-game scheme (`ember-sfv://`), or one scheme with `game=` and a small neutral dispatcher. Links already shared in Discord use `embernetplay.link/j#CODE`, so the web page could choose the game. Kernel-object, AppData and pipe names collide too, until step 2 makes them per-product.
5. **Cross-game rooms.** An SF4 player who joins an SFV room is refused only by the `sidecarHash` check (`SessionServer.cxx:514`), which shows as a "different build" message. `gameKey` should travel in invites and the hello, so the message reads "This room plays SFV".
6. **Room host.** Either one process per game (`build_id` plus `game` in its config, `RoomHost.cxx:64`), or one multi-game process. One process per game is simpler and matches today's one-build-per-process design.
7. **The SFV launcher.** The proxy is enough for Slice 1. Links, updates, crash dumps and the recovery UI need an x64 launcher built from `ember::launcher_core`; S's launcher is x86, and Detours injects its own bitness. Its timing is your call.
8. **Logging.** Either add spdlog and fmt to V (simplest, keeps the code identical), or use the `ember::log` shim.
9. **Toolchain friction, not checked.** I could not verify that `sf4e_ui`, `room_model` and `session_server` compile under V's C++20 `/W4 /permissive- /utf-8`. Before step 9, compile `ember/` once with V's flags in a scratch configure.
10. **What SFV's first release offers.** Slice 1 has no winner (so no sets, records or rankings), no spectators, no Training, no replays and no public rooms. Should SFV wait for result reading (gap (a)) before anyone outside sees it?
11. **Overlap of work.** Steps 3–6 touch `NetplayRuntime*` and `SessionClient`, the files with the most active Nightly churn. Expect merge conflicts with in-flight Nightly work; land each step quickly.