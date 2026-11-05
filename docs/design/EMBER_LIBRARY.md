# Design: Ember as a shared, game-neutral library used by SF4 Ember and SFV Ember

> **Status: proposal (2026-11-04), not started.** The maintainer's direction: Ember on SFV
> reuses Ember's own code through a shared library of game-neutral Ember
> code that SF4 Ember and SFV Ember both build against; the game is an
> adapter and Ember's interface is what players see. The maintainer called it a
> long-term pivot that may still change. Nothing here has been done in the
> Ember repo. Decisions needed are in section 5; SFV branding is in 2.1.


Roots: **S** = `C:\Users\user\Desktop\sf4\sf4-current` (branch `release`); **V** = `C:\Users\user\Desktop\sf4\sfv-ember`, HEAD `53da8f0`. Paths are under `src/` unless they start with a root. Nothing was edited, built or run. This document builds on the four readers' reports and `overlay_plan.txt`. Where I checked something new, I say so.

**Correction to the platform-common reader.** V's `src/overlay/` is not untracked. It was committed at `53da8f0` ("Overlay host for Ember's interface on SFV"). `V/src/overlay/Overlay.hxx:25-37` already exposes `OverlayUi{applyScale, draw}` and `SetOverlayUi`, which is where Ember's shell plugs in on SFV. `git status` in V is clean.

## 0. Decisions (the maintainer, 2026-11-04)

- Start now, on a branch of the Ember repo (`feat/ember-library`, off `release`), in its own worktree; nothing merges to `release` or `nightly` without the maintainer. Step 0 (test-only goldens) first.
- Pick model: **game-defined dimensions** (5.1 (a)). The review's "limits first" step (6, step L) comes before it; the generic model follows.
- The large mechanical move (steps 9-10) lands **right after a Stable release**, so the next merge into `nightly` carries it once.
- Development skips the ownership check; the public version keeps it.
- SFV Ember lives in its own repository (the maintainer, 2026-11-04): `sfv-ember` stays separate and consumes the shared library; it never moves into the Ember repo.

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

---

## 6. Review of this design (skeptic pass, 2026-11-04)

Where this section and sections 1-5 disagree, this section wins; its corrected slice is the plan of record.

The plan's overall direction holds up, but several of its key claims are wrong. Five problems matter most:

1. **The runtime is coupled to SF4 far more than the plan says.** Besides the `fSystem` and `Dimps` calls the plan counts, it calls SF4 Ember's own hook layer (input devices, the pad layer, saved preferences, game events, Steam and others) directly.
2. **The shell does not draw fighter select.** The SF4 overlay host draws it, keeps the player's pick, and builds the screen data. The plan leaves all of that in SF4, so SFV would have nothing to pick with.
3. **The adapter only goes one way.** About 22 calls go from the SF4 battle hooks back into the runtime, and none of them are in the interface.
4. **Ports are handed over at the wrong time.** GGPO ports only exist after the match session has started, which is later than the "Enter" call that carries them in the plan.
5. **Slice 1 cannot reach "back in the room" on SFV today.** SFV can only rematch in place; nothing in V leaves a battle for the main menu.

Roots: **S** = `C:\Users\user\Desktop\sf4\sf4-current` (release, 0f4dc0a42); **V** = `C:\Users\user\Desktop\sf4\sfv-ember` (53da8f0). Paths are under `src/` unless they start with a root. Nothing was edited or built.

## Confirmed
- **V's overlay is committed.** `53da8f0` is HEAD and `src/overlay/` is tracked. `OverlayUi{applyScale, draw}` and `SetOverlayUi` are at V `overlay/Overlay.hxx:24-37`.
- **Runtime counts.** It uses exactly 16 distinct `fSystem::` symbols. `UserApp::netplay` has 180 uses and `UserApp::server` 34. Besides those statics, the only `UserApp` calls are `EnterAuthorizedMatch`, `MatchEntry` and `StartIrohSession`.
- **The runtime's line count is 6,297, not about 4,950** (`sf4e/sf4e__Netplay*` plus `RuntimeBridge`).
- **The runtime bridge already works across threads.** `sf4e/sf4e__RuntimeBridge.hxx:2-5` and `sf4e__NetplayFacade.hxx:331-361` say the game thread publishes and any thread can read or submit. That suits SFV, where `Present` runs on a different thread.
- **Input size and packet cap:**
  - `GgpoInputBytes = 8` and `GgpoMaximumPacket` are at `session/MatchAuthority.hxx:11-16` and are used at `session/IrohMatchSession.cxx:86,160,230`.
  - The helper checks `max_packet` with no game-specific limit (`rust/sf4-net/src/bridge.rs:102,120`, `service/games.rs:474`).
  - V already computes the cap from the input size (V `core/MatchLink.hxx:43-46`).
- **Same GGPO build.** The GGPO overlay port is identical in both repos (`vcpkg-overlays/ports/ggpo`, checked with `diff -rq`).
- **Private rooms use the in-game server.** They are hosted by the in-game `SessionServer`, created in `AttachRoom` (`sf4e/sf4e__NetplayRuntime.cxx:179-191`) from `sidecarHash` and a `FixedPoint` round time.
- **Identity literals** are where the plan says: `platform/HelperProcess.cxx:45`, `discord/BridgeServer.hxx:19`, `platform/JoinLinkMailbox.hxx:27-32`, `ui/ApplicationShell.cxx:712`.
- **Build links.** `Session` links `Dimps` privately (`CMakeLists.txt:316-324`). `sf4e_session_server` links `sf4e_selection` (`:301-302`), and the package installer does too (`:403`).
- **Verification** writes `rSystem RS_ISLEAVING` (`session/sf4e__SessionClient__Verification.cxx:163`) and reads `snapshotMap` at `:176` and `:240`.
- **SFV picks** are fighter, costume and colour only (V `mod/VersusLaunch.hxx:35-39`). No V-Skill or V-Trigger appears anywhere in V's code; whether they are pre-match choices is still unverified.
- **V has no spdlog.** Its warning flags are global (V `CMakeLists.txt:19`), and `sfve_shared` is lowered to `/W3` (`:28`).

## Wrong (evidence, then correction)
1. **"The runtime becomes neutral once two things happen."** False. The runtime also calls SF4 Ember's hook layer:

   | Namespace or call | Uses | Where it comes from |
   |---|---|---|
   | `input::*` | about 70 | `sf4e__InputDevices`: `ReadDevices`, `AssignToSide`, `ReleaseFromSide`, `ReadAssignedInput`, `ScanKeyboard`, `RestoreController` |
   | `Gamepads::*` | 18 | the SDL pad layer: `ReadFrame`, `ControlSetup`, `SetLayouts`, `BeginSetup`, `SaveSetup`, `BrandForDevice` (`NetplayRuntime__Input.cxx:20-295`) |
   | `OverlayPrefs::*` | 24 | Save/Queue player preferences; `sf4e__OverlayPrefs.hxx:11-12` includes the fighter and stage catalogs |
   | `GameEvents::*` | 11 | `VsBattle::nextRequest` (`NetplayRuntime.cxx:32`, `__Match.cxx:306`) and `MainMenu::RequestTraining` |
   | others | — | `MenuMusic`, `AwayAlert`, `steam::*`, `custom::Installed`, `training::*`, `DirectInputFilter`, `Overlay::MenuPads()` (`__Input.cxx:67,150`), `SelfTest`, `sidecarHash`, `localRand`, `GetConfig()` |

   `AttachRoom` will not join a room until `runtime->input.Ready()` (`NetplayRuntime.cxx:181`). **Correction:** list these in §1.0. `game::Input` also needs device enumeration and assignment, plus the pad-setup and layout pipeline, or a capability that turns them off.

2. **"`RuntimeBridge.hxx` is already pure."** Only its own body is. It includes `NetplayFacade.hxx`, and that header includes:
   - `Dimps__GameEvents.hxx` (`:42`)
   - `FighterCatalog`, `StageCatalog` and `InstalledContent` (`:7-9`)
   - `ReplayFiles` (`:31`)
   - `ControllerView` (`:14`)
   - `sf4e__NetplayConfig` (`:16`)

   The snapshot and command types it carries are SF4 types: `ConfirmedCharaConditions` (`:121`), `selection::RandomPool` (`:124`), `gamepads::*` (`:166-172`), `std::array<Availability, FighterCount>` (`:186`) and `InstalledContentPtr` (`:188`). **Correction:** the *pattern* works; the boundary does not exist yet.

3. **Step order: step 4 cannot drop the Dimps includes before steps 5–6.** `NetplayRuntime.hxx:152` and `NetplayFacade.hxx:121,374-375` hold the native pick type. **Correction:** make the pick type and catalog limits neutral before, or together with, step 4.

4. **§1.1's "neutral" UI list pulls in SF4 files:**
   - `OverlayLayers.hxx:3` and `ReplayPlaybackHud.cxx:5` include `TrainingPanel.hxx`.
   - `RoomPanelTables.cxx:16` and `ReplaySummaryText.hxx:8` include `FighterSelector.hxx`, which pulls in `SelectionArt` and so D3D9. `ReplaySummaryText` is included by `ApplicationShell.cxx:24`.
   - `CountryFlag.cxx:3` and `NameMarks.cxx:4` include `SelectionArt.hxx`.
   - `ApplicationShell.hxx` includes `GameDisplayConfig` (`:8`) and `ControllerView` (`:40`), and holds a `selection::Pick` (`:184`) and a `ReplaysPanel` (`:46,404`). `ReplaysPanel` pulls in `ReplaySets`, `ReplayFiles` and `ReplaySender`.

   Yet §1.2 says `GameDisplayConfig`, `ControllerView`, `GamepadBrand` and `ReplayFiles` stay in SF4, so the plan contradicts itself. `lab/UiLab.cxx` is a D3D9 harness (`:36`, `:339`), not neutral. `ApplicationShell.cxx`, `__Settings`, `__Training`, `__Updates`, `__ButtonLayout`, the `RoomPanel*`/`RoomActions`/`RoomControls` files, `ControllersScreen`, `PadSetupScreen`, `ButtonLayoutScreen` and `Recovery*` are in neither list.

5. **`sf4e_ui` is a single target.** It builds `SelectionArt`, `FighterSelector`, `MatchHud`, the `Training*` files and `Speech.cxx`, and links `d3d9` (`CMakeLists.txt:96-119`). No step splits it before step 9. **Correction:** add that split as its own step.

6. **"The shell itself never touches the game; it sends commands and reads the snapshot."** Fighter select is drawn by the host, not the shell:
   - The shell calls a `DrawSelection` callback the host passes in (`ApplicationShell.cxx:738-752`, `.hxx:312`).
   - The SF4 host draws `FighterSelector` (`sf4e/sf4e__Overlay.cxx:80,274,300`).
   - The player's pick and the Training dummy live as statics in that host (`:107-125`), not in the runtime.
   - About 250 lines of `selection::*` code build `ShellView` and send `Submit` (`:336-560`).

   **Correction:** pull a neutral `ShellHost` out of `sf4e__Overlay.cxx`: snapshot to `ShellView`, `Submit` to `RuntimeCommand`, and `Background`. The selection screen becomes an adapter hook (`DrawSelection(Pick&)`), and the pick's storage moves into the runtime.

7. **`MatchSetup` puts ports in `Enter`, which is too early.**
   - Today `Enter` is `OnReady` / `TryStartPendingMatch` → `StartMatchFromLobby`, reading the room's lobby and match data (`UserApp.cxx:88-145,338-341,387-397`).
   - Ports exist only once `IrohMatchSession` is `Started`. At that point the game pulls `GetRuntimeMatchEndpoints`, then calls `ReleaseRuntimePortToGgpo`, then starts GGPO (`NetplayRuntime__Match.cxx:284-297`; `UserApp.cxx:180-240`).
   - The battle-loaded and synced handshake comes between them: `client.Battle_Loaded()` at `GameEvents.cxx:351-358`, and `OnBattleSynced` at `UserApp.cxx:343-345`.

   **Correction:** split it into `Enter(picks, rules, seed)` and library calls the game makes at its GGPO start: `MatchEndpoints()`, `ReleasePort()`, `BattleLoaded()` and an "is synced" check.

8. **`Battle::StartTransport` is in the wrong layer.** `StartIrohSession` only builds the `SessionClient` (`UserApp.cxx:402-408`), which is library work. What the game adds is its build id and the callbacks (`clientCallbacks`, `:351-356`).

9. **"Nothing checks the wire bytes."** Partly false: `tests/room_private_golden_test.cxx` already compares SHA-256 hashes of the room snapshot and checkpoint JSON. What is missing is a byte check on `SessionProtocol` messages and `PlayerPreferences`. **Correction:** step 0a should extend this existing golden test.

10. **The "pick checks" list is incomplete.** It also misses:
    - `SessionClient.cxx:456,465`
    - `SessionProtocol.cxx:37,39` (the rejected-match check)
    - `SessionProtocol.hxx:529` (`ReadStage`)
    - `SessionServer__Handlers.cxx:104` (`FighterAllowed` with the edition)
    - `SessionPlainTypes.hxx:46`
    - `room::Chara` and `Member` keep named `edition`/`costume`/`color`/`ultra`/`keptEdition` fields (`session/RoomTypes.hxx:198-222,306-310`).

    Under decision (a), `PickDimension` also needs a JSON key and a "kept with the fighter" flag, because `edition` affects the rules. The Linux room host must know these too.

11. **V's vcpkg.json still has imgui `win32-binding` and `version>=1.91.5` with no pin.** That goes against overlay plan §1e. Treat the change as still to do, not as already done.

12. **Identity covers the window title but not the strings.** `locales/en.po` has 2,751 entries, and 73 of them use SF4, Ultra, edition or Steam wording, for example "SF4 Ember Netplay" and "Ultra Street Fighter IV could not be found." **Correction:** add per-game locale overlays.

13. **Slice 1's "back in the room" cannot happen on V today.**
    - `NetplayMode.cxx:69` always calls `Rematch`.
    - `MatchEnd.hxx:11-14` says the decision is always a rematch.
    - Neither `MatchEnd.cxx` nor `VersusLaunch` leaves a battle for the main menu. The only path is the failed-launch `kModeBackToMainMenu` (`VersusLaunch.cxx:139`).
    - When a session fails, the battle "carries on offline" (`NetplayMode.cxx:44-48`).

    This is an SFV game-side prerequisite, not "gap (b)".

## Missing pieces
- **Calls from the game back into the library.** The SF4 battle hooks call 22 distinct `NetplayFacade::` functions: `NotifyGgpoSyncPhase`, `NotifyMatchEnded`, `NotifyRuntimeMatchResult`, `NotifyRuntimeBattleClosedWithoutSession`, `NotifyRuntimeEventSystemReady`, `FinalizeControlPlaneLossAfterBattle`, `CancelDeferredGgpoClose`, `ClearBattleState`, `PushAlert`, `ReadRuntimeMatchInput`, `BindRuntimeInput`, `IsRuntimeMatchEndCommitted`, `TrainingRequestHolds`, `RematchInPlaceOffered`, `IsRuntimeSpectator`, `TickFrame` and others. These need a `RuntimeEvents` interface that the adapter calls.
- **The outer tick.** Its order lives in `UserApp::Steam_PostUpdate` (`UserApp.cxx:410-500`):
  1. `TickRuntime`
  2. `PrepareForCallbacks`
  3. `client.Step`
  4. `server.Step`
  5. `ObserveControlPlane`
  6. `ReconcileTrainingCall`
  7. `TickFrame`
  8. `ggpo_idle`
  9. `DrainPendingAbort`
  10. `PollNativeMatchResult`
  11. `PollSpectators`
  12. `StepPacing`
  13. the original engine call
  14. `PublishPresentationFrame`

  Steps 1–7 and 14 belong in a neutral `ember::Tick()`; the rest are `Battle` hooks. Slice 1's item 7 (only `TickRuntime` and Publish) leaves out stepping the session client and server. V can host the tick with `RunEachGameFrame` (V `mod/GameThread.hxx:18`).
- **Helper start.** SF4 gets the helper from the launcher payload (`NetplayRuntime.cxx:373-388`). SFV starts it in-process (V `mod/HelperLink.cxx:81-89`), so the runtime should take a `HelperBootstrap` supplied by the adapter. V's `HelperLink` and `MatchLink` must be retired once V uses `IrohRoom`; the plan never says so.
- **Capability flags still missing:** controllers, pad setup and button layout, the watched picture, Steam profile, the away alert, installed or custom content, and random pools.
- **SFV roster versus `FighterCount = 44`** (`common/FighterCatalog.hxx:9`). From game knowledge (not checked in V's code), SFV CE has 45 playable fighters. So reusing today's fighter index fails at `RoomModel.cxx:109`, `RoomModelJson.cxx:398-400` and the fixed-size availability arrays. Fighter, costume and ultra limits must come from the catalog at run time before any SFV slice.
- **Round count and time.** SFV's `MatchPicks` has no rounds or time. In Slice 1 the shell must lock or hide those rules, or it will offer rules SFV ignores.

## Corrected smallest vertical slice for SFV
Scope: a private room, two players, one game per Ready, an explicit end that returns to the room, and no result read (the game is recorded as Cancel).

1. **V prerequisite.** A synchronized exit from the battle to the main menu, used both at match end (instead of `Rematch`) and when the session fails. Both copies must decide it at the same confirmed frame, the way `MatchEnd` already decides a rematch.
2. **S, step 0a.** Extend `room_private_golden_test` to cover `MatchData`, `PreBattleSetChara`, `LobbyData` and `PlayerPreferences`.
3. **S, step 1.** Overlay plan §1a, plus making `ReplaySummaryText`, `RoomPanelTables` and `OverlayLayers` stop including `FighterSelector` and `TrainingPanel`.
4. **S, step 2.** Identity, with the window title and locale overlays.
5. **S, step L (new, before the adapter step): limits only, not the generic pick model.**
   - `FighterCount`, `CostumeLimit` and `UltraChoices`, the availability arrays and the stage checks become catalog values read at run time.
   - The wire keeps today's field names.
   - SFV uses `charaID` = its roster index, plus `costume` and `color`; `ultra` and `edition` are 0 or -1, with `editionSelect` off.
   - SF4's bytes do not change, and decision §5.1 is deferred.
6. **S, steps 3+4, reduced.**
   - Pull `ember::Tick` and `ShellHost` out of `UserApp.cxx` and `sf4e__Overlay.cxx`.
   - Put `fSystem`, `GameEvents` and `UserApp` behind `Battle`, with the split from Wrong #7 and the game-to-library calls.
   - Training, replays, spectators, the pad wizard, `OverlayPrefs` paths, Steam and custom content go behind capabilities, all set true for SF4.
7. **Library on V.** Add the library as source with C++17 code compiled under V's flags; check that build in a scratch configure first.
8. **The SFV adapter:**
   - `CanStart` means the main-menu scene.
   - `Enter` calls `LaunchOnMainMenu` with picks taken from the SFV roster.
   - In `NetplayMode::StartSession`, replace `link_->Ready()` with the library's endpoints query and port release.
   - Report battle-loaded at `NewBattle`, and start `RollbackSession` only once synced.
   - `Sample` reads `RollbackSession` stats and `ReadRift`.
   - `ConfirmedFrame` and `HashAt` come from `RollbackSession.hxx`. The desync check compares one overall hash, with the per-character hashes left empty.
   - `End` and `Leave` use the step-1 exit.
9. **Selection on SFV.** A `DrawSelection` provided by the host, built from `GameMenu` rows over `ReadContentLists()` (the pick rows of the overlay plan's SfvPanel). Do not port `FighterSelector` for this slice.

After that, in order: read a winner (results, sets, rankings), rounds and time in `MatchPicks`, the generic pick model (decision a) with V-Skill and V-Trigger if they turn out to be pre-match choices, public rooms through the room host's `game` key, and spectators.

## 7. x64 / C++20 compile probe (2026-11-04, snapshot 820dc938c)

The NEUTRAL set (section 1.1, plus the refactor-first files) was compiled with SFV's flags (x64, `/W4 /permissive- /utf-8`, `/MD`, C++20, x64-windows-static-md): 103 `.cxx` files and one translation unit per NEUTRAL header, `/c` only. A canary confirmed C4244 fires with these flags. **There were no narrowing or pointer-to-int warnings in Ember's own code, no inline asm and no `SetWindowLong` without `Ptr`.** The whole set builds in about 1.5 minutes on this PC. Everything that failed is listed below, with the step that absorbs it.

| Class | Sites | Cause | Fix / step |
|---|---|---|---|
| 32-bit layout `static_assert`s | 7 | `common/KeyboardSlots.hxx`, `common/ReplayRecorder.hxx` describe SF4 engine objects | Both are SF4-only: add them to section 1.2 |
| Dimps pointer casts (C4311/C4302/C4312) | 129 | `IrohRoom*`, `IrohMatchSession`, `MatchAuthority.hxx`, `RoomMessageTag.hxx` and `RoomRecoveryRuntime.hxx` reach Dimps through `sf4e__SessionProtocol.hxx`, then `SessionPlainTypes.hxx` under `_WIN32` | Step 6: the plain (room host) branch becomes the default, Dimps only under an SF4 define; its `typedef unsigned int DWORD` becomes `std::uint32_t`. `IrohRoom*` is therefore not "unchanged" until step 6 |
| `path::u8string()` is `std::u8string` (C++20) | 6 | `launcher/update/PackageInstaller.cxx:566,674,773,784` | One `Utf8(const fs::path&)` helper; also covers the deprecated `u8path` (10 sites, incl. `ZipExtract.cxx:98`, `ReplayInputDetails.hxx:68`) |
| Comparing two enum types (C5054, C++20) | 14 | `ui/ControllerNavigation.hxx:26-31` `static_assert`s | Cast both sides to `int` |
| spdlog wide file name | 1 | `platform/VideoLinkServe.cxx:22` needs `SPDLOG_WCHAR_FILENAMES`, which SF4's overlay triplet sets | SFV adds an x64 overlay triplet with the same option (part of the logging decision in 1.3) |
| Missing includes | ~10 | e.g. `common/ButtonLayoutCapture.hxx` uses `optional`/`vector` without including them | Include what is used |
| `/permissive-` `typename` | 2 | `sf4e/sf4e__Platform.hxx:149,156` (SF4 adapter, reached via `SessionClient`) | One word each, if SF4 ever builds `/permissive-` |
| /W4 noise | ~15 | shadowing, unused parameters, `strncpy`/`getenv`, `NOMINMAX` redefinitions (`CountryPicker.cxx:10`, `JsonFileStore.hxx:3`) | Fix in place |

**NEUTRAL files that reach SF4-only headers the plan did not list:**
- `ui/ApplicationShell.hxx` (included by every `ApplicationShell__*`) pulls `FighterCatalog`, `GameDisplayConfig`, `ControllerView`, `ReplaysPanel` → `ReplayExport`/`ReplayFiles`, and `PlayerPreferences`. `ApplicationShell__Values.cxx` reaches `FighterCatalog` through `ReplaySummaryText` → `SelectionLabels`. Steps 5 and 7 (catalog and capabilities) cut these. The shell is Ember's interface and moves into the library; the overlay plan's "do not copy ApplicationShell" rule was for the SFV stand-in panel, which the maintainer rejected.
- `ui/OverlayLayers.cxx` → `TrainingPanel.hxx`; `netplay/ControllerInventory.hxx` and `common/ControllerHints.hxx` → `ControllerView.hxx`; `session/RandomFighterPick.hxx` → `RandomFighter.hxx`; `launcher/GameLocator.cxx` → `SteamKeyValues.hxx`; `github_release_client_internal.hxx` → `PackageInventory.hxx` (step 8).
- `ui/lab/UiLab.cxx` is the D3D9 harness (`imgui_impl_dx9`, `FighterSelector`, `TrainingPanel`): it goes to section 1.2, and SFV gets its own DX11 lab.

**SFV's `vcpkg.json` needs:** `fmt`, `spdlog` (with wide file names) and `zlib`. The two GGPO overlay ports differ: SFV's carries `ignored-inputs.patch`. Reconcile them before the library shares `ggponet.h`. The simplest fix is to add the patch to Ember's port, since the call is opt-in.

## 8. Step 6 as built (2026-11-05)

Step 6 came before the SFV adapter, because SFV cannot compile the library's session code on x64 until the wire stops reaching SF4's engine headers.

It follows section 6 ("the wire keeps today's field names"), not the section 3 row that says FixedPoint and the pick "live only in `Sf4MatchCodec`":
- **One definition of the wire's match structs.** `common/MatchWireTypes.hxx` defines `sf4e::wire::FixedPoint` and `sf4e::wire::ConfirmedCharaConditions`, with their JSON codecs and `static_assert`s on their sizes (4 and 9 bytes). SF4's engine headers alias them (`Dimps::Math::FixedPoint`, `VsMode::ConfirmedCharaConditions`), so the 43 SF4 uses compile unchanged and there is no `#ifdef _WIN32` in the shared path. `SessionPlainTypes.hxx` is gone. `MatchData::rngSeed` is `std::uint32_t`, which encodes to the same JSON as `DWORD`. The wire goldens pass with no fixture changed.
- **The desync check is the game's.** `session/BattleVerifier.hxx` is the interface: the remote legacy snapshot, a step per client step, and Reset. Each game defines `MakeBattleVerifier()` once, as it defines `Game()`. The room client keeps the neutral part: it decodes both messages, buffers the remote v2 hashes and owns the verifier. SF4's verifier is `sf4e/sf4e__Sf4Verification.cxx`: the old `SessionClient__Verification.cxx` moved, with its pending snapshots, the desync exit and the reports. The fake game's verifier compares nothing. For SFV, the verifier publishes one overall hash per confirmed checkpoint, with `flow`, `chara0` and `chara1` set to 0, and never sends v1 snapshots.
- **The room client includes nothing of SF4's.** Its Dimps, battle-system and facade includes had no uses. `MatchAuthority.cxx` now includes `windows.h` before `bcrypt.h` itself; it used to get it from the engine headers.
- **Left as it is:** `GgpoInputBytes = 8` (`MatchAuthority.hxx`) bounds the room's gameplay packets. SFV's `PackedInput` is 4 bytes, so the bound holds, and taking it from the game can wait for a game with wider inputs.

## 9. Library on V: the runtime under x64 (2026-11-05)

The x64 library build (`cmake/ember-x64-probe`, `scripts/probe-x64.ps1`) compiles the runtime (`ember_runtime_x64`, x64 C++20 `/W4 /WX`), then links and runs the runtime's test under the fake game (`ember_runtime_x64_test`, the x64 build of `RuntimeCapabilitiesTest`). Every run of the script does this; the `-LinkRuntime` switch is gone. The runtime's source list is `cmake/sf4e_runtime_sources.cmake`, which SF4 Ember's build reads too.

The test links because the runtime no longer reaches SF4's side. The first version of this section counted 27 SF4 symbols, in four groups. When this work began, the test's link named 21 of them, because nothing it linked called the pick labels yet. All four groups were moved anyway. Each group was moved as follows (S `feat/ember-library`, commits 48660dc28 to 8fcc1f88d). SF4's behaviour is unchanged: the same labels, art files, replay archive and language detection.

| Group | Was | Now |
|---|---|---|
| Game language | `LocaleWindows.cxx` called SF4's `DetectGameLanguage` (`SetGameDirectory`) | `GameState::Language()` gives the game's language code, and the runtime records it as it starts (`platform::SetGameLanguage`). SF4's adapter detects the language from the game's folder, as before. The launcher detects it the same way for the folder it found. `LocaleWindows.cxx` is neutral: the Windows UI languages, Steam's language and the country |
| Pick labels | Defined in `ui/FighterSelector.cxx` | `SelectionLabels.hxx` declares all six (`CostumeLabel`, `ShortCostumeLabel`, `ShortAppearanceLabel`, `UltraLabel`, `ShortUltraLabel`, `StageLabel`). Each game defines them once at link time, as it defines `Catalog()`. SF4's moved unchanged to `ui/Sf4SelectionLabels.cxx`. The fake game's (`tests/fake_selection.cxx`) are plain numbered names |
| Selection art | `ui/SelectionArt.cxx` was SF4's | `SelectionArt.cxx` is neutral: the cache, the decoding worker, the uploader and Ember's own art (menu backdrop, input prompts, flags, developer mark). The game's pictures (`Portrait`, `Character`, `Appearance`, `Ultra`, `Stage`, `RankingEmblem`) are defined once per game. They name their files through the class's private `Request` and `PackagedFiles`. SF4's moved unchanged to `ui/Sf4SelectionArt.cxx`, with its patch-folder paths. The fake game's are "missing" pictures |
| Replay archive | The runtime called `platform::replays::*` directly | `game::Replays` adds `ArchivePath`, `WantListing`, `LatestListing`, `WantDetail`, `LatestDetail`, `StopListing`, `MarkKept`, `MarkSent`, `MarkSetClosed`, `SetPackPath` and `ImportSetPack`. Listing and detail may be called from any thread, because the shell's host asks for them. SF4's methods call the same functions as before; the fake game's archive is empty. `ApplicationServices` is given the archive folder when it is constructed (the runtime passes `Game().replays.ArchivePath()`), so "Open folder" and "Show in folder" no longer call `FindFolders`. `ArchivePath()` is split out of `FindFolders`, so asking for the folder does not scan Steam's saves |

**The art's seam is link-time, not an adapter interface.** Three reasons:
- The shell calls the game's pictures (`GameMenu`, `NameMarks`, Settings), but only the game's own code constructs `SelectionArt`: SF4's overlay, the recovery window and the UI tests. An injected interface would have to pass through every constructor; link-time definitions need none.
- It is one definition per binary, like `Catalog()`, the labels and `MakeBattleVerifier()`, and a binary without one fails to link.
- SF4's members moved verbatim, so their cache keys, files, sizes and log lines are the same.

**The image decoding is generic and stays neutral.** EMZ/EMB (a zlib stream with an entry table) and plain 32-bit DDS are recognised only by their magic bytes, and use no SF4 type or path. WIC decodes everything else. SFV's files never match those formats.

**SF4's build.** `sf4e_ui` carries SF4's labels and pictures, so SF4's `RuntimeCapabilitiesTest` uses them, as it uses SF4's catalog. Only the x64 test links the fake game's.

**The probe's lists.** `platform/LocaleWindows.cxx` and `ui/SelectionArt.cxx` left `EMBER_SF4_SIDE`. `ui/Sf4SelectionLabels.cxx` and `ui/Sf4SelectionArt.cxx` joined it, and `tests/fake_selection.cxx` joined the test's files. The neutral library publishes the generated locale ids (`generated/public`), as `sf4e_localization` does in SF4's build. `winmm`, `ole32` and `windowscodecs` are link libraries.

**Still SF4's:**
- The test's roster, stages and identity (step 5).
- The archive's listing type (`ArchivedReplay`) lives in SF4's `platform/ReplayFiles.hxx` until step 9.
