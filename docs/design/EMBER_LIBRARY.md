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
// As built: a constant table on the product instead, ember::BattleHud (Branding::battleHud), section 18.

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

**Superseded (the maintainer, 2026-11-06):** SFV Ember gets its own colours, mark,
backdrop and shapes, similar to Ember's but reading as Street Fighter V's; the
whole theme is now the product's (`Branding::theme`, an `ember::ShellTheme`).
See section 17.

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
- **(b)** A rematch decision instead of always-rematch, and generation + 1. (Done, section 19: the "+ 1" is the room's game count within the match, as SF4's.)
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

## 10. SFV adapter, stage A (2026-11-05)

SFV's game definitions for the library are in `src/ember/` (static library `sfve_ember`). The mod does not link them yet: only `sfve_ember_tests` does, together with the mod modules the adapter calls and the library's runtime. That link proves SFV now defines everything the runtime needs at link time.

**What exists**

| Definition | File | What it is |
|---|---|---|
| `ember::Catalog()` | `SfvCatalog.cxx` | Filled once at run time by `FillCatalog(mod::ReadContentLists())`, before the runtime starts, so ids are positions in the game's lists. Before that it has no fighters and `Usable` is false. Costume and colour limit: 100, the two digits `ParseSetup` takes; the game's own check runs at launch (`kCostumeValid`/`kColourValid`). The game data gives no tighter limit. Ultra choices: 1 (always 0). Pick rule: fighter in the roster, costume and colour under 100, Ultra 0, handicap 0; edition, personal action and win quote are ignored. Stage rule: a position in the stage list. There is no random stage. Because ids are positions in each PC's lists, `FillCatalog` also sets the build id (`MatchBuildId`): `kMatchBuild`, "/", and 16 hex digits of an XXH3 hash of both code lists (`ContentFingerprint`). Copies with different installed content fail the room's build check instead of disagreeing on fighter N. `LaunchPicks` builds `MatchPicks` from the typed pick and stage codes; the room's rounds and time must be ones SFV plays (`PlayableRounds`: 1/3/5/7; `RoundClockFrames`: 30-546 s), the same rules `ParseSetup` applies to text |
| `ember::Product()` | `SfvProduct.cxx` | "SFV Ember" and "Street Fighter V". Folder and object stem `SFVEmber`; `sfv_ember.dll`, `StreetFighterV.exe` and `sfv_ember.log`; URL scheme `ember-sfv` (5.4 is still open). The mark and the backdrop are Ember's until SFV Ember has its own art. The accent is Ember's orange. No release repositories, launcher, updater or Discord bridge yet, so those names are empty. `sf4e_embed_brand_mark` (library) embeds the mark |
| `MakeBattleVerifier()` | `SfvVerification.cxx` | `CheckpointExchange`: one overall hash per confirmed checkpoint (`RollbackSession::HashAt`, every 60 frames). A checkpoint is taken only once the session has saved it: GGPO confirms frames up to the input delay before they run. Only a checkpoint older than the session's hash history is skipped. `flow`, `chara0` and `chara1` are 0, and no v1 snapshot is sent. A watcher publishes nothing and its hashes never count. The battle's first mismatch logs, pushes the desync notice and calls `Battle::Fail`. When the confirmed frame goes back (a new battle), the check starts over and drops the remote hashes it holds |
| Labels and pictures | `SfvSelection.cxx` | Stage by its code; "Costume N" and "color N" use the game's numbers; Ultra labels are empty. Every picture reports missing |
| `ember::game::Game()` | `SfvGame.cxx` | Every capability is off (spectators are on since section 25). Training and replays are the library's `NoTraining`/`NoReplays` (`sf4e__AbsentGameFeatures.hxx`). `SetBattleInPlay` is the seam through which the adapter and the verifier read the mod's battle (its `RollbackSession` and `SfvAdapter`) |

Mapped to existing modules: `BuildId` → `MatchBuildId` (SfvCatalog: `kMatchBuild` and the content fingerprint). `MainMenuInFront` and `ForegroundScreen` → `GameScene` (two new queries). `Enter` → `LaunchPicks` + `LaunchOnMainMenu` (a room's game always goes back to the room; `MatchEndChoice` was retired in section 12); it arms a `LaunchArming` ticket, and the launch waiting for the main menu goes ahead only while that ticket is armed. `DropNextBattle` drops it, so a player who left does not launch the old picks. `Live`, `FightRunning` and `Sample` (ping) → `RollbackSession`. `GameResult` → `MatchEnd::DecidedMatchResult`: the result `TakeEndedVersusBattle` reads on the confirmed frame it takes the match's end, kept until the next battle; `Abort` (no result) before that. `ResultHeld` and `RematchChoices` → `MatchEnd`. `ReadAssignedInput` → `SfvAdapter::Sampled(0)`. `FighterAvailability` reports everything owned (ready, every costume, colour and personal action), so the runtime's Ready check takes SFV picks; the game's own ownership check judges content at launch.

**Stubbed (neutral value, one-line comment in the code)**
- **GameState:** `Language` (contract issue queued), `Random`, `PlayChallengerCall`, `CloseGame`.
- **Battle:**
  - `OnSessionSynced`.
  - `Retire`, `Abort` and `Fail` log "exit not wired": `NetplayMode::Leave` is private.
  - `GamesPlayedInPlace`, `DisconnectCountdownMs`, `RecentRollbackFrames`, `PingSummary`.
  - The connection warning, prediction stall and applied delay in `Sample`: `RollbackSession` does not expose them.
  - Spectators; `VerificationHeldAt`.
- **Input:** no device list, side assignment, pad layer or pad setup. With no devices, `runtime->input.Ready()` stays false, so the runtime will not join a room until SFV lists its pads.
- **Preferences:** a save is taken and written nowhere.
- **Services:** Steam, music, away sound, custom content, self-test.
- A failed launch is only logged.

**Left for the next step** (done in section 12). These three stay as they are until the runtime runs in the mod:
- `Retire`, `Abort` and `Fail` only log. `NetplayMode::Leave` becomes their target (below).
- The Input stub lists no devices, so `runtime->input.Ready()` stays false: `AttachRoom` returns early and a join or Ready is refused until SFV lists its pads and assigns one to its side.
- The runtime start must check `ember::Usable(ember::Catalog())` after `FillCatalog`, and log instead of starting when the lists were empty or refused.

**Where each RuntimeEvents call goes (the next step)**
- `FillCatalog(ReadContentLists())`, then the runtime start, in `dllmain` once the lists are not empty. `ember::Tick` runs on `RunEachGameFrame`. `NotifyRuntimeEventSystemReady` goes in `GameScene`'s tick, the first time the main menu is in front.
- `NetplayMode::NewBattle`:
  - `SetBattleInPlay`, then `ClearBattleState`, `CancelDeferredGgpoClose` and `ResetGgpoBattleWatch`.
  - `NotifyRuntimeBattleLoaded` when `HasRuntimeRoomLink`.
  - `OnSessionSynced` sets the flag `StartSession` waits for.
- `NetplayMode::StartSession`:
  - Replace `link_->Ready()` with `GetRuntimeMatchEndpoints`. `localSlot` becomes `localPlayer`, and the other slot's `remotePorts` entry becomes `remotePort`. The seed and delay come from the room's match data.
  - Call `ReportGgpoTransport("127.0.0.1", remotePort)` and `ReleaseRuntimePortToGgpo` just before `session_->Start`.
  - After the session starts, call `NoteRuntimeMatchStart`. On failure, call `PushAlert`.
- `NetplayMode::Frame`:
  - `NotifyGgpoSyncPhase` as `Live`/`Running` change.
  - At `TakeEndedVersusBattle`, call `NotifyRuntimeMatchResult` with `GameResult()` (MatchEnd's decided result), the frame, `LastConfirmedFrame`, 0 and the rematch choice. Then `RematchInPlaceOffered` decides `Rematch` or `Leave`, in place of the fixed `MatchEndChoice`.
  - In the no-session branch, call `NotifyRuntimeBattleClosedWithoutSession`.
- `NetplayMode::Leave` becomes the target of `Retire`/`Abort`/`Fail` and calls `NotifyMatchEnded`. `FinalizeControlPlaneLossAfterBattle` runs once the main menu is back if `IsControlPlaneLost`. `ShutdownNetplay` runs at process detach.
- `MatchLink`/`HelperLink` are retired: the room's grant, ports and picks replace them. `ReadRuntimeMatchInput`, `BindRuntimeInput`, `TrainingRequestHolds` and the spectator calls are not used while those capabilities are off.

**Still SF4's in this link (until step 5, section 11, which removed it):** `sfve_ember_tests` compiled the library's `FighterCatalog.cxx`, `StageCatalog.cxx` and `InstalledContent.cxx` because the runtime still reaches SF4's roster and stage tables (step 5). The Ready check is one of them: `selection::Available` runs SF4's `Valid` (44 fighters, 6 or 7 costumes, 12 or 22 colours, SF4's editions) after the availability, so an SFV pick outside those ranges is refused until step 5 checks picks with `Catalog().validPick`.

## 11. Step 5 as built (2026-11-05)

Step 5 routes every pick and stage decision, and every fighter fact the shared screens show, through `ember::Catalog()`. SF4's roster tables (`FighterCatalog`, `StageCatalog`, `InstalledContent`) are now reached only from SF4's catalog definition (`product/Sf4Catalog.cxx`), SF4's labels, art and package files, and SF4's adapter. SF4's behaviour and wire bytes do not change: each new SF4 entry is the function or table the shared code called before, and `game_catalog_test` pins them.

**The seam: `GameCatalog` grew, no new mechanism.** It stays C++11 data with forward-declared types, so the Linux room host still builds it with g++ alone.

| Field | What the shared code asks | SF4 | SFV |
|---|---|---|---|
| `randomStage` | Which stage choice stands for Random (-1: none) | `RandomStageId` (255) | -1 |
| `fighterLocked`, `availablePick` | Whether the player owns the fighter, and the whole pick; the runtime's Ready check | `FighterLocked`, `Available` | never locked; `ValidPick` |
| `normalizePick` | A pick made valid, and owned where availability is given; Random fighter's candidates and rolled look | `Normalize` | fields SFV refuses become 0 |
| `resolveStage` | The stage P1 sends for a choice | `ResolveStage` over the installed custom stages | a listed stage, else 0 |
| `fighterName` | A fighter's name on every shared screen | the roster table's name | the game's code ("RYU") until a names table exists |
| `costumeCount` | How many costumes are the game's own (the replay's "look unknown") | `CostumeCount` | 100 |
| `rosterOrder` | The order of the main-character list | `RosterDisplayOrder` | list order, filled by `FillCatalog` |

`Usable()` now also requires every rule, fact and the roster order.

**Types.** `Pick`, `Availability`, `InstalledContentPtr`, `FromNative` and `ToNative` moved to a neutral `common/SelectionPick.hxx`; `FighterCatalog.hxx` keeps only SF4's tables and functions and includes it. The shared code (session, room client and server, runtime, shell, adapter interface) includes `SelectionPick.hxx` or `GameCatalog.hxx`, not `FighterCatalog.hxx`. `Availability` keeps SF4's bit layout; only a game's own catalog rules and select screen read it.

**Sites changed.** The runtime's Ready check and Random stage (`NetplayRuntime__Room.cxx`), the opponent-changed notice (`NetplayRuntime.cxx`), Random fighter (`common/RandomFighter.hxx`, now neutral), and the shell's names and lists: `ApplicationShell.cxx`, `__Settings.cxx` (main-character list in `rosterOrder`), `RoomControls.cxx`, `RoomPanelTables.cxx`, `ReplaysPanel.cxx`, `ReplaySummaryText.hxx` (one `ReplayFighterName`). The room model, its JSON, the preferences, the public rooms card and the session server were already on the catalog (step L).

**The package inventory's selection files.** `PackageInstaller` pulled SF4's roster into every runtime link through `IsSelectionAssetPath`. It is now declared in `common/SelectionAssetPath.hxx` and defined once per game, like the labels: SF4's moved unchanged to `common/Sf4SelectionAssetPath.cxx` (in `sf4e_selection`); the fake game's and SFV's (`SfvSelection.cxx`) allow none.

**Proof.**
- `tests/fake_catalog.{hxx,cxx}`: a catalog with 60 fighters, 20 costumes, 100 colours, 1 Ultra, 40 stages and no Random stage. The fake game defines `Catalog()` from it (`fake_game.cxx`).
- `RuntimeCapabilitiesTest` (x86) and `ember_runtime_x64_test` link it instead of SF4's catalog. Its new case drives the runtime's Ready check: fighter 59 in colour 99 passes the stage, fighter and pick checks (it stops only at the seat); fighter 60, costume 20, colour 100, stage 40 and stage 255 are each refused with that check's words.
- `CatalogRosterTest` uses the same rules at 60 fighters and 100 colours: the room model, its JSON, the room host's status, the protocol's match check and the session server's pick decode take fighter 59 in colour 99 and refuse the values past each limit.
- The x64 test no longer compiles `Sf4Catalog.cxx`, `FighterCatalog.cxx`, `StageCatalog.cxx` or `InstalledContent.cxx`, and nor does `sfve_ember_tests`. The room host's `selection` group gains `InstalledContent.cxx`, which SF4's `Available` calls.

**Still SF4's, on purpose.**
- The type of the Random stage pool (`RandomPool`, `StageCatalog.hxx`), carried by the runtime's room command and passed through to `resolveStage`. SFV ignores it. A neutral pool waits for a second game with a Random stage.
- `InstalledContent`'s definition: the runtime builds an empty one when the game has no custom content, so it includes the header; nothing shared reads its fields.
- `StageValue.hxx`'s `PreferenceStage`, which only SF4's saved preferences use.
- SF4's select screen (`FighterSelector`), Training, art, labels, lobby music and custom-content files, which are SF4's adapter.
- The product identity in the runtime's tests (step 2).


## 12. Slice 1 in the mod (B1, 2026-11-05)

Ember's runtime now runs inside the SFV mod, and a private Ember room plays matches between two SFV copies. There is no shell yet: test commands on the control pipe stand in for its buttons, and the evidence is the mod's logs and screenshots. This does the three "left for the next step" items of section 10.

**How it is hosted** (`-sfve_mode=netplay -sfve_room=1`)
- `src/ember/EmberHost.cxx` registers one `RunEachGameFrame` tick. The first frame the main menu is in front, it runs `FillCatalog(ReadContentLists())` and starts the runtime only if `Usable(Catalog())`. Empty lists make it wait; refused lists stop it, with the reason in the status field `ember`. After the fill, `SealCatalog` makes any later fill log and refuse. The runtime's threads read the catalog without a lock.
- The host then starts the helper the way the runtime expects a launcher to: `HelperProcess::Start(sf4-net.exe)` in a kill-on-close job, then `ConfigureHelper(bootstrap)`. Next come `InitFromPayload`, with a display name of "SFV <instance + 1>" (a room's names must be unique) and editions off, then `StartHelper`, `NotifyRuntimeGameReady` and `NotifyRuntimeEventSystemReady`. The runtime's spdlog lines go to the mod's log as `ember <level>:`.
- From then on, every game-thread frame runs `ember::Tick`, with `NetplayMode` as the `GameTick`.
  - `PollBattle` closes a room battle once the main menu is back.
  - `UpdateGame` is empty: SFV's scene manager ticks its state right after the game-thread pump, outside the tick. So on SFV, `PublishPresentationFrame` runs before that frame's game update, not after it as on SF4.
- At process exit, a static guard runs `ShutdownNetplay(true)`. It is constructed after the battle mode, so it is destroyed first. There is no `StopHelper`: its waits would run under the loader lock (GGPO owns the DLL's `DllMain`), and the job ends the helper with the process. The guard is installed but not exercised: every test ended with `taskkill`, which never reaches DLL detach. (Section 14 removed the guard: a window close leaves the room from the engine's exit request, and was tested.)
- `HelperLink` and `MatchLink` are gone. The room's grant gives the ports, seed, delay and picks. `-sfve_link`, `-sfve_fighter/_stage/_rounds/_time/_after`, the `host`/`matchend` pipe commands and `MatchEndChoice` went with them. `tools/runroom.ps1` replaces `tools/runauto.ps1`.

**The battle** (`NetplayMode`, through `RuntimeEvents`)
- **`NewBattle`.** A battle is a room's when `-sfve_room=1` and `HasRuntimeRoomLink()` holds as it binds. Such a battle:
  - calls `SetBattleInPlay` (session, battle and the exit hook) before the old session can go;
  - calls `ClearBattleState`, `CancelDeferredGgpoClose`, `ResetGgpoBattleWatch` and `NotifyRuntimeBattleLoaded`.
  - Any other battle while rooms are on plays offline.
- **Session start.** The session starts on the first frame that has `SessionSynced()` (the room client's `OnSessionSynced`) and `GetRuntimeMatchEndpoints`:
  - `localSlot` is the player and `remotePorts[1 - slot]` the peer;
  - the seed is `_matchData.rngSeed` and the delay `_matchData.inputDelay[slot]`.
  - The calls go `BindRuntimeInput`, `ReportGgpoTransport`, `ReleaseRuntimePortToGgpo`, `RollbackSession::Start`, then `NoteRuntimeMatchStart`; a failed start calls `PushAlert`.
  - If `IsRuntimeMatchLive()` turns false while the battle waits, the battle leaves with no session.
- **`Frame`.** It reports `NotifyGgpoSyncPhase` (Synchronizing, then Running).
  - On the confirmed frame `TakeEndedVersusBattle` takes the match's end, it calls `NotifyRuntimeMatchResult(GameResult(), frame, confirmed, 0, false)`, then `Leave()`.
  - `RematchInPlaceOffered` is read and logged, but SFV has no prompt over the WIN (`ResultScreenShown` is false), so every game goes back to the room. The test command's rooms set `instantRematch` off. (Superseded by section 19.)
- **Retire, Abort and Fail.** These call the exit hook, which only sets a flag, so no runtime call can re-enter the mode's lock. The next battle frame closes the session and calls `Leave()`. `Abort` also pushes its notice.
- **Close (`PollBattle`, main menu back).** The session closes and `SetBattleInPlay({})` runs. Then the battle reports how it ended:
  - `NotifyMatchEnded` if the match reached its end, otherwise `NotifyRuntimeBattleClosedWithoutSession`;
  - then `FinalizeControlPlaneLossAfterBattle` if `IsControlPlaneLost()`.
  - Only then is `Battle::Live()` false: the runtime's teardown and the next Ready wait for it.

**Input.** `SfvInput::ReadDevices` lists the XInput slots as the game last read them (`VirtualPads::LastPadState`, recorded in the XInput hook, so the listing makes no XInput calls of its own) as "Controller N". The gameplay device is the game's pad 0: `SavedGameplayDevice` and `MenuDevice` name it, and `AssignToSide` accepts only it, for either side. `runtime->input.Ready()` therefore holds once the game reads pad 0, so `AttachRoom` joins and Ready is allowed. The log shows it: `Input: gameplay controller 'Controller 1' ... slot 0 by restore`, then `Input: P1 plays with xinput 0` as the session starts. `GameState::Random` uses the system's generator: the host's match seed is no longer always 0.

**Test commands** (`src/ember/RoomCommands.cxx`, its own module)
- Commands: `room [rounds] [seconds]`, `invite`, `join <invitation>`, `sit [table] [seat]`, `pick CODE[:costume[:colour]] [STAGE]`, `ready`, `delay N` and `leave`.
- Each submits a `RuntimeCommand` tagged with the published session generation. `sit` is the room board's Queue action, fenced by the room and table revisions.
- The status field `room` reports the room and match phase, the seat, the members, `canReady`, the pick and the latest notice.

**Evidence** (`tools/runroom.ps1`, two copies, 1 round of 30 s, RYU vs CNL on S25, input delay 2, seeded mash)
- **0 ms lag, 2 games.**
  - Both copies boot to the main menu and fill a catalog of 46 fighters and 37 stages (build `.../d41890506b8a4a34` on both).
  - The host's room is joined by its invitation. Both copies take P1 and P2 and Ready.
  - The match launches through `VersusLaunch` on both, each session starts after "every member's battle has loaded", and both log `match result ... at frame 4586`.
  - The room records the result (`Match result: report acknowledged`, `Room: match ended table=0 generation=1 result=1`). Both copies log "battle closed at its match's end; back in the room", and the second game runs the same way.
  - 154 confirmed checkpoints were compared across the copies' logs, with 0 differing and no desync notice. Screenshots of each fight and of each copy back at the main menu were checked. Two from the 30 ms run are kept, `D:\sfv-re\shots\room\game2-fight-1.png` (the fight, 26 s left) and `game2-room-0.png` (the main menu after it); the others were deleted.
- **30 ms lag (`-sfve_lag=30`), 2 games:** PASS twice.
  - With 1 round of 30 s: 154 checkpoints compared, 0 differing; both copies decide at frame 4586.
  - With 3 rounds of 60 s (`-rounds 3 -seconds 60`): both games decided at frame 10487 on both copies (2-0, time over), 350 checkpoints compared, 0 differing, no desync notice.
  - Rollbacks per game, from the battle status as last published: P1 888 and 917, P2 0 and 1, longest 1 frame. The rollbacks fall almost all on P1. The pacing runs P2 behind and was not examined further; the earlier `runauto.ps1` figure (665) was a different flow and is not comparable.

**Left for B1's follow-ups**
- The result goes to the room, and the room accepts it (`profile record saved`). Sets, rotation and rankings were not exercised beyond one Unlimited table.
- Rematch in place needs a WIN prompt. `MatchEnd::Rematch` is kept for it. A rematch would also need the session's restart without a second battle-loaded sync. (Done, section 19: the session is not restarted at all.)
- `GamesPlayedInPlace`, `RecentRollbackFrames`, `PingSummary`, the disconnect countdown, the connection warning and the prediction stall are still stubbed.
- Settings are written nowhere. Copies on one PC share `%APPDATA%\SFVEmber` (settings and the session trace); no conflict was logged in these runs.
- In every run, each helper logged "actor stuck in task:hosted/guest_control" for about 3 s while the room opened. The room then worked; this was not looked into.
- `GameState::Language` is empty.

**What blocks B2 (Ember's shell drawn on DX11)**
1. **No neutral `ShellHost`** (section 6, Wrong #6). SF4's `sf4e__Overlay.cxx` turns the snapshot into a `ShellView`, keeps the player's pick and Training dummy, and draws the fighter selector. SFV needs that host, minus SF4's selector, plugged into `SetOverlayUi` (`src/overlay/Overlay.hxx`), plus a `DrawSelection` over `ReadContentLists()`. Today the pick lives in `RoomCommands`.
2. **The shell's pad input.** `SfvInput::ReadController` and `MenuButtons` return nothing, and `MenuTakesPads` is false. The overlay's `FilterGamePad` already takes the pad from the game. The shell's navigation needs `ReadController` from `LastPadState` (XInput bits to Ember's `input::xinput` layout).
3. **Textures.** The shell's `SelectionArt` needs a DX11 `TextureUploader` (overlay plan section 1d). SFV's pictures all report missing until there is art.
4. **The shell in the mod's link.** `ApplicationShell` compiles in the x64 probe (section 7), but the mod has never linked it: fonts, locales and ImGui 1.91.9 (the pin, section 6 Wrong #11) are untested in the DLL.
5. **Rules SFV cannot play.** The shell offers 15 and 99 rounds and 9999 s; SFV plays 1/3/5/7 rounds of 30-300 s. `Battle::Enter` rejects such a match (`Entry::Rejected`). The shell should offer only the catalog's playable rules: add rule lists to the catalog (section 2, `RoundCounts`/`RoundTimes`).

## 13. B2: the shell in SFV (2026-11-05)

Ember's shell (the library's `ApplicationShell`, through its neutral `ember::ShellHost`) now draws over SFV through the mod's Direct3D 11 overlay, branded SFV Ember. A pad opens it at the main menu, and from it a player creates a private room, copies its invitation, joins one from the clipboard, picks, and Readies. The room test commands (section 12) stay for `tools/runroom.ps1`.

**The five blockers of section 12, as solved**
1. **ShellHost.** `sf4e__ShellHost` was already neutral and already in the runtime's sources; what SFV lacked was its side. `src/ember/SfvShell.cxx` is SFV's `ShellGame` and frame driver: `FillView`, `StepSelection` (colour), `AddPick`, and `DrawSelection`. It uses the library's own `OverlayPresentation`, `MenuPads` and `ControllerNavigation`, as `sf4e__Overlay.cxx` does. The pick moved out of `RoomCommands` into `src/ember/SfvPick` (one store, any thread); the pipe's `pick`/`ready` and the shell both use it. `DrawSelection` is `SfvSelectionScreen`: GameMenu value rows (fighter and stage with all choices on Select, costume and colour stepped by Left and Right, Done), named by the game's codes. It follows `EmbeddedReturn` as `FighterSelector` does; FighterSelector is not ported.
2. **Pad input.** `SfvInput::ReadController` reads `VirtualPads::LastPadState`, which is recorded before the overlay hides the pad from the game. The D-pad (and the left stick, now recorded in `PadSeen::stick`) gives the directions. The buttons go through the library's new `input::xinput::Physical` (common/PadKind.hxx), which the UI lab now uses too. `MenuTakesPads` reports `MenuPads::Takes`. Start on the gameplay pad opens Ember (`input::StartPress`, game thread); F10 toggles it. A virtual pad (`pad 0 ...`) drives the shell through exactly the path a physical pad takes.
3. **Textures.** `src/overlay/TextureUploaderD3D11` makes immutable BGRA textures and hands their SRVs to ImGui. The overlay owns `SelectionArt` on the game's device. The build copies Ember's own `assets/brand`, `assets/input` and `assets/flags` next to the mod, and `sfve.py deploy` deploys them. SFV's fighter and stage pictures still report missing.
4. **The shell in the DLL.** It links: ImGui 1.91.9 and the library's patched Win32 backend (input bridge, render size; the probe now builds it with `IMGUI_IMPL_WIN32_DISABLE_GAMEPAD`, as SF4 does), the embedded fonts and the locales. The overlay was reworked:
   - An interface set means every Present is an ImGui frame. The shell does its hidden work too (`ShellHost::Background`), and returns whether it is shown.
   - Window input goes through the library's `Win32InputBridge` and `HandleOverlayMessage` under `OverlayLifecycle`, in place of the mod's own queue.
   - The View toggle and ImGui's own gamepad navigation are gone. View is the shell's Chat button.
5. **Playable rules.** `GameCatalog` gained `roundCounts`/`roundTimes` (and `PlayableRoundCount/Time`), `editions` and `rematchInPlace`. SF4's catalog lists every rule, all true. SFV's lists are the room's rules that `PlayableRounds`/`RoundClockFrames` accept: 1/3/5/7 rounds of 30/60/99/300 s, no editions, no rematch in place. The shell offers only those. Edition and Instant rematch rows are hidden. A Practice room (9999 s) is offered only when the catalog plays that time. A room the shell opens has Instant rematch off.

**Other library changes (SF4 unchanged; `game_catalog_test` pins SF4's lists):**
- `ShellView::offersTraining/offersReplays` (from the adapter's capabilities) hide Home's Training and Replays rows and the room's Training row.
- The table page has no Ultra row when the catalog offers one Ultra. It has no fighter options row when the game says its fighters have none (`ShellView::fighterOptions`).
- A seat caption has no Ultra for such a game.

**SFV Ember's words.** `SfvProduct` now has an English locale overlay for the shared strings that named SF4 Ember on the screens SFV shows: header subtitle, Home title, "Return to SFV", About, the shortcut hint and the build checks. English replaces each translation of those (Localization.cxx).

**Native menu lock** (section 12 review item). While a room is joined or a game is on its way, the shell keeps the pads from SFV's main menu whether it is shown or not (`NativeMenuLockFor`), so the only way on is Ember's. In a room, Back on Home does not hide the shell (the library's rule).

**Evidence** (`D:\sfv-re\shots\shell\`)
- `1-menu.png`: Home over SFV.
- `2-rules-rounds.png`: Create room, Rounds offering 1/3/5/7.
- `3-room-invitation.png`: the room, "Invitation copied".
- `4-pick.png`: the pick screen.
- `5-back-in-room-game1*.png`: both copies back in the room after the match.
- `tools/runshell.py` drives both copies with their virtual pads only; the pipe only reads status and presses buttons. Copy 0 opens Ember with Start, skips the first-run welcome, creates a room of 1 round of 30 s and copies its invitation. Copy 1 pastes it on Join. Both take a seat from the table card, pick (X), and Ready on their seat. The match launches as in B1, and the shell comes back over the room afterwards ("a match ended").
- `tools/runroom.ps1` still PASSes (the room commands, shell present but hidden).

**The frame-0 desync after pad presses at the main menu (resolved 2026-11-05).**
- Found: shell-driven two-copy runs passed 3 of 9 attempts, each failure at the first checkpoint, with only `battleframe` differing: its last dword, +0x1BC, 1.0f on one copy and 0.0 on the other. Room commands plus ten DOWN presses per copy desynced 2 of 3; without presses, runroom passed.
- **Cause: +0x1BC is not a field.** It is the 4-byte alignment gap between the tick counter (+0x1B8, a dword) and the TArray at +0x1C0 in the 0x1D0-byte battle object (SFV_REGION_MAP.md section 0).
  - The constructor (rva 0x4C5C40) and the reset (0x4D8830) write +0x1B8 as a dword and +0x1C0 as a qword. No battle code reads the gap: every `+1BCh` access in rva 0x2E0000..0x520000 is on another object (the two near it, 0x4DBA8D and 0x4DD719, on a unit's transform). Nothing in the battle writes it either, or both copies would hold the same value.
  - The object is allocated by FMemory::Malloc directly (0x4F4788 calls 0x11370F0), not through GameAlloc, so the mod's zeroing never covered it, and the gap held whatever the heap left. Pad activity at the menu changes the heap's history, so one copy got a block with 1.0f there.
- **Fix: the `battleframe` region ends at +0x1BB** (`battleframe::kStateSize` 0x2C; it was the mod's own region, guessed through +0x1BF since the first commit). This corrects the region's bounds to the object's fields; it is not a peer mask: nothing the fight reads left the hash, and every field of the region is still saved, restored and hashed as before.
- **Evidence** (after the library merged SF4 1.5.3; 1 round of 30 s, 0 ms):
  - Press repro, `tools/runroom.ps1 -presses 10 -games 1` (the new `-presses` option): 5 of 5 PASS, 77 checkpoints compared each, 0 differing.
  - `tools/runshell.py`: 3 of 3 PASS, 77 checkpoints on each copy with every hash equal (frame 0 included), no desync line. `6-back-in-room-after-fix.png` shows both copies back in the room, 1 - 0.
  - `tools/runroom.ps1` (no presses, 2 games): PASS, 154 checkpoints compared, 0 differing.
  - `tools/synctest.ps1` (the layout changed; RYU vs KEN, random schedule 1, depth up to 8, learning): 7,073 frames, 1,220 rollbacks, 0 failures.
- **The test tools after 1.5.3.** A seat whose fighter changes now takes back the other seat's Ready, and a Ready carries its pick, so the second copy's first Ready took back the first's and no match started. `runroom.ps1` and `runshell.py` Ready whichever copy can until both sessions start. The room board's bar (copy, copy-short, room-more, leave) sits below the tables, so `runshell.py` reaches Copy by DOWN to room-more and then LEFT.
- **Library changes the merge needed on SFV's side:** `EmbeddedReturn::openOn` is a `SelectionStart`, and the selection seam gained `UltraMove` and `SelectionArt::UltraIcon` (SFV: empty and missing).

**Left**
- `room.rules_summary` still says "Edition Select Off": it needs a new msgid without the edition, across the locales. (Done, section 14.)
- Settings are saved nowhere (section 10), so the first-run welcome shows on every launch; `runshell.py` skips it. (Done, section 14.)
- Only F10 and Start open the shell. Windows' DPI scale is not applied (the picture's height alone sets the scale).
- At 960x540 the room page is cramped and its spatial navigation awkward.
- Public rooms, Ember ID and rankings show but need the bridge, which SFV Ember does not have yet.
- The clipboard join replaces what the Windows clipboard held.
- SFV has no fighter or stage art, and no names table (codes are shown). (Stages are named since section 14; fighters, costumes and pictures since section 15.)

## 14. Fix pass (2026-11-06)

The review of fa93655..e507998, what the shell showed over SFV (section 13's screenshots), and two library nits. SF4's behaviour and wire bytes are unchanged: no wire fixture changed; SF4's visible text changed only where it was wrong for SF4 too (below).

**The battle and the runtime (sfv-ember)**
- **A battle is the room's only when the room launched it.** `MatchPicks::linked` marks the room's launch (`SfvBattle::Enter`); VersusLaunch keeps the mark from the jump's second step until the battle binds (`TakeLinkedBattle`), and drops it when the load fails. A native battle (SFV's menus, the pipe's `launch`) while joined plays offline and the player stays in the room; a room battle whose room closed during the load still leaves as the room's (`!IsRuntimeMatchLive` → Gone).
- **`src/ember/RoomBattle`** owns that mark and everything the room hears of its battle: bind and loaded, the session's room config (with a null check of `RuntimeSession::netplay` and SF4's `InputDelayWireCeiling` check of the granted delay), the sync phase, the result on its confirmed frame, and the close. NetplayMode keeps the battle and its session and asks it; `frames_`, `wasLive_` and `startFailed_` are gone.
- **An undecided match end** (`MatchResult::decided` false) is logged and dropped on the confirmed frame both copies take it on, and the battle steps on; no Abort is reported for it.
- **A leave the scene manager cannot take** is asked again each frame (`leaveWanted_`/`leaving_`).
- **A failed launch** also calls `NotifyRuntimeBattleClosedWithoutSession`, so the room ends the game (the runtime's sessionless close: abort) instead of the peer waiting at the session start.
- **The mode installs its own host.** `BattleMode::Install` (called by the driver once it holds the mode): NetplayMode registers its commands and, with `-sfve_room=1`, the room commands, `EmberHost` and the shell. `-sfve_room=1` with another mode is turned off and logged (`IgnoredSetting`). dllmain no longer branches on rooms.

**A normal close (A2).** The DLL-detach guard is gone. `mod/GameExit` hooks the engine's exit request, `RequestExit(bool force)` at rva 0x11D0580: with force it terminates the process; without, it posts the quit message and sets the requesting-exit flag. A window close reaches it on the game thread through the EXIT console command ("Closing by request", rva 0x1ACAB20), as do some 80 other callers. In the hook, on the game thread, EmberHost's exit work runs before the engine's exit goes on:
1. it submits Leave room, then ticks the runtime itself about once a frame until the room is Idle or 2.5 s pass (the game's frames do not run once its window is closed: a first version that held the request and waited for them never exited);
2. then, as SF4's `fMain::Destroy`: `ShutdownNetplay(true)`, `StopHelper`, and the settings' writer stops after writing what is queued; the host's tick stops.

A request from another thread runs nothing. Evidence (`tools/runclose.py`, PASS): the host, with its room open, closed by WM_CLOSE (what `Process.CloseMainWindow` sends) exited in 2.5 s, the graceful leave done in 609 ms, its `sf4-net.exe` gone; the guest saw the room go on with 1 member and no "Room control is recovering" (with only `ShutdownNetplay`+`StopHelper`, as the guard and SF4 do, the guest saw the host lost and recovered for over 30 s). The guest, closed from its own battle, exited in 2.3 s; no helper was left. Section 12's "a normal close is untested" no longer holds.

**The guest's clean leave was intermittent (A2, follow-up).** Later runs of `runclose.py` failed about one in two: the guest showed "Room control is recovering" although the host had left gracefully. The exit was not the cause. A helper trace showed the host's helper hand the room off before it reported the room closed (successor promoted, elected at term 2 and its claim confirmed, in 2 ms), and in a failing run the guest raised the notice before the host's helper stopped; the 156 ms against 609 ms exits were `StopHelper`'s wait for the helper to disconnect, not the leave. The cause was on the successor:
- A new term's `coordination_state` clears the native room's `rebound`, and the room reads as lost (`writable && rebound` false) until that term's `control_rebound`; the old leader's `control_closed` degrades it as well while the old leader is still the leader it knows.
- The helper sent `coordination_state` and `control_rebound` as bulk events, dropped when its IPC queue has no headroom and repeated only at the next refresh, while `control_closed` is a lifecycle event, never dropped and sent ahead of bulk ones.
- The host's Leave commits a 41 KB checkpoint (its removal) just before the handoff, and the guest's queue was full of its chunks: the term-2 `control_rebound` was dropped, or the term-2 `coordination_state` came after the old leader's close, or the pair fell across the native poll's 8-event budget. The runtime ticked once with the room lost and raised the notice.

The fix is in the library and game-neutral. The helper sends a term's first `coordination_state` and first `control_rebound` as lifecycle events (`Actor::rebound_term`; the per-second repeats stay bulk), and `IrohRoom::Poll` reads past its 8-event budget, to 16, while a writable term waits for its rebound, so the pair lands in one poll. The exit order is unchanged; `StopHelper` is unchanged. SF4 changes the same way, deliberately: a host leaving by Leave room had the same race on its guest. No wire fixture changed (the IPC events are the same; only their delivery class changed). The log now gives `abandon`, `leave_acked`, `writable` and `leader_local` on "Room: closing", and logs the room's coordination term and writability changes, the leader's control closing and the helper's `room_closed`.

Evidence (library 3a8ad6735, helper built from it): before the fix, 2 of 5 runs failed (exits 156 to 172 ms). With it, 15 of 16 runs passed (exits 156 to 172 ms, one 594 ms), the last 8 in a row. `runroom.ps1 -presses 10 -games 2 -lag 30` PASS (154 checkpoints compared, 0 differing), `runshell.py` PASS, sfv-ember's 4 ctest suites pass; the library's x86 build, its targeted ctest (Session, Iroh, Room, Helper, Runtime: 46 of 46), `probe-x64.ps1` and sf4-net's `cargo test` (476 passed) pass. The one failure was a different, slower handoff: the host's helper spent over 2.4 s in the leave and the guest's new term was not writable for 1.6 s, so the exit ran out of its 2.5 s cap and the host took 5.7 s to close. That is left open. Also left: while the helper's IPC queue has no headroom, its coordination refresh retries about every half millisecond.

**The A1 test** (`runclose.py`, the same run): joined copy 1 launched a versus battle with the pipe's `launch`; it ran (frames 630 → 930), its battle status showed `room: false` and no session, no "room's battle has loaded" was logged, and copy 1 stayed joined with 2 members. `runclose.py` takes `7-own-battle-in-room.png` only with `--shots`; it is not in the set.

**What the shell shows**
- **Version (B1).** sfv-ember's CMake names its version (`0.1.0-beta.1`, `SFVE_VERSION`, in the log's first line) and sets `EMBER_APP_VERSION` before it adds the library's x64 build, which now takes that variable (`0.0.0-x64-probe` when the probe builds alone). SfvProduct's overlay gives `about.version_line` as "SFV Ember {0} · {1}": "SFV Ember 0.1.0-beta.1 · Beta".
- **Names (B2).** Stages: SFV's `GetStageName` native (`KWStageEntryDataAsset`'s, thunk rva 0xD18B70) is rva 0x564550, `FString* (StageAsset*, FString* out, const FString* code)`: it finds the entry by code, finds or loads the stage's data asset (the entry's +32 path, rva 0x55E550) and copies its FText (+40) as shown. The favourite-stage screen calls it so (rva 0x812F70) with the stage asset from the game data (`content::kStageAsset`). `GameContent` reads one name per stage once both lists are there (`ContentLists::stageNames`, UTF-8, in the game's language): all 37 stages named, read in under 1 ms. The shell shows `StageName` (the code where the game gives none). The names never enter the build's fingerprint. Fighters: no reader found. There is no `GetCharaName` native; the fighter's name is presumably an FText in its `KWCharaPersonalDataAsset` (`/Game/Chara/RYU/DA_RYU_Personal`, from the embedded `DA_CharaEntry`), but no native reads it, and finding that class's property offset was not done. `CharaName` (0x140DE5320) is a field of `KWVersionInfo`, not of the fighter. Fighters stay codes.
- **Hint bar (B3).** Not SFV's: SF4's room at 960x540 drops its labels too (UiLab). `V2::HintBar` measured each hint with a label gap even when it had no label (LB beside RB), and the status on one line although it wraps onto two; it was 6 px over. It now measures as it draws, with the status at its narrowest two-line break: the room's footer has its labels at 960x540 in both games.
- **"Slot 1" (B4): not changed, a wording question.** `{1}` of `room.board_status_slot` is the member's table (`m.table + 1`), so both members at table 1 read "Slot 1" correctly; "slot" is Ember's older word for a table (chat's "won a game at slot {1}"). The board says "TABLE 1", and several translations read the word as a seat (ru "Место", ja "席", es-ES "Asiento", it "Posto", fr "Place"). Renaming it to "Table" across the catalogs is the maintainer's call, with the translators.
- **Ultra in sentences (B5).** With one Ultra choice (`Catalog().ultraChoices`), Ready says `room.ready.fighter` / `room.ready.fighter_against` ("Ready with KEN."), and a replay's look has no Ultra.
- **Edition (B6).** With no editions (`Catalog().editions`), the rules line uses `room.rules_summary_no_edition` / `_practice_no_edition`.
- **"1 rounds" (B7).** The catalogs have no plural mechanism; their README says to phrase counts neutrally (ru, pl and cs already did). en, de, fr, es-ES, es-419, nl and pt-BR now read "Rounds 1 / 30 sec" (and "Rounds 3, 99 s, ..."); pl's and cs's Practice strings likewise. This changes SF4's footer and rules line in those languages too: "1 rounds" was wrong there as well. The new msgids are in all 14 catalogs, translated by dropping the clause from each locale's existing string.
- **Settings (B8).** `ProductIdentity::settingsBesideModule`: SFV Ember's settings folder is `SFVEmber` beside the mod (`SettingsStore::DefaultDirectory`), where its logs are; SF4's stays under %APPDATA%. `SfvPreferences` queues to the library's `SettingsWriter` there, as SF4's do, through a neutral `netplay/PlayerPreferencesJson.hxx` that SF4's `OverlayPrefs` now uses too (the same JSON). The writer starts with the first save and stops at the game's exit. The runtime already read that file at start. Evidence: `settings.json` appears beside the logs, and a later launch shows the record (2 W / 0 L) and no first-run welcome. Copies on one PC share the file; its lock serialises them and the last save wins.

**Library nits.** `ember::FighterNameOr` (GameCatalog.hxx) at its three sites; `SelectionArt::UltraIcon`'s comment is game-neutral, with SF4's archive files described at SF4's definition.

**Evidence**
- Library: x86 build; targeted ctest (RoomPrivateGolden, Shell*, UiRender*, RuntimeCapabilities, Localization and others) 13 of 13; full ctest (without HelperRust, EmberRust, UpgradeRecoveryPowerShell, PackageFiles) 230 of 230 (SpeechWine skipped); `scripts/probe-x64.ps1` passes. `scripts/subset-cjk-fonts.py` could not run (the Noto CJK sources are not on this PC); the new ja/ko/zh-Hans strings use only characters the catalogs already had, and the Localization test checks the subsets.
- sfv-ember: `scripts/build.ps1`, 4 of 4 ctest suites pass; `tools/runroom.ps1 -presses 10 -games 2 -lag 30` PASS (154 checkpoints compared, 0 differing); `tools/runshell.py` PASS; `tools/runclose.py` PASS.
- Screenshots (`D:\sfv-re\shots\shell\`): `1-menu.png` (the version line), `2-rules-rounds.png`, `3-room-invitation.png`, `4-pick.png` (stage names), `5-room-seated.png` (hint labels; both members "Slot 1"), `6-table-rules.png` (Ready with KEN.; "Rounds 1, 30 s, own delay"), `8-back-in-room-game1.png`.

**Left**
- Fighter names (above; done, section 15), and the header's "EMBER" title on Home.
- "Slot" in the members list (B4, above).

## 15. Picks and pictures from the game's own data (2026-11-06)

SFV Ember's catalog, its pick screen and its pictures now come from SFV's own tables and textures, read at run time from the player's install. Every address below is from our IDA database of build 13173604 and carries a signature that BuildCheck verifies (`SfvOffsets.hxx`). No library change was needed: the per-fighter and per-costume rules live in SFV's `GameCatalog` callbacks, the stepping in SFV's own pick screen, and the pictures in SFV's definitions of `SelectionArt`'s game members. SF4's behaviour and wire bytes are untouched.

**The tables**
- **Fighter table: `DA_CharaEntry`**, embedded in the exe as text and parsed at boot (rva 0x56D050) into the game singleton's `CharaAsset` (+96): 232-byte entries with +0 `CharaCode`, +16 `CharaProgID`, +20 `CharaUniqueID` (the spec's key), +24 the loaded `PersonalData` object (`KWCharaPersonalDataAsset`), +80 `CostumeDatas` (a TArray of package paths, indexed by costume number), +112 `CostumeColorPreviewDatas` (one name per costume number, e.g. "RYU01", "Z0006").
- **Spec: `DA_Spec`** (`/Game/Spec/DA_Spec`, loaded at boot, rva 0x691790; the singleton's "SpecDataAsset"). At +328 a TMap keyed by `CharaUniqueID` (136-byte elements, value at +8). A fighter's value holds at +24 a TMap of costumes (120-byte elements, the costume's value at +8); a costume's value holds at +24 a TMap of colours (40-byte elements). Byte flags decide what is listed:
  - fighter value +2: selectable (rva 0x566AF0); +4: needs no package check (rva 0x5591E0);
  - costume value +0: enabled (rva 0x54F1F0); +3: needs no package check (rva 0x558DC0); +8: a UObject whose FText at +64 is the costume's name (rva 0x55C5B0);
  - colour value +0: enabled (rva 0x54EE30); +1, or a number under 128: listed (rva 0x558920).
- **The game's listers**, each `bool (SpecData** spec, ..., TArray<int>* out)`, walk those maps in their own order. GameContent calls them:

| Lister | rva | What it lists | Install? |
|---|---|---|---|
| `kSpecFighters` | 0x566AF0 | the spec's selectable fighters' ids | no |
| `kSelectableFighters` | 0x566FC0 | those whose package exists or need none, as codes, less Random | **per copy** (FighterInstalled, rva 0x549180) |
| `kSpecCostumes` | 0x54F1F0 | a fighter's enabled costumes | no |
| `kPlayableCostumes` | 0x54FAB0 | those this copy can load (flag +3, or the CostumeDatas package exists, rva 0x548E40) | **per copy** |
| `kSpecColours` | 0x54F870 | a costume's colours | no |

- **Colour count is per costume**, as the task guessed. Costume numbers have gaps (RYU: 0-3, 5, 6, 9-13), and colours run 0-99 and then 100 and up for EX colours (RYU's default costume: 0-14, 100, 108). The game's labels are "%02d" of the number plus one and "EX%02d" from 100 (rva 0x553B80).
- **The game's own checks** at launch are membership in those lists: `kCostumeValid` (rva 0x56B190) asks `kPlayableCostumes`, `kColourValid` (0x56B0E0) asks `kSpecColours`; both let the Random fighter through (rva 0x56B6C0).
- **Ownership** is a third, separate filter: the costume menu lists `kPlayableCostumes` less what the player's entitlements lack (rva 0x5545B0 with 0x55AEF0, whose +96 turns the check on). Ember leaves it to the game's own check at launch (VersusLaunch's `checkOwnership`).
- **Names.**
  - A fighter's: its PersonalData's FText at +80, through `FText::ToString` (rva 0x116E270), as the versus screen's "charaName" (rva 0x6DD620). The game gives them in capitals ("CHUN-LI", "M. BISON").
  - A costume's: rva 0x557870, the spec's FText or "Costume<n>" ("DEFAULT", "Story", "Battle Outfit 1", "Mega Man").
  - A stage's: section 14.

On this PC the spec lists 46 selectable fighters (ELEVEN with one costume), 5 to 19 costumes each, and nothing the install lacks.

**How the native screens choose and apply them.** The costume settings menu (rva 0x8CC6E0) builds its spin boxes from these lists: case 3 lists the fighter's costumes (rva 0x5545B0) and names each (0x557870); case 4 lists the costume's colours (0x5539D0) with each one's label (0x553B80) and two swatch colours (0x554750). The battle takes a fighter code, a costume number and a colour number in its CharPkg, which VersusLaunch writes through the mode data's setters (sections 10 and 12).

**What Ember does with them** (`mod/GameContent`, `ember/SfvCatalog`)
- **The pick space** (the same on every copy of the same game data): the spec's selectable fighters in the game's order, each fighter's spec costumes, each costume's spec colours. A pick carries the game's own costume and colour numbers.
- **A fighter with no battle setup is left out:** one whose `DA_CharaEntry` record has no `SetupData` (entry +48). That is Random and ELEVEN. ELEVEN's record has only `PreviewSetupData`, with no setup and no BattleResourceList, because the select screen decides which fighter Eleven plays as (`bIsDecidedCharaEleven_Left/Right`). A launch with ELV hung at a black screen on both copies (the room logged "load over 60 s"). The rule reads the exe's own table, so every copy leaves out the same fighters: 45 remain on this PC.
- **`ValidPick`** takes a costume of the fighter and a colour of that costume. **`NormalizePick`** fits a pick to the fighter's first costume, then to that costume's first colour (the game's first, not number 0). The pick store (`SfvPick`) fits every pick it keeps, so the shell and the pipe's `pick` fit alike.
- **Per copy:** a fighter this copy has not installed is locked (`FighterLocked`), and a costume it cannot load is not available (`AvailablePick`). The pick screen does not step onto either.
- **Limits:** `costumeLimit` and `colorLimit` are one past the highest number in the data. `costumeCount` is one past a fighter's highest costume: the shared contract says "numbered from 0", which SFV's numbers are not, and its one reader (a replay's "look unknown") only needs every spec costume to read as known.
- **The fingerprint** in the build id now hashes the whole pick space (codes, costume numbers, colour numbers; not names or installs), and the match version is 0.2: copies whose spec differs fail the build check.
- **The pick screen** steps through the fighters this copy can play, the fighter's installed costumes and the costume's colours, and names them as the game does. The pipe's setup takes colour numbers up to 255.

**Pictures that exist** (format and size read from the game's own Direct3D textures, all fully resident with one mip)

| Picture | Path | Size, format |
|---|---|---|
| Select-grid face | `/Game/Chara/<code>/Hud/CharaSelectIconLeft` (and `Right`) | 116x144, B8G8R8A8 |
| Fighter banner | `/Game/Chara/<code>/Hud/CharaSelectIcon_l` | 380x258, B8G8R8A8 |
| Fighter strip | `/Game/Chara/<code>/Hud/CharaSelectIcon_s` | 164x52, B8G8R8A8 |
| HUD face | `/Game/Chara/<code>/Hud/FaceIconLeft` (and `Right`) | 256x256, B8G8R8A8; drawn as is it is a blue silhouette, the same shape for RYU, KEN and CHUN-LI, so Ember does not use it |
| Stage picture | `/Game/Stage/<code>/UI/StageSelectBG` | 2048x512, BC1 |
| Stage strip | `/Game/Stage/<code>/UI/StageSelectIcon` | 42x242, B8G8R8A8 |
| Shop item | `/Game/WinSysAsset/Shop/ShopItemImage/Costume/shp_<code>_<nn>_<mm>` (264), `.../Color/...` (963) | 420x190, B8G8R8A8 |

- Each fighter's `Hud` folder also holds the result, list, replay, story, arcade, team and command-list icons; the select-screen resource list builds those paths (rva 0x599860, 0x7654D0).
- **The game has no picture of a fighter in a given costume or colour.** Its select screens show the live 3D model (`PreviewSetupData`, `PreviewCostumeDatas`). A colour is shown as two colours from the costume's `DA_<x>_CostumeColorPreview_<nn>` (rva 0x554750; its palette at +40, and +56 for EX colours, rva 0x553C70).
- **The shop images are per shop item, not per costume.** They exist only for costumes and colours sold in the shop, and their numbers are not costume numbers: RYU has shop costume pictures numbered 01, 02, 03, 05, 06, 07, 12 and 16_01, while his spec costumes are 0-3, 5, 6 and 9-13 (Halloween, Mega Man and others have none). The item-to-costume link is in the spec's shop DataTable (spec+64; a row's +16 is its type, +104 its picture's name; rva 0x5636C0), whose row layout was not reversed. They are not shown.
- (The paths come from the exe's strings and the paks' indexes. Only the index of each pak was read; no asset was read or extracted.)

**How Ember loads and draws them** (`mod/GameArt`, `ember/SfvArt`)
- **Loading.** On the game thread at the main menu only (never during a battle), a few a frame, a screen's request loads the texture: `StaticLoadObject` (rva 0x1242910) with UTexture2D's class (rva 0x1F3CF10). The game makes its render resource a frame or two later.
- **Reading the texture.** The game's own Direct3D 11 texture is reached as UTexture+0x40 (its FTextureResource), then +0x30 (TextureRHI, after FRenderResource's 48 bytes; rva 0x155A4C0 initialises them), then +0x68, the ID3D11Texture2D. A material's texture parameter is set through the first two (rva 0x17397F0).
  - The +0x68 slot was found at run time, not read from code. So every texture is checked before use:
    - the resource's owner (+0x68) must be the texture;
    - the pointer's vtable must be d3d11.dll's texture class (from a texture made on a device of Ember's own);
    - its size must be the texture's platform data size (UTexture2D+0xC0, GetSizeX rva 0x1CB9410).
  - That last check also proves the whole top mip is resident, not a streamed smaller one.
  - Ember then takes its own reference to the Direct3D texture and never touches the UObject again, so the game's garbage collector cannot pull it from under ImGui.
- **Drawing.** On the render thread, Ember makes its own view of the texture on the game's device, with the typeless format viewed as UNORM (the stored values, as the game's menus show them), and hands that view to ImGui. Block-compressed textures (BC1 stages) are drawn as they are; nothing is decoded and no pixel is copied. Views and references are kept for the session.
- **Where they show.**
  - `SelectionArt::Portrait` is the fighter's select-grid face, on every shared screen. The room's seats and members ask for `Character` first and fall back to it.
  - `SelectionArt::Stage` is the stage's picture.
  - `Character`, `Appearance`, `Ultra` and `RankingEmblem` report missing: the game has none.
  - The pick screen's pane shows the fighter's banner, its name, the costume's name and the colour's label with its two swatch colours, and the stage's picture while the stage row has the focus.

**Evidence** (sfv-ember `312fbba` and the portrait change on top of it; library `8a7314d7e`, unchanged)
- `scripts/build.ps1`: 4 of 4 ctest suites pass. `sfve_ember_tests` covers the new rules: gaps in costume numbers, EX colours, a fighter's first costume and a costume's first colour that are not number 0, fitting in costume-then-colour order, installs per copy, and a fingerprint that moves with every costume or colour number but not with names or installs.
- Both copies' logs: "catalog of 45 fighters and 37 stages", the same build id on both (`sfv-ember/13173604/0.2/8dafe602f7615d90`). The game's textures loaded as `CharaSelectIconLeft: 116x144, format 90` and `CharaSelectIcon_l: 380x258, format 90` (DXGI B8G8R8A8_TYPELESS).
- `tools/runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, 154 checkpoints compared, 0 differing.
- `tools/runshell.py --shots D:\sfv-re\shots\shell`: PASS.
  - `4-pick.png`: the pick screen with CHUN-LI's banner, her name, "DEFAULT" and "Color 02" with its swatch.
  - `5-room-seated.png`: the room's seats and members with the game's own faces, RYU for the unshared seat (fighter 0) and KEN.
- **The swatch's channel order** (`10-pick-ryu-colour01.png`): RYU's DEFAULT colour 01 shows his gi's tan and his headband's red, which swapped channels would draw blue. The palette entries are 8-byte pairs of FColor (a dword 0xAARRGGBB), and rva 0x553C70 keeps the low 24 bits, 0xRRGGBB, as Ember reads them.
- **Two copies fit out-of-range picks the same way.** Two games on a fresh pair, each copy's pick sent through the pipe (`pick CODE:costume:colour`, fitted by the pick store):
  - game 1: copy 0 asked for LUKE (5 costumes) at costume 40, colour 120; copy 1 for RYU's Halloween (colours 0-9) at colour 14;
  - game 2: the same picks swapped between the copies, so each copy fitted each kind.
  - Every reply read "(fitted)", with the same result on both copies: LUKE costume 0 colour 0, RYU costume 3 colour 0.
  - Both copies logged the same launch, "player 1 ... -> Z42 costume 0 colour 0", "player 2 ... -> RYU costume 3 colour 0" (swapped in game 2), loaded the same battle and decided both matches at frame 4586 with the same result. No desync was logged.
  - `9-fitted-game1-copy0.png` and `-copy1.png` show the same intro on both copies, LUKE in his default costume (the intro shows only him).

**Left**
- **Costume and colour pictures**: the shop images (above) need the shop DataTable's row layout to map them to costumes. Until then a costume has its name and a colour its swatch.
- **A new battle at the old battle's address is not seen as new.** `SfvAdapter::SeeBattleFrame` tells a new battle by the battle frame's address. In one run the second game's battle frame on one copy landed at the first's address: that copy logged "the flow's state list changed" and never "new battle frame". Its battle never bound to the room, and the peer waited ("load over 60 s"). This is independent of picks and predates section 15; a fresh pair played both games. A new battle should be recognised by its construction (rva 0x4C5C40) or its reset (0x4D8830), not by its address. (Fixed, section 16.)
- Ownership is still the game's own check at launch: Ember's `FighterAvailability` reports everything owned, so an unowned look is replaced by the game as the battle loads rather than refused at Ready.

## 16. A battle is its frame's construction, not its address (2026-11-06)

Section 15's open bug: one copy's second battle never bound and its peer waited ("load over 60 s"). The run's logs were overwritten by later runs, so the cause is confirmed from the code and section 15's record of that run (the copy logged "the flow's state list changed" and no "new battle frame"):
- The battle frame's owner (a virtual at rva 0x4F4690) allocates the frame, 0x1D0 bytes, with FMemory::Malloc (rva 0x4F4788 calls 0x11370F0) whenever its +0x300 holds none. Another virtual of the same vtable (rva 0x4EFBE0) resets the frame, clears +0x300 and frees it at the battle's end. The next battle's allocation can return the freed block.
- `SfvAdapter::SeeBattleFrame` returned early when the frame's address was the last one's, so the adapter kept the first battle's layout, flow record and units. The flow record found the old state list gone and broke, which is the log line above.
- `BattleDriver` also keyed `NewBattle` on that address. So the mode never got the new battle, `RoomBattle::Bind` never took the linked launch, and the runtime never heard the battle had loaded. The copy played the battle offline while its peer waited at the session start.
- The other places were checked. `LaunchArming` uses tickets, and `RoomBattle`, `NetplayMode`, `MatchEnd` (`ForgetEndedVersusBattle`), the flow record and the cached input and rollback units are all reset from the driver's `NewBattle` or the adapter's `Forget`. They were wrong only through those two compares.

**The event** (every address from our IDA database, verified against the exe's bytes; `SfvOffsets.hxx` `kBattleFrameCtor`, checked by BuildCheck):
- **Constructor, rva 0x4C5C40.** It sets the frame's vtable (rva 0x2C742D8, whose +0x40 is `BattleFrameUpdate`, rva 0x4DC6A0) and writes the object to +0x1C8. It has one caller (rva 0x4F4795), right after the allocation. That caller then installs the battle through the frame's +0x08 (rva 0x4DA710). So it runs once per battle.
- **Reset, rva 0x4D8830 (vtable +0x18).** It is not once per battle: it runs at the install (0x4DA710 resets first), at the end (0x4EFBE0), and again in the destructor (rva 0x4C7180).
- Training resets, round changes and the game's rematch (`RestartBattle`, flow code 18) construct no frame. They stay one battle, as before.

**The fix** (sfv-ember 9e88f46 and the log line after it):
- `GameHooks` hooks the constructor and calls the listener after the original runs.
- `mod/BattleIdentity` numbers the battles. An update starts a battle when its frame is not the last update's, or when that frame was constructed again since. Several constructions before an update count as one battle. A late update of an older frame after a construction elsewhere stays the older battle. With no construction seen (a missing hook), the address is all it has.
- `SfvAdapter` forgets on a new number (`Battle()`), and `BattleDriver` gives the mode `NewBattle` on it.
- The log line names the number and the constructions so far: "adapter: new battle frame %p (battle N, M frame(s) constructed[, at the last battle's address])". The battle status carries `battle`.
- `tests/BattleIdentityTests.cxx` (in `sfve_ember_tests`) drives two battles through one address and expects two battles. It also covers frames at another address, constructions that collapse into one battle, the hookless fallback, and the adapter forgetting the first battle's units. `Bind` itself needs the game.

**Evidence**
- `scripts/build.ps1`: 4 of 4 ctest suites pass, with the 5 new cases in `sfve_ember_tests`.
- `tools/runroom.ps1 -lag 30 -games 8`: PASS, 616 checkpoints compared, 0 differing. That is 16 battles (8 per copy), each logged "battle N, N frame(s) constructed" and followed by "the room's battle has loaded", so the hook fired once per battle. No battle came at the last battle's address in this run. Copy 0's 8th battle reused its 4th battle's address (`00000236DA4A5C00`), which shows the allocator does hand blocks back.
- `tools/runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, 154 checkpoints compared, 0 differing; 4 battles, none at the last battle's address.
- `tools/runshell.py`: PASS.

**Left**
- A battle installed on an existing frame through +0x08 (rva 0x4DA710) without a construction would still be the old battle. The only caller found is the owner's begin, right after the constructor.

## 17. SFV Ember's look (2026-11-06)

The maintainer: make SFV Ember's look similar to Ember's but distinct, so it reads as Street Fighter V's, with its own colours and mark. It stays Ember: the same name, layout, screens and structure.

A first pass (graphite grounds, an electric cyan accent, a perspective grid backdrop with diagonal slashes, cut corners and leaning titles) was rejected: it lacked cohesion and read as generic. That pass took a few stock ideas and stacked them. This one starts from SFV's own screens and from one tonal ramp.

**What SFV's menus do (sampled from the game, CE 07.010).** Colours from the captures, in OKLCH:
- The title and ticker bands are neutral charcoal, `#171717` to `#1A1A1A` (L 0.21, chroma 0), edged with black and a thin gold hairline (`#DAB55D`, L 0.79, C 0.115, hue 87).
- The ground is a pale ivory map, `#E9EBDB` (L 0.93, C 0.02, hue 112). Its tiles are a warm light grey, `#D1D1C8` (L 0.86, C 0.012, hue 107), with labels in dark grey `#454545`.
- The chosen tile goes the other way: near-black `#0B0B08` (L 0.15), with its label in gold (`#FFE38C`, L 0.92, C 0.11, hue 92), a gold hairline under it and a gold disc for its icon.
- Type is a heavy sans in capitals, set plainly: no italics in the menus, no tracking tricks, labels left-aligned after their icon.
- Tiles are square-cornered, close together with even gaps. Nothing glows; there are no gradients on the controls; one accent, gold, and the rest neutral.

So SFV's palette is one family: neutrals with almost no chroma, leaning to hue 90-110, and a single gold near hue 88-92. Selection is shown by value (a light tile turning black) and the gold, never by a coloured outline.

**Direction: Band.** Ember's grounds become SFV's charcoal bands, and Ember's orange becomes SFV's gold. Selection works as SFV's does.
- One ramp, OKLCH hue 95, neutral chroma under 0.01. Ink L 0.17, panel 0.21, frame (a card at rest) 0.275, button 0.30, hover 0.33, border 0.37, muted text 0.80, text 0.94. The text is SFV's ivory.
- The accent is gold: L 0.80, C 0.115, hue 88, with a light step at L 0.90.
- Danger, Ready and Pending move to the accent's lightness (0.82) at a chroma at or under the gold's, so none looks pasted in; Pending is a sand on the ramp rather than an orange. LIVE stays a deeper red (0.66), as a live mark should.
- Cards at rest are a step lighter than their panel. The focused card or row goes a step lighter than hover, marked by a gold bar on its left alone (section 36; until 2026-11-09 it went darkest, L 0.12, with a gold hairline along its foot). There is no box outline around it, and secondary buttons are edged in the border tone, not the accent's.
- Screen titles are in capitals. Corners are square. Gold hairlines close the header and open the hint bar, as on SFV's bands.
- There is no backdrop picture: the menu's ground is flat ink.

| Role | SF4 Ember | SFV Ember (Band) | OKLCH (L C h) |
|---|---|---|---|
| ink (background) | `#141312` | `#100F0D` | 0.17 0.004 95 |
| panel | `#211E1B` | `#191816` | 0.21 0.005 95 |
| frame (card at rest) | `#191715` | `#282824` | 0.275 0.006 95 |
| button | `#382D24` | `#2F2E2A` | 0.30 0.007 95 |
| hover | `#59402B` | `#373530` | 0.33 0.009 95 |
| header (focused fill) | `#3A2A1F` | `#060604` | 0.12 0.006 95 |
| border | `#443A31` | `#41403C` | 0.37 0.007 95 |
| rule (header, hint bar) | border | `#7B6633` | 0.52 0.075 88 |
| accent | `#FF8738` | `#DDB961` | 0.80 0.115 88 |
| accent light | `#FFB16F` | `#F2DD9D` | 0.90 0.085 92 |
| text | `#F3EBDD` | `#EDECE0` | 0.94 0.016 102 |
| muted | `#B5A99B` | `#C0BEB4` | 0.80 0.014 100 |
| danger | `#FFAAA0` | `#EEB4AB` | 0.82 0.07 28 |
| ready | `#A4CEA0` | `#A2D3A2` | 0.82 0.085 145 |
| pending | `#F1C477` | `#DFC398` | 0.83 0.065 78 |
| live | `#FF5C50` | `#EE5A50` | 0.66 0.185 27 |

**Contrast (WCAG ratio).** Every pair that carries words or a focus mark is at least SF4 Ember's lowest; most are higher.
- Text on panel 14.9 (Ember 14.0), on the focused fill 17.1 (11.6), on hover 10.3 (8.1), on a button 11.4 (11.3), on a card 12.5 (Ember's darker card: 15).
- Muted on panel 9.5 (7.2), on a card 7.9 (7.8), on a button 7.3 (5.8). Disabled words 5.1 (4.9).
- The gold bar on the focused fill 10.8 (Ember's orange edge 5.7); ink on the primary button 10.2 (7.8).
- The focused fill against a card at rest is 1.4: focus is carried by the gold bar and hairline, as in SFV, not by the fill alone.

**The mark.** It is Ember's own mark, its shapes untouched: the fire character 火 in its open brush circle, with the tail. Only its colours are Band's. Ember's ivory becomes the theme's text, its orange (the left dot and the tail) the gold, and the tail's light orange the gold's light step. Two other tries were turned down: a re-cut octagon around a cut character, and a set of other surrounds (thin circle, gold disc, black tile, none). The maintainer's call was Ember's mark in gold. `scripts/build-sfv-brand.py` writes `art/brand/sfv-ember-mark.svg` (Ember's SVG with its three fills swapped) and recolours Ember's export, `src/ui/ember.png`, pixel by pixel into the 1024 px PNG and the 128 x 128 RGBA the build embeds (`sf4e_embed_brand_mark`, from sfv-ember's CMake).

**Type.** It is still Inter, with no font added. Screen titles are in capitals (`ShellTheme::upperTitles`); rows keep Ember's mixed case. The theme stays dark. A light variant (SFV's menus are light), capitals in rows and a condensed title face were offered and left out (the maintainer, 2026-11-06).

**Working on it.** The look was designed in the UI lab, not in the game. With `-DSF4E_LAB_PRODUCT_SOURCE=<sfv-ember>/src/ember/SfvProduct.cxx -DSF4E_LAB_PRODUCT_MARK=<sfv-ember>/art/brand/sfv-ember-mark.rgba`, the library builds `UiLabProduct`, the same lab linked with SFV Ember's identity, so the real shell draws in SFV Ember's theme with no game and no network (`UiLabProduct --room --shot room.bmp`). Without the two options nothing changes.
**What stays Ember.** The name (EMBER on Home, SFV Ember in the version line and About), every screen and its layout, the hint bar and its button prompts, the flags, the link marks, the meaning of every tone, and the fire character in a ring.

**How it is built.**
- Library: `ember::ShellTheme` in `common/ProductIdentity.hxx`, pointed at by `Branding::theme` (none means `EmberShellTheme`, SF4 Ember's values). `Theme.cxx` (ImGui's style), `v2::Colors()`, `palette::Ember/Ivory/Muted` and the shell's warm-ink literals all read it.
- The theme's shape and mark knobs: `corner`, `cut`, `headingSlant`, `rule`, `underlineFocus`, `quietButtons` and `upperTitles`, drawn through `v2::Ground`, `Edge`, `Corner`, `Slant` and `Capitals`. `cut` and `headingSlant` are kept from the first pass; Band leaves them at 0.
- With Ember's theme every call is the one it replaced, with the same values. SF4-only screens (Training, the public room list, Rankings, the appearance gallery) keep their literals.
- sfv-ember: `SfvProduct.cxx` defines `Band` and the gold, and names the flat ground. CMake embeds `art/brand/sfv-ember-mark.rgba` and copies sfv-ember's `assets/brand` in place of the library's.

**Evidence (library c15f8a044 on feat/sfv-brand, from feat/ember-library 8a7314d7e).**
- SF4 unchanged:
  - UiRenderTest's English shots at 1280x720 and 1920x1080 (402 each), from a build of 8a7314d7e and one of the branch, compared byte for byte. Every difference was texture-load timing: fighter previews, controller pictures and the random tiles, not yet in on one run.
  - At 720p each build was rendered twice, and the branch's second run matches the base's on every file that its first run did not.
  - Targeted ctest passes 14 of 14: UiRender, UiRenderLocales, UiRenderGallery, UiRenderAtlas, ProductIdentity (it now pins EmberShellTheme's values), ShellJourney, Localization, V2Layout, RoomPanelNavigation, MenuButtons, MenuSpeech, ButtonLayout, NativeMenuLock and UpgradeRecoveryPowerShell.
- The library's x86 build and `scripts/probe-x64.ps1` pass.
- sfv-ember: `scripts/build.ps1` passes 4 of 4 ctest suites.
- `python -I tools/runshell.py` passes in game. Its screenshots are in `D:\sfv-re\shots\brand\final\`, with `before-after.png` beside section 14's.
- sf4-net was not rebuilt: the brand build used main's `sf4-net.exe`, which this change does not touch.

## 18. The match HUD on SFV's battle screen (2026-11-06)

The maintainer: "match hud the names should be placed correctly".

**What was wrong.** SFV Ember drew no match HUD at all. Only SF4 Ember's overlay (`sf4e__Overlay.cxx`) builds a `MatchStripView` and calls `DrawOverlayLayers`; SFV's shell (`SfvShell`) never did, so a room's fight showed SFV's own "PLAYER 1"/"PLAYER 2" and none of Ember's names, score or link telemetry (`D:\sfv-re\shots\hud\before\`). Wiring it as it was would not have placed it well either: the split layout's plates are USF4's measurements (`PlaceSplit`, 720p x 146-1134, y 74-95), which land on SFV's life bars, and Top center fits the band above USF4's K.O. sign, where SFV has its timer.

**The names are SFV's own text.** SFV's battle HUD is a Scaleform movie, not UMG. The plate on each life bar is the movie's ActionScript `SetPilotName(side, text)`; the mod hooks the call and gives the room's name in place of the game's label, so the game draws it in its own font and size, scaled with its HUD on every resolution. Every address is from our IDA database of build 13173604 and carries a signature BuildCheck verifies (`SfvOffsets.hxx` `kHudInvoke`):
- **HudInvoke, rva 0x73D600**: `(HudMovie*, const FString* method, float number, const FString* text)`. It calls the movie's player (+0x318, then +0x28; rva 0x73DDA0), which builds the two arguments (a number and a string) and invokes the method. The string is copied into the player's own string table (rva 0xEEBE40, UTF-16 to UTF-8) before the call, so a substituted FString need only live for the call.
- **The HUD's setup, rva 0x3F9260** (a `HudUnit` virtual): for each side, the side as the number (`cvtdq2ps` of the side index), it calls `SetFaceIconPath`, `SetPlayerName` (the fighter's name under the bar; rva 0x3FD150 online, 0x3FD428 offline) and `SetPilotName` (rva 0x3FD223) with the label `HudPilotLabel` (rva 0x416510) picks: `ID_SYS_HUD_0015`/`_0016` by side, `_0011`, `_0017`, or a name.
- **The HUD's update, rva 0x403220**, names a plate again (rva 0x4037B8) when `HudPilotLabel` reports a change (rva 0x403709).
- **The marker over each fighter at a round's start** ("PLAYER 1" on a diamond) is the same movie's `SetCursorText(side, text)`, at rva 0x4047C3 in its setup (rva 0x4041D0), with the same labels by side. It is named as the plate is, so the two never disagree.
- No other code calls these methods: the exe's three `SetPilotName` strings, three `SetPlayerName` strings and one `SetCursorText` string are referenced only there and from another screen's setup (rva 0x64B5A0), which calls another invoke (rva 0x73D560) on its own movie.
- `mod/HudText` hooks HudInvoke, passes every other call through untouched, and for `SetPilotName` and `SetCursorText` asks the adapter for the side's name (`SfvHud`: the member in that seat of the local table, P1 left and P2 right, while `Fighting(session.match)`). It logs each side's plate and marker once per change ("hud: side N's plate "PLAYER N" shows "NAME"") and reports the last ones in the status field `hudText`. Outside a room's game (offline versus, or no room) the plates keep the game's label.
- The HUD names its plates as the battle loads, 0.3 s before the runtime's session starts and takes the match's sides (`matchSides`, `NoteRuntimeMatchStart`). The first build read those and still showed "PLAYER 1". The table the game was launched from already knows its seats.

**Ember's overlay, laid out around SFV's HUD.** The rest of Ember's match HUD (each name's link and class marks, and the telemetry panel with the score, ping, rollback, delay and watchers) is drawn by the library's `MatchHud`, which now lays out around a game's own battle HUD when the product describes it:
- `ember::BattleHud` (`common/ProductIdentity.hxx`, `Branding::battleHud`): the HUD frame's size, each side's plate, whether the game writes the names on them (`namesOnPlates`), each side's marks box, where the top HUD ends (`topEnd`), where the bottom corners' HUD starts (`bottomStart`), and whether the game has a HUD position option for the name offset to follow (`nameOffset`). The plan's `game::Hud::SplitLayout()` (section 2) became this table: the placement is a property of the game's art, constant per product, and the shell's Settings page reads it with no battle running.
- SF4 Ember's product leaves it null, and every SF4 path is unchanged (USF4's arithmetic as it was). SFV Ember's (`SfvProduct.cxx`, `SfvBattleHud`) is measured from its fights.
- With `namesOnPlates`, the split layout draws no plates of its own, only each side's marks: a chip the marks box's height, against its inner end. Every anchor goes around the game's HUD (`PlaceAroundGameHud`): the top ones just below `topEnd` (Top center under the timer), the bottom corners just above `bottomStart`, Bottom center at the screen's foot. The side margins are 10% of the game's frame, not the window. Without `nameOffset` the Settings page leaves out the Name offset row.
- SFV draws the match HUD from `SfvShell::Draw` (`SfvHud::DrawSfvMatchHud`): the strip when nothing is open over the fight, only a stalled or ending fight's notice while the shell is open, as `DrawOverlayLayers` decides for SF4. SFV cannot call `DrawOverlayLayers` itself: it draws Training's layers, which the x64 library does not carry.

**How SFV's HUD was measured** (`before\`, fights at three window sizes, Ryu against Chun-Li): SFV keeps a 16:9 picture, pillarboxed at 2560x1080 (x 320-2240), and its HUD is a 1280x720 frame scaled evenly into it: every edge at 1920x1080 is 1.5 times its 1280x720 place (the bars 158-891 against 105-593, the timer 915-1005 against 610-670). In that frame:
- Life bars x 105-593 and 686-1174, y 56-82, symmetric about x 640. The "PLAYER 1" label inside P1's, x 113-171, y 63-73; P2's mirrored.
- Under each bar, the fighter's name at the outer end (from x 116, to y 99; Chun-Li's x 1082-1162) and the stun gauge at the inner end, x 446-594 and 685-833, y 85-95.
- Timer x 610-670, y 43-91. Portraits in the corners, to y 95; their glow reaches over the bars' outer ends.
- Bottom corners: the V-Skill marks from y 627, the EX gauges y 664-684 to x 367 and from x 912. The middle of the bottom is clear to its foot.
- So the marks go in the row under each bar, in the free stretch between the fighter's name and the stun gauge, against the gauge (P1 x 200-440, P2 840-1080, y 83-99): next to the side's own name, inside SFV's HUD, covering nothing of it. Above the bars they would sit over the portraits' glow, and beside the name inside the bar they would cover the life bar.
- `topEnd` 100, `bottomStart` 624.

**Evidence**
- `tests/HudLayoutTests.cxx` (in `sfve_ember_tests`): on 1280x720, 1920x1080, 2560x1440, 3840x2160 and 2560x1080, every layout, anchor, size and spacing, with and without the score, watchers, class marks and a notice line (720 layouts), Ember's panel, state line and marks stay on screen and clear of SFV's bars, timer, portraits, fighter names, stun gauges and bottom gauges. The marks' places are checked at 720p, 1080p and 2560x1080.
- In game, `D:\sfv-re\shots\hud\` (both copies of a room's game, `tools/runroom.ps1 -lag 30 -games 1` with `SFVE_RES`): `before\` at 1280x720, 1920x1080 and 2560x1080 shows SFV's "PLAYER 1"/"PLAYER 2" and nothing of Ember's; `after\` shows "SFV 1"/"SFV 2" on SFV's plates in its own font, each name's link mark against its stun gauge, and the telemetry at the bottom's middle between the EX gauges, at all three sizes and on both copies (each PASS, 77 checkpoints, 0 differing). Both copies logged "hud: side 1's plate "PLAYER 1" shows "SFV 1"" and the same for side 2, 0.3 s before "netplay: room match as P1/P2". `after\1280x720-round-start-markers-copy0/1.png` show the round-start markers over the fighters naming them "SFV 1" and "SFV 2" on both copies.
- Long names (`after\1920x1080-long-names.png`, a build with 31-character names that was not kept): SFV's plate holds about 10 of the widest letters or 15 ordinary ones and cuts the rest itself. The name never spills over the bar or the timer.
- Library: the SF4 render goldens UiRender, UiRenderLocales and UiRenderGallery (with `ui_render_match_hud`) pass unchanged, with ProductIdentity and ShellJourney. The x64 probe builds clean.
- sfv-ember: `scripts/build.ps1` passes 4 of 4 ctest suites, with the 3 new cases in `sfve_ember_tests`.
- `tools/runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, 154 checkpoints compared, 0 differing.
- `tools/runshell.py`: PASS.

**Left**
- The telemetry shows ping only. `SfvGame::Sample()` reports neither the applied delay ("Delay —") nor the recent rollbacks (always "0f"), nor the connection warning, stall and disconnect countdown, so the state line has nothing to say about the link either: RollbackSession does not report them yet (the adapter's own comment says so).
- Class marks: SFV has no class emblems (they are USF4's ranking art), so only the pill marks (a placing "?", the developer's) draw beside the link mark.
- Names longer than SFV's plate are cut without an ellipsis. Ember could cut them itself with one, but the plate's width in SFV's font is only known from the shot above.
- 16:10 and 4:3 windows are laid out as SFV's 16:9 picture fitted and centred (letterboxed), as the 21:9 one is pillarboxed; no such window was run.

## 19. Instant rematch: Ember's prompt over SFV's WIN (2026-11-06)

The maintainer asked for "the instant rematch". After a room game ends, both fighters can choose to play again at once, in the same battle with the same fighters and stage, without going back to the room. If either declines, or the prompt's time runs out, the battle goes back to the room as before. This closes section 3's gap (b) and section 12's "rematch in place needs a WIN prompt".

**How SF4 does it, and what SFV reuses.** SF4's WIN is the game's own wait (MATCH_RESULT/DRAW_RESULT). Ember's post-match prompt (`ApplicationShell__PostMatch.cxx`) opens over it when the runtime says `MenuContext::MatchResult` (`PostMatchPromptDue`: a fought room game, a table with Instant rematch, `Battle::ResultHeld` and `ResultScreenShown`). Its Rematch is the room's own Ready, which the runtime parks over the WIN (`readyIntent`), and `ReadRuntimeMatchInput` turns it into input bits (`common/RematchInput.hxx`: `ChoiceBit`, or `AskingBit` while the fighter is still asked, for `AskingMs`). Both copies read both fighters' bits on the same frame, and when both chose the game starts the next match in the same battle and GGPO session. The room hears each game with `NotifyRuntimeMatchResult(..., game, continued)`: two `continued` reports of a game count it (`ApplyRecordResult`: `RecordWonGame`, the score, `gamesScored`) and keep the match, its generation and its stream open; anything else ends the match. `RematchInPlaceOffered` ends the offer when the game ends a first-to-N set. SFV reuses every one of these unchanged: the prompt, the parked Rematch, `ReadRuntimeMatchInput`'s bits, `RematchInPlaceOffered`, the result reports and the room's game count. The "generation + 1" of section 3 (b) is that game count: an in-place game keeps the room's match generation, as SF4's does, and the next game is `game` + 1 of it.

**What SFV adds.** SFV's results menu is skipped in a room's battle (MatchEnd, `kShowResult`), so SFV has no wait of its own over its WIN: the mod makes one.
- **The held WIN.** On the confirmed frame the match end is taken (`RoomBattle::TakeEnd`, as before), the battle stands still: its frame updates no longer run the game. The rollback session goes on in held frames (`RollbackSession::StepHeld`): it adds the local input, synchronizes and advances GGPO's frame, but does not advance the game, so the state saved and hashed is the WIN's, frozen. On screen SFV draws its own RESULT (the mark the skipped menu leaves, `kResultPhase` 2), and Ember's prompt over it.
- **Only confirmed frames.** A held frame takes no input for a misprediction (`ggpo_set_inputs_ignored`), as every frame outside the fight, so a guess of the other copy's answer would never be corrected. `StepHeld` therefore steps a frame only once its inputs are confirmed (`LastConfirmedFrame() >= Frame()`): both copies read both final answers on the same frame. The WIN runs in lockstep, which costs nothing on a still picture. A rollback that replays a held frame (none is expected; `heldReplays` counts them) does not advance the game either.
- **The answers** ride in the held frames' inputs above every bit a pad makes (`kInputBits` 0xFFF; Restricted clears them before the game sees a frame): the library's `ChoiceBit` 0x20000000 and `AskingBit` 0x40000000, from `ReadRuntimeMatchInput`, and SFV's own `DeclineBit` 0x10000000 (`ember/RematchDecision.hxx`). A copy sends DeclineBit when its fighter chose Back to room or Character select (`ApplicationShell::PostMatchLeft`, a new read-only accessor; the runtime does not hear those) or when its own room offers no rematch here. The offer is per copy (the room snapshot, Instant rematch, the set's score), so making it one of the exchanged answers means two copies whose rooms disagree still decide alike: both leave.
- **The decision** (`rematch_in_place::Decide`, a function of both inputs and the held frame's number only, so the same on both copies): any DeclineBit leaves; both ChoiceBits rematch; otherwise wait while a fighter is still asked, for at least 60 held frames (the prompt opens on the first, and its first bits arrive an input delay later) and at most 900; then leave. SF4's prompt gives each fighter 10 s and asks for 11 (`AskingMs`), so a fighter who does not answer is a timeout on both copies.
- **The runtime reads the fighter's input every fight frame** (`RoomBattle::NoteFightFrame`), as SF4's battle does. `ReadRuntimeMatchInput` keeps the prompt's timer (`promptDueSinceMs`), which starts over only on a read with no prompt due. The first build read it only over the WIN, so the second game's WIN found the first's timer 60 s old, sent no AskingBit and went back to the room after the 60 held frames; the first in-game run found it.
- **The rematch.** On the held frame both chose, inside `StepHeld` and before the next frame is saved: `MatchEnd::Rematch` (the game's Continue, rva 0x684760: the results mark cleared, then flow code 18, BattleRestart, broadcast synchronously), the session's random generators and the match's start time reseeded from the room's seed and the game's number (`ReseedRandom`), and the flow record recaptured. The next step ends the hold; the intro and the round call follow, and the fight. The GGPO session, its frame numbers and its checkpoints run on: no second battle-loaded sync, no new session. The restart constructs no battle frame (section 16), so `BattleIdentity` keeps the battle and `NewBattle` does not run.
- **The start time.** The fighters' generators are reseeded at each round's start from the match's start time (`kStartTime`), which lies outside the rollback state and which a match data reset takes from the clock (rva 0x304EC0). The netplay mode keeps it at the session's value before each frame of a room battle and logs if the game ever changed it.
- **The room.** Both copies report the game on the frame they decide: `continued` for a rematch, then `GamesPlayedInPlace` counts it and the prompt closes (the library's `gamesInPlace` rule); not continued for a leave, before the battle goes back. A copy that offers no rematch reports at once, as SF4's does. A session lost over the WIN, the runtime ending the battle, or the battle closing first all report a held result as the match's end (`FlushHeld`), as SF4's `FlushHeldNativeResult` does. If the link breaks on the very frame one copy decided rematch, that copy reports continued, the other not: the room ends the match, and the rematched copy's session times out and goes back (5 s), so the two cannot stay apart.
- **The adapter** (`SfvGame`): `ResultHeld` (the result held for an offered prompt, not yet reported), `ResultScreenShown` (the held WIN), `GameResult` (Abort again after a rematch), `RematchChoices` (the last held frame's ChoiceBits) and `GamesPlayedInPlace` come from `RoomBattle`. `SfvCatalog`'s `rematchInPlace` is now true, so the shell offers the table's Instant rematch row and keeps the rule a host sets. `SfvShell` opens Ember's menu once per WIN on `MenuContext::MatchResult`, as SF4's overlay does.
- **Spectators.** SFV has none (`Capabilities::spectators` is off; `RollbackSession` plays two players), so there is nobody to follow. The room's members see the table's score count each game from the room snapshot, as with SF4. (Superseded by section 25: watchers follow the WIN on the fighters' frames.)
- **Addresses.** No new address: `kShowResult` (0x66A0D0) and `kRestartBattle` (0x684760) were already in `SfvOffsets.hxx` with their BuildCheck signatures. The restart's body was read again in our IDA database: through rva 0x587860 it takes the battle frame (the frame owner's +0x300, section 16) and sets its +0x150 to 1, then broadcasts flow code 18 to the battle and calls the battle's vtable +0x820 with (0, -1). It neither frees nor allocates a frame, which the runs confirm (no "new battle frame" after a rematch). +0x150 lies outside the hashed `battleframe` region (+0x190..+0x1BB); both copies write it on the same held frame. `kStartTime` (battledata +108) is the match record's `_time64` stamp written by rva 0x304EC0.

**Library change (SF4 unchanged).** One read-only accessor, `ApplicationShell::PostMatchLeft()`. SF4 never calls it; no behaviour, wire fixture or golden changes.

**The prompt** is the library's post-match screen, so it takes Band's look from the theme: PLAY AGAIN? in capitals with the gold rule, the two fighters with their answers, the gold countdown ring, and Rematch, Character select and Back to room with their pad glyphs and the hint bar. A pad drives it as any shell screen (A Rematch, X Character select, B Back to room).

**The test tools.**
- `tools/runroom.ps1 -rematches N [-firstTo K] [-padAnswers]`: the host's `room` command takes two more numbers, Instant rematch (0/1) and the set's length (first to K, 0 unlimited). One battle plays N + 1 games. Over each of the first N WINs both copies answer Rematch, and the tool checks that both logged the same decision on the same frame, that the room still plays the same generation and that its score counts every game. Over the last WIN copy 0 answers Rematch and copy 1 Back to room, and both must go back to the room. By default the answers go through the pipe: `rematch yes` submits the prompt's Rematch command; `rematch no` declines, as Back to room does. With `-padAnswers` each copy presses A or B on its virtual pad on the prompt itself.
- The battle status has `rematch` (`prompt`, `offered`, `held`, `choices`, `bits`, `games`, `last`) and `heldFrames`/`heldReplays`; the room status has the seat's `table` (score, generation, phase, Instant rematch, set length).
- `tests/RematchDecisionTests.cxx` (in `sfve_tests`): the decision for both Rematches, declines, a copy with no offer, the opening wait, the timeouts and the choices; and two LoopbackPair cases. In the first, two real sessions (input delay 1 and 3) hold from the same frame, answer 25 held frames apart and decide on the same frame; the rematch's change lands on that frame on both, and the hashes agree for 300 frames after. In the second, one peer declines and both leave on the same frame. Neither replays a held frame.

**Evidence** (two copies on this PC, 1 round of 30 s, RYU vs CNL on S25, input delay 2, seeded mash).
- `tools\runroom.ps1 -rematches 3 -lag 30`: PASS. One battle, four games. The three WINs both answered Rematch decided on frames 4807, 8899 and 12992, after 220, 195 and 196 held frames, the same frame on both copies each time. The room's score went 0-1, 1-1, 2-1, all at match generation 1. Over the fourth WIN (P1 `20000000`, P2 `10000000`) both went back to the room at frame 17096, and the room's chat recorded the set 3-1. 285 checkpoints compared, 0 differing; these include the frozen WINs and every game after a restart. 822 held frames, 0 replayed. No "new battle frame" after a rematch, and no start-time change was logged.
- `tools\runroom.ps1 -rematches 1 -padAnswers` (0 ms): PASS. The shell's own A on both pads went on in place (frame 4902). B on copy 1 (`PostMatchLeft`, P2 `10000000`) went back at frame 9121. Copy 0's Rematch then stood as its Ready in the room, and copy 1 was offered Ready for rematch, both still seated. 153 checkpoints compared, 0 differing.
- `tools\runroom.ps1 -rematches 3 -firstTo 3 -lag 30`: PASS. A first-to-3 set in one battle. Games 1-3 went on in place (frames 4808, 8957, 13089), with the score at 0-1, 1-1 and 2-1. Game 4 gave P1 its third win, so neither copy's room offered a rematch. Both sent DeclineBit (P1 and P2 `10000000`) and went back after 2 held frames, at frame 16988 on both. The room recorded the set ("won the set at slot 1 (3 - 1)", 1 W / 0 L), cleared the table's score for the next set and kept both seated. 284 checkpoints compared, 0 differing.
- Instant rematch off (`tools\runroom.ps1 -presses 10 -games 2 -lag 30`): PASS, 154 checkpoints compared, 0 differing. Each game's WIN offered nothing, so the room heard the result at once and both copies went back on the first held frames that carried the declines.
- `tools\runshell.py`: PASS. `tools\runclose.py`: PASS (the host closed in 2.1 s after a graceful leave; the guest closed from its battle in 2.3 s).
- sfv-ember `scripts/build.ps1`: 4 of 4 ctest suites, with the 12 new cases in `sfve_tests`.
- SF4 unchanged (library 64d3168b5): the x86 build; targeted ctest 57 of 57, including RoomPrivateGolden (wire goldens), UiRender, UiRenderLocales and UiRenderGallery (render goldens), ShellJourney, PresentationSnapshot, GameCatalog, RuntimeCapabilities, RoomPanelNavigation and the Session, Room and Iroh suites; and `scripts/probe-x64.ps1`.
- The first in-game run found the prompt-timer bug above: its second WIN went back to the room with no answers. An earlier trial was cut short when another agent's `sfve.py kill` closed both copies; its first game had already gone on in place on both copies at the same frame.
- Screenshots in `D:\sfv-re\shots\rematch\`: `1-prompt-over-win.png` (PLAY AGAIN? over SFV's RESULT, the 8 s ring), `2-waiting-for-opponent.png` (one fighter chose), `3-game4-in-place.png` (the fourth game in the same battle, the game's own "2 WINS"), `4-pad-before-back.png`, `5-back-in-room-3-1.png`, `6-pad-decline-back-in-room.png`, `7-set-over-back-in-room.png`.

**Left**
- With the pipe's answers the shell itself saw no answer. In an Unlimited Classic room, a prompt that closes unanswered gives up the seat (the library's `KeepsSeat`, as in SF4), so after the pipe-answered run neither copy was seated. With the shell's buttons, the seats stay.
- The library's prompt shows the opponent's answer only once the room shows their Ready, and over an in-place WIN it never does: the opponent reads "Choosing" until both chose (SF4 the same). `ShellView::rematchChoices` carries it and could show it.
- If the link breaks on the one frame a copy decides rematch, that copy plays on alone until its session times out (5 s), then goes back. The room has already ended the match.
- Not run in game: a WIN that nobody answers (AskingBit lapses after `AskingMs`, then both leave), and a disconnect over the WIN (the session's failure leaves, with `FlushHeld`). The decision tests and the code cover them.
- The held WIN runs in lockstep: at 30 ms about 48 held frames a second, against the 60 the fight runs. The 60-frame opening wait and the 900-frame cap count held frames, so at high ping they last longer in seconds.

## 20. The link on the HUD, the room's faces, the opponent's answer and the shell's ground (2026-11-06)

Five small gaps: section 18's strip never showed a delay or a rollback; the Members rows showed a fighter nobody picked; the post-match prompt said "Choosing" for an opponent who had answered; the shell over SFV's RESULT, and the cuts between the game and the shell, were jarring (the maintainer: "fix issues with the background on rematch etc being very jaring"); and the chat still said "slot" for a room's table.

**1. The link on the match HUD.** `SfvGame::Sample()` gave a ping and nothing else, so the strip read "Delay —" and "Rollback 0f" and the state line had nothing to say about the link. SF4's adapter (`sf4e__Sf4Game.cxx`) fills the same `LinkSample` from its GGPO session; SFV's `RollbackSession` now reports the same things with the same meanings (`core/RollbackSession.hxx`):

| Field | SF4 Ember | SFV Ember |
|---|---|---|
| connection warning | `simGate.connectionWarningActive`: GGPO's CONNECTION_INTERRUPTED until RESUMED or the drop; not recorded once the result is known | `ConnectionWarning()`, the same gate model and events; hidden once the room battle has taken the result (`RoomBattle::ResultTaken`, SF4's "result known") |
| prediction stall | `simGate.predictionStalled`: the last `add_local_input` hit the prediction threshold | `PredictionStalled()`, the same |
| disconnect countdown | the interruption's `disconnect_timeout`, from the warning's start, in `GetTickCount` ms | `DisconnectCountdownMs()`, the same arithmetic (`DisconnectCountdown`, pinned by a test, a wrap included) |
| ping | `MatchTelemetry`: polled every 250 ms, none (-1) while warned, 0 is no sample, stale after 2 s | the same `MatchTelemetry`, polled from `PollTelemetry` (the runtime's tick) and reset with each battle |
| applied delay | the delay `ggpo_set_frame_delay` took (-1 if it refused it) | `AppliedDelay()`, the same |
| recent rollback | `RollbackHud`: the deepest replay of the last second (a burst per state load, counted per replayed frame) | the same `RollbackHud`: `Begin` in the load callback, `Replayed` after each replayed game frame (a replayed held frame does not count: the game does not advance on it) |

- The link warnings SF4 raises (its GGPO event handler's notices) come from `RoomBattle::NoteLink`, each frame the battle plays and once more when its session is gone: "connection unstable" (a warning) as an interruption starts, "connection restored" when GGPO resumes it, and "opponent disconnected" (an error) when GGPO drops the peer. As in SF4, none once the game's result is taken: the other copy may leave the WIN first.
- `PingSummary` stays empty: Auto delay's record of the games played is SF4's, and turning it on for SFV is a change of its own (the pings are collected for it).
- The battle status has `link` (`warning`, `stalled`, `countdownMs`, `delay`, `recentRollback`, `peerDropped`).

**2. The room's faces.** In `merged\5-room-seated.png` both Members rows showed RYU, though SFV 2 had picked KEN.
- What the row draws: `ApplicationShell::RoomMemberCard` gives the row the member's profile main (`Member::mainFighter`, this PC's own from its settings). That is SF4 Ember's rule by design: the Members column is who each player is, "a main in its original outfit" (library ae1e52896), and ShellJourney pins it ("Member portraits must use saved mains, not battle fighters"). A seat shows the pick, and an unpicked seat the main, captioned "Fighter not shared".
- So the seat did not show a "?" either: in that shot SFV 1's unshared seat showed RYU too. Both copies' main is fighter 0 (RYU), the settings' default: they share one `SFVEmber\settings.json`, and nothing in SFV Ember asks a player for a main. Under SF4's rule, SFV shows a face nobody chose in both places.
- The fix is a product rule, off for SF4: `Branding::roomFacesArePicks`. With it a seat, a cabinet and a Members row show the fighter the player shared (in its costume and colour), else no fighter, which `DrawCharacterPortrait` draws as its "?" (a fighter of -1 has no picture). The seat's caption still says "Fighter not shared". SFV Ember turns it on. The profile's main is still sent and still offered under Profile; nothing in SFV's room shows it.
- SF4 is unchanged: with the rule off the card and `SeatLookOf` take the values they took before (the card's field is renamed `fighter`, with the pick's costume and colour beside it, 0 for a main as before).

**3. The opponent's answer.** Over a WIN that goes on in place the room shows neither player's Ready until both chose, so the prompt read "Choosing" for an opponent who had answered (SF4 too; section 19's "Left").
- `Branding::answersFromInput` (off for SF4): the prompt takes the opponent's Rematch from `ShellView::rematchChoices` as soon as the held frames carry it ("SFV 1 wants a rematch", their row "Ready"), and their decline from the new `rematchDeclines` (their row and the title "Back to room", the ring stopped). Both are kept for the prompt: the battle reports them only until it decides.
- The decline is SFV's `DeclineBit`, which ends the WIN on the frame it is read, so a live read would never be seen. `Battle::RematchDeclines()` (new; SF4's adapter answers 0, as USF4's WIN sends no Back to room) is the deciding frame's declines, which `RoomBattle` keeps until the next WIN or battle, through the runtime snapshot and the view.
- With a Rematch chosen and the opponent's decline in, the prompt says so until the game is back at the main menu, then closes, and the Rematch stands as the player's Ready in the room. Before, it waited over the room for the opponent's answer for up to 25 s.
- SF4 could adopt `answersFromInput` as it is: its `RematchChoices` already carries the opponent's Rematch over USF4's WIN. Showing a decline would need USF4's WIN to send one.

**4. The shell's ground.** What drew what (the `before-*.png` in `D:\sfv-re\shots\polish\background\`):
- The post-match prompt was the shell's window (ink at 97%) as a 720 x 420 panel, over a veil of ink at 55% on ImGui's background list (`ApplicationShell__PostMatch.cxx`). Over SFV's bright gold RESULT that veil is an olive smear, with the RESULT banner, the WIN/LOSE letters and the fighters moving around the panel's edges.
- The room and every other screen are the shell's whole window, ink at 97% (`ImGuiCol_WindowBg`), with no backdrop picture in SFV (section 17).
- Nothing eased between them: the prompt cut in over the RESULT, a rematch cut from the dimmed prompt to SFV's bright FINAL ROUND, and a decline cut from the veil to the room's flat ground.

Three ways were weighed. A darker veil keeps a dim, still moving RESULT behind the prompt, and its edge where veil meets panel. A compact band docked at the bottom, as SFV's own result menu docks, would let SFV's RESULT show cleanly; but SFV's RESULT fills the frame (the banner, the letters, both fighters' stats left and right), so a band holding the prompt's two answers, its words and its three buttons would still cover part of it, and it would be a second layout of the prompt for one product. The chosen way is the room's own ground: `ShellTheme::promptOnGround` (off for SF4) puts the shell's ground (ink at 97%, `WindowGroundAlpha`) behind the prompt and draws the prompt as a panel on it (the theme's panel tone, edged in the border tone), as the room's panels are. The prompt and the room it gives way to then stand on one ground, so going back to the room changes nothing behind the words. The player has seen the K.O. and the winner before the WIN.
- The cuts: SFV Ember's shell fades (`SfvShell`, 0.2 s, eased out). Opening, everything the shell draws that frame comes up together, its ground too: the shell's design parts draw in their own colours, which ImGui's style alpha does not reach, so `FadeScope` scales the opacity of the vertices of every window the shell began and of what it added to ImGui's background and foreground lists. Closing, the shell's ground goes down over the game it uncovers. Nothing of this is in the library. So the prompt eases in over the RESULT, a rematch eases into the restart, and the room eases out into the main menu as a game launches.
- The look stays Band's: one flat ground, no glow, no blur, no new colour.

**5. "Slot".** `chat.event.game_won`, `chat.event.set_won` and `chat.event.ready_timeout` said "slot" for the room's table in English ("won a game at slot 1"), Korean (대전 슬롯) and Simplified Chinese (对战位); the other languages already used their table word. They now use each language's word from `room.table_occupancy` (table, 테이블, 对战席), as library 8a7314d7e did for the Members rows. No other `room.*` or `chat.*` msgid uses "slot" for a table; Training's recording slots, the controllers' game slots and King of the hill's top slot stay. This changes SF4 Ember's text too (the maintainer chose "Table").

**Evidence**
- Library x86 build clean. Targeted ctest: UiRender, UiRenderLocales, UiRenderGallery, UiRenderAtlas, ProductIdentity, ProductRules (SF4's identity, both rules off, the shell as before), ProductRulesOn (a product with both rules on), RoomPrivateGolden, RuntimeCapabilities, CustomRoomGame, RoomPanelNavigation, V2Layout, Localization, GameLanguage and UiPolish pass; ShellJourney passes run alone (it stalls under heavy parallel load). SF4's render goldens and wire golden are unchanged.
- ProductRulesOn first failed on its own expectation: this PC always holds a pick (a default Pick is fighter 0), so its own Members row shows that pick as its seat does; only an unpicked peer shows no fighter. The test now expects that.
- x64 probe clean. sfv-ember ctest 4 of 4, with new RollbackSession cases (the countdown arithmetic and its wrap, the rollback burst, held frames not counted) and a RematchDecision case for declines.
- In game (two copies, 30 ms lag, input delay 2):
  - `runroom -rematches 2 -lag 30`: PASS, 3 games (two rematched in place, then a decline on the same held frame on both copies), 218 checkpoints compared, 0 differing.
  - `runroom -presses 10 -games 2 -lag 30`: PASS, 154 compared, 0 differing.
  - `runshell.py`: PASS.
- Shots (`D:\sfv-re\shots\polish\`): the strip with a live delay and rollback (`1-strip-live-delay-rollback.png`: "Rollback 1f, Delay 2f" at 83 ms); the Members rows and seats with an unshared "?" beside a shared KEN (`2b-members-placeholder-and-pick.png`); the opponent's Rematch read at once ("SFV 1 WANTS A REMATCH", their row Ready: `final\game1-prompt-one-chose-1.png`); the prompt on the shell's ground and the room after a decline with the chat's "at table 1" (`background\after-*.png`, `final\game3-room-0.png`).

## 21. A host's leave: no busy loop, and the successor read at once (2026-11-06)

Section 14 left two things open: one close run in 16 where the guest's new term stayed unwritable for 1.6 s (the host ran out of its 2.5 s cap and took 5.7 s to close), and the helper's coordination refresh retrying about every half millisecond while its IPC queue was full. Both came from one loop in the helper, which was not the queue's doing; the fix is in the helper and game-neutral.

**What the traces showed.** Temporary helper traces (each leave step, every coordination read, every CoordinationState and its delivery class, the Raft metrics, the authority RPCs served), 16 `runclose.py` runs on library 7e7d150de plus traces. Runs 1 and 2 did not start (Steam was not running; the first launch started it). Run 12's room part passed (handover 2.4 ms), but SFV's own engine exit then took 20 s after "the graceful leave is done": not the room. The other 13 passed, host closes 1.9 to 2.5 s.
- The handover itself was never slow: from the host helper's Leave to the successor's committed claim took 1.3 to 4.3 ms in every run (promote the guest's vote, joint and uniform membership, `request_election`, the successor elected at term 2, `confirm_available_successor` at the first try). Election, rebound and checkpoint commit are all inside that. The 1.6 s case did not recur in these runs.
- The guest was spinning before it. The host commits its removal checkpoint about 50 ms before its helper gets the Leave. The guest's helper fences the host's incarnation from that commit (`pending_retired_incarnations`), but only the leader can commit the removal. Each coordination refresh started a membership reconciliation for it; the follower's reconciliation confirmed nothing; and `completed_membership_reconciliation` always republished, which refreshed, which started the same reconciliation: a cycle of about 0.3 ms, until the handoff removed the host. In the 3 s before each leave the guest made 56 to 154 coordination reads, and the host served as many authority RPCs (one per follower read). The queue was not involved: its free count stayed at 126 to 128 throughout. When the queue was full each turn's bulk state was dropped, which is what section 14 saw.
- 3a8ad6735 made each turn cost the native side. A republish clears `last_coordination_state`, and the term test (`last.is_some_and(|last| last.term == marker.term)`) took that for a new term, so every turn's unchanged term-1 state went as a lifecycle event, never dropped: 51 to 149 of them per run, queued ahead of term 2. The guest's game logged term 2 34 to 162 ms after its helper sent it, the time `IrohRoom::Poll` took to read through them 8 or 16 per frame. A longer gap between the host's checkpoint and its Leave, or a slower guest frame, makes that proportionally longer.
- The loop was also the only thing that read the new term promptly: its read in flight when the successor was elected came back stale (`completed_coordination_refresh`'s check), and its immediate retry read term 2. Nothing else refreshes on a Raft change; without the loop the guest would have waited for the once-a-second refresh. And that in-flight read is a follower's authority RPC to the old leader, which can hold the single refresh slot for up to `AUTHORITY_READ_TIMEOUT` (2 s) when the leader's helper is going away mid-read. Both are consistent with section 14's 1.6 s, which these runs did not reproduce.

**The fix (sf4-net only).**
- *No refresh after a reconciliation that changed nothing.* The reconciliation reports whether the applied members or voters changed; its completion republishes only then, when it confirmed a retirement, or when it is stale. A follower waiting on the leader's removal now runs one reconciliation per refresh trigger. `completed_admission` had the same shape for an admission that fails at once with no control to settle (a full membership history, say); it no longer republishes then either.
- *Lifecycle only for a transition.* A CoordinationState is a lifecycle event when its term, `writable` or `leader_local` differs from the last one sent (`coordination_sent`, kept for the room and reset with it, and in the departure grace); a repeat is bulk, as before 3a8ad6735. A term's first state is still never dropped.
- *Wait for queue room.* A bulk state dropped for want of headroom sets `coordination_deferred`; the tick reads the state again once the queue has headroom (`pump_deferred_coordination_state`), rather than retrying or waiting for the next second. The ControlRebound and invitation refresh that follow the state go with it. A later state that is sent clears the flag.
- *Read a Raft change at once.* `install_recovery` watches the Raft metrics and signals `raft_changes` when the term, leader or membership log id changes. With a leader known, the actor drops a read in flight (its `AbortHandle`; it could only come back stale) and reads now. While no leader is known nothing changes, so a candidate's term is not sent as a lost room. The successor's first writable state and rebound go out within the host helper's handover itself: 1.0 to 1.7 ms after the host's helper takes the Leave (traced runs below).
- Three new tests (`service/tests/coordination_state.rs`): a follower holding the leader's retirement runs one reconciliation and no further read; a repeated state under a full queue goes bulk, is deferred and is read again once the queue has room; a Raft membership change and a new term are signalled, and the read in flight across the election is replaced by one of the new term, whose writable state is the last one sent.

**SF4.** The same helper serves SF4 Ember; its host "Leave room" took this path too. No IPC event changed shape, only how often coordination_state goes and which class repeats go in; the control wire, the C++ side and `IrohRoom::Poll`'s 16-event read are unchanged. No wire fixture or golden changed.

**Evidence**
- Traced (the fix as first written plus the same temporary traces, 16 `runclose.py` runs; the two small additions above, the deferred flag cleared by a later sent state and `coordination_sent` reset in the departure grace, came after, and the clean runs below carry them): all 16 passed, host closes 1.9 to 2.5 s. In the 3 s before each leave the guest made 6 to 8 coordination reads (the once-a-second refresh and the reads around the checkpoint; 56 to 154 before) and sent 0 or 1 term-1 state (51 to 149 before); the host served 3 or 4 authority RPCs (53 to 151). The host's helper took 1.1 to 2.1 ms over the Leave, and the successor's term-2 writable state was sent 1.0 to 1.7 ms after it began; the guest's game logged it -1 to 15 ms after its helper sent it (34 to 162 ms before; -1 is the two logs' clocks). The traces were removed before the build below.
- Clean (library 00176021d, as committed, helper and mod built from it; the deployed `sf4-net.exe` hashed the same as the library tree's): 16 `runclose.py` runs, 16 PASS. Each guest saw a clean leave: 1 member, no notice, no "Room control is recovering", term 2 writable and local. Host closes: 2.1, 2.0, 1.9, 2.0, 1.9, 1.9, 2.0, 2.1, 1.9, 2.0, 1.9, 2.0, 1.9, 2.3, 1.9, 1.9 s; the guests closed from their battles in 2.2 to 2.5 s. From the host's game starting its leave to the guest's game logging term 2 (both logs, one PC): 89 to 104 ms, against 124 to 252 ms in the 14 traced runs before the fix.
- `tools\runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, 2 games, 154 checkpoints compared, 0 differing.
- sf4-net `cargo test --locked -j 4 --target x86_64-pc-windows-msvc`: 479 passed, 0 failed (6 ignored), the three new tests among them (476 before).
- Library x86 build; targeted ctest (`Room|Session|Iroh|HelperProcess|RuntimeCapabilities`, RoomPrivateGolden among them, without HelperRust, which is the cargo run above): 42 of 42. `CustomRoomGame` is not registered in this build tree (`SF4E_NETWORK_TESTS` off), so its small scenarios ran directly with the new helper, with their ctest arguments: CustomRoomLateSpectator, CustomRoomSlowSpectator and CustomRoomStalledSpectator pass. `scripts/probe-x64.ps1` passes.
- sfv-ember `scripts/build.ps1` against the library tree: 4 of 4 ctest suites.

**Left**
- Section 14's 1.6 s unwritable term did not recur in 32 runs after the fix, nor in 14 before it, so the fix is matched to it by the trace reasoning above, not by a reproduction. A follower's authority read to a leader whose helper is going away can still wait up to `AUTHORITY_READ_TIMEOUT`; now a Raft change drops it.
- Two refreshes now wait for the once-a-second refresh or a Raft change instead of following at once: after a membership reconciliation that was current but failed, and after a Raft change when the helper already runs `MAX_TASKS` tasks (the read is not started then).
- In two of the 16 traced runs after the fix the host's own path from the game's leave to its helper's Leave took 324 and 492 ms (about 90 ms otherwise; in the 492 ms run the helper reported the room closed 419 ms after the native side logged the acknowledged leave). The guest was not involved and the closes were 2.4 and 2.2 s. It did not recur in the 16 clean runs; not investigated.

## 22. SFV Ember's launcher, updater and package (2026-11-06)

The maintainer: "make the launcher (same as sf4 ember) ... up to parity with usf4 ember". SFV Ember now starts as SF4 Ember does: a player runs Launcher.exe from an extracted package, the launcher finds the game through Steam and starts it with the mod inside it, and nothing is written into the game folder. Library `feat/sfv-launcher` (from 5d27f6417): ff56f7f51, 711398482. sfv-ember `feat/launcher` (from 44200cb): f6a2f51, e3316cd, f325ef9 and this section.

**One launcher program, the game's steps per product.** SF4 Ember's launcher.cxx is now the launcher of both products, not a fork:
- Its SF4-only steps moved unchanged to `launcher/Sf4Launch.cxx`, behind `launcher/LaunchSteps.hxx`, which each product defines once at link time, as it defines `Product()` and `Catalog()`: Sidecar's payload and its Detours start with the helper and Discord companion it is handed and the sync wait (`StartGame`, `GameEnded`), the replay archive before and after a run and the steam_appid.txt dev fallback (`BeforeStart`, `GameEnded`), USF4's language from its folder (`GameLanguage`), Sidecar's runtime libraries for the shadow scan (`RuntimeLibraries`), replay links (`ResolveReplayLink`), the display name and payload fields (`Prepare`), and the log name, lock scope and extra options of a product's test copies (`LauncherLogName`, `InstanceScope`, `AddOptions`). SF4's are the same calls in the same order, with the same log lines.
- `ProductIdentity` gains `SteamGame`: the app id (the environment's SteamAppId and SteamGameId, and `appmanifest_<id>.acf`), the usual install folder, and the exe's folder under it. `GameLocator` reads them: SF4 Ember's are 45760, "Super Street Fighter IV - Arcade Edition" and none; SFV Ember's 310950, "StreetFighterV" and `StreetFighterV\Binaries\Win64`. The install folder's own `StreetFighterV.exe` (175 KB) is UE4's bootstrap, which starts the real game; started instead, it would start that one without the mod.
- The vc_redist link (x86 or x64) and Steam's overlay module (`GameOverlayRenderer.dll` or `GameOverlayRenderer64.dll`) follow the launcher's own bitness, which is its game's; so does the system folder of the shadow scan.
- The x64 probe builds `launcher.cxx` with the recovery window (`ember_launcher_x64`, now including Direct3D 9's `SelectionArtD3D9`, `RecoverySurface`, `RecoveryController`) and `updater.cxx` (`ember_updater_x64`), with `/W4 /WX`. `SteamKeyValues` (a VDF tokenizer), `GameLocator` and `netplay_persist` are neutral. One fix fell out: `netplay_persist.hxx` included `../common/` from `launcher/netplay/`, which only resolved through SF4's include paths.
- An empty release repository means no update check: `CheckForUpdate` succeeds, offers nothing, makes no request and logs "No release repository for this channel; no update check" to the update log. The Updates window then says "You are up to date." (screenshot below).
- The package inventory is the product's: a product's build names its file (`EMBER_PACKAGE_INVENTORY`), SF4's stays `common/PackageInventory.inc`. `Obsolete` is a list, since SFV Ember's has none. `collect-notices.py` takes a product's name and vcpkg ports.

**SF4 Ember is unchanged.** No SF4-visible text changed (the one rewritten log line, "Game directory from settings has no {gameExe}", prints the same words for SF4), its CLI help is the same, and no golden changed. The one ordering difference: after a failed start the helper and Discord objects are released before the recovery window opens rather than after; both are already stopped on every failure path, so nothing observable moves.

**SFV Ember's launch steps** (`src/launcher/SfvLaunch.cxx`): `BeforeStart` sets SteamAppId and SteamGameId to 310950 (Steam must be running, as for SF4 Ember); `StartGame` starts the Win64 exe suspended, in its folder, with `DetourCreateProcessWithDllsW` adding `sfv_ember.dll` from the package to its imports and the command line `-sfve_loader=launcher -sfve_mode=netplay -sfve_room=1`. The ownership check is ModConfig's own default (no flag). Steam's `%command%` is ignored as SF4's is. No replays, Discord, game language or runtime libraries: those steps do nothing. The mod starts its helper itself (EmberHost), so the launcher hands it none.

**Load path: injection, with evidence.**
- StreetFighterV.exe's entry point (rva 0x43EC310) is in a `.bind` section (rva 0x43EC000, 0x32918 bytes): SteamStub. Its import directory is an ordinary one in `.rdata` (32 DLLs, XINPUT1_3.dll among them) and it has no TLS directory. Detours' import rewrite in the suspended process works under it: every launch below logged "before game code" and "build: verified (SFV CE build 13173604, 52 sites verified)", so the stub ran and reached the game's own entry with the mod already started. No other injection method was needed.
- The mod starts itself (`src/mod/EarlyStart.cxx`): a static initializer, which runs in the DLL's process attach under the loader lock, sees `-sfve_loader=launcher` and points the exe's entry at its EntryHook (the proxy's 12-byte patch); EntryHook puts the bytes back and runs `SfveStart(1)` outside the loader lock, then the entry point. `sfv_ember.def` pins `SfveStart` at ordinal 1, which Detours imports.
- One start per process (`src/mod/StartClaim.cxx`): the first copy to start claims `Local\SFVEmber.Mod.<pid>` with its path and loader; a later copy logs both and stays inert, with no hook installed. This branch's proxy loads no second copy when the launcher's is already loaded.
- Settings and logs are under `%APPDATA%\SFVEmber` (`settingsBesideModule` false; `mod/Log.cxx`), whichever loader started the mod. A `SFVEmber\settings.json` beside the module or beside the game's exe is copied there once, when there is none (`MigrateSettings`); the old file stays. The overlay publishes the game's window (`GameWindowSection`), so the launcher's freeze watch and a second launcher's link hand-off see it.

**Updater and package.**
- `Updater.exe` is the library's updater under SFV Ember's product and inventory (`src/launcher/PackageInventory.inc`). `sfve_package_check.exe` runs its validation and extraction for scripts.
- `scripts/build.ps1` writes a build receipt (`build-provenance.json`: both checkouts' commits and source fingerprints by the library's `Get-SourceFingerprint`, the binaries' hashes, the tests that passed). `scripts/build-helper.ps1` (cargo `-j 4`, `--locked`) records the helper's checkout, fingerprint, Cargo.lock and hash.
- `scripts/package.ps1` refuses anything else: the build tree configured from this checkout (`Assert-CMakeSource`) and the receipt's library, both checkouts at the receipt's commits and fresh fingerprints, the binaries and the helper unchanged. It stages the inventory's files (the binaries, SFV Ember's ground, Ember's prompts and flags with their licences, the notices, Ember's licence and attribution, `docs/guides/START_HERE.md`), writes the package receipt (the build's, with the helper's record and the package's label), BUILD_INFO.txt and MANIFEST.txt, validates the folder by the updater's rules, zips it, runs the library's `test-updater-download-validation.ps1` on the zip (its transport, checksum, archive and fixture refusals, then the package), and writes the .sha256. Output: `D:\sfv-ember-dist`; it refuses the C: drive.
- Launcher.exe and Updater.exe carry SFV Ember's version, its mark as their icon (`art/brand/sfv-ember.ico`, from the same recoloured export by `scripts/build-sfv-brand.py`) and an amd64 manifest. The recovery window draws in the Band theme with SFV Ember's mark; it shows no fighter (`src/launcher/LauncherSelection.cxx`: no pictures outside the game).
- The launcher's, recovery's, updater's and problem reports' words that named SF4, SSFIV.exe, `%APPDATA%\sf4e` or x86 have SFV Ember's own in `SfvProduct.cxx`'s English overlay; a test checks none names SF4 and that the runtime message says x64.

**Test copies.** `Launcher.exe --instance N` (1-3) starts copy N with its own lock (`<prefix><SID>.instanceN`) and log (`launcher.instanceN.log`; `launcher.N.log` are the log's own rotations); instance 0, what players run, keeps the one lock per user. `--game-option` passes one `-sfve_*`, `-windowed`, `-ResX=N` or `-ResY=N`, placed first so it takes the place of the launcher's own (ModConfig reads the first). `tools/sfvelib.py` starts copies through the launcher with `SFVE_VIA=launcher` and `SFVE_PACKAGE_DIR` (a package, or the build tree), or through the proxy as before (the default). Logs are read from `%APPDATA%\SFVEmber\logs` (also `runroom.ps1` and `synctest.ps1`). `kill` now kills only the copies the tools started (launcher trees and games, kept in `%TEMP%\sfve-launched.json`).

**Evidence** (library 711398482; sfv-ember e3316cd, package `D:\sfv-ember-dist\sfv-ember-0.1.0-beta.1-20261106-214025.zip`, unless noted; the final package is f325ef9's, `sfv-ember-0.1.0-beta.1-20261106-220301.zip`, SHA-256 b7de22842aa2079f0c7392c2421b5e94cd2380606521f9e840545ce01730138c. The xinput1_3.dll proxy was undeployed for the runs and the game folder restored after, byte for byte):
- Library x86: targeted ctest 20 of 20 (ProductIdentity, ProductRules, ProductRulesOn, JoinLink, GameLocator, PackageInstaller, UpdaterRun, UpdaterDownloadValidation, UpgradeRecoveryPowerShell, PackageFiles, BuildProvenance, ApplicationServices, UpdatePolicy, EmberUpdate, RecoveryController, ProgressWindow, SettingsStore, HeapCorruptionCapture and the rest of the set), then the default set without the Rust, SDK, Nightly and network suites: 231 of 231 (SpeechWine skipped).
- sfv-ember `scripts/build.ps1`: 4 of 4 suites; `sfve_ember_tests` gains the launcher's words, the product's Steam game and folders, the update check with no repository, and the manifest search for 310950.
- Public start (Launcher.exe from the package, no options): the mod "loaded from D:\sfv-ember-dist\...\sfv_ember.dll, started by the launcher", before game code; network guard installed 0.45 s after; build verified; main menu (scene 0x2D98660) 20.8 s after the launcher started; the guard refused the game's only two connection attempts (`InternetConnectW cdn2-prod-cfn.akamaized.net`, `etsource.epicgames.com`), so nothing reached Capcom; the window's close left the room, the game exited 0x00000000 and the launcher exited 0, 1.9 s after the close. Logs in `%APPDATA%\SFVEmber\logs`.
- `tools\runroom.ps1 -presses 10 -games 2 -lag 30` with launcher-started copies: PASS, 2 games, 154 checkpoints compared, 0 differing; again from the final package: PASS, 154 compared, 0 differing. `tools\runclose.py`: PASS (the host closed in 2.0 s and left the room, its helper with it; the guest saw one member and no recovery, and closed from its battle in 2.2 s; both launchers logged a clean exit). `tools\runshell.py`: PASS.
- Two copies of the mod in one game: with the proxy and mod from another build deployed beside the game, both copies loaded; the proxy's started first and the launcher's logged that it stays inert, naming both. With this branch's proxy deployed, the game held only the launcher's copy.
- Updater.exe from the final package, started with no arguments as a player double-clicks it: exit 0 after it opened the launcher's Updates window, whose check logged "No release repository for this channel; no update check"; the window closed and the launcher exited (`updater-plain.png`).
- `scripts/probe-x64.ps1`: passes, 442 neutral units, `ember_launcher_x64` and `ember_updater_x64` with `/W4 /WX`, and the runtime test under the fake game.
- The public start's package (e3316cd's) was made by `scripts/package.ps1` as described; on each run the folder passed the updater's rules and the zip the download checks.
- Shots, `D:\sfv-re\shots\launcher\`: `recovery-build.png` (the recovery window: SFV Ember's mark and Band theme, "Street Fighter V" in its words), `updates-build.png` ("You are up to date." with no repository), `oldproxy-main-menu.png` and `newproxy-main-menu.png`, and the runs' own shots under `runroom\`, `runclose\` and `runshell\`. The public start ran full screen, which a window capture cannot read (`public-main-menu.png` is black).

**Left, and the maintainer's calls**
- A release repository for SFV Ember (`stableRepo`, `nightlyRepo`): until there is one, updates are off and the Updates window says "You are up to date.".
- An installer: the library's `scripts/installer/ember-setup.iss` and `package-installer.ps1` are SF4 Ember's (its AppId, name, x86 runtime and LICENSE). It needs the product's AppId, name, licence and the x64 runtime as defines, and its test a product too. Not built.
- Code signing: Launcher.exe, Updater.exe and sfv_ember.dll are unsigned (`sign-release-binaries.ps1` is SF4's).
- SFV Ember's own licence: the package carries Ember's (`notices\Ember-LICENSE.txt`, `Ember-ATTRIBUTION.md`).
- The crash dump channel: the launcher creates and serves it (freeze dumps of the published window), but the mod has no crash handler that asks for a dump, and UE4 handles the game's own crashes; the mod's side is next. **Done in section 29.**
- The settings copy did not run on this PC: `%APPDATA%\SFVEmber\settings.json` from 2026-11-05 was already there, so by design nothing was copied. A room link handed to a running game (`ember-sfv://`, registered per user) was not exercised. **Exercised in section 29.**
- The CLI's `--join-link` help names `ember://`, the shared text. **Fixed in section 29.**
- The other agent's proxy-era files stay in the game folder as they were; once its branch merges, `SFVE_VIA=launcher` can become the tools' default and the proxy can go.

## 23. Ownership: each player picks what their own copy allows (2026-11-06)

The maintainer asked for SFV Ember's ownership check "up to parity with usf4 ember". The rule is SF4's DLC gate: each player may pick only what they own, and the opponent's pick plays as picked, whatever this copy owns. SFV_PORT.md open question 3 is decided so. This section is what SFV actually gates offline (evidence first), Ember's gate on it, and why a room's match never runs the game's own check.

**What the offline game gates, and where** (our IDA database of build 13173604; every address below is an rva)
- **One source for every menu and the battle jump: the battle setup data asset** (`UKWBattleSetupDataAsset`, 344 bytes, `/Game/GameFlow/CommonSceneStates/BattleSetup/BattleSetupDataAsset`, found by 0x551520 and 0x477A70; class getter 0xC26230). Its lists:
  - fighters, 0x54E8E0: `kSelectableFighters` (the spec's selectable fighters whose package is installed) that the asset allows (0x55A9B0);
  - costumes, 0x5545B0 with 0x55AEF0: `kPlayableCostumes` that the asset allows. This is section 15's "third, separate filter", and the costume settings menu's list (0x8CC6E0, case 3);
  - colours, 0x55ABA0: the spec's colours of the costume that the asset allows;
  - stages, 0x550DA0: the spec's stages that the asset lists at +184 and does not exclude at +168.
- **What "the asset allows" is.** Its +96 byte restricts every list to the asset's own list at +120 (a TArray of 24-byte `KWBattleSetupCostumeColor`: +0 fighter code, +16 costume, +20 colour), less a set at +104, each entry also passing the filter object at +280 when one is installed (vtable +8; a BattleSetupState's constructor installs one, 0x5A4D50 and 0x574510, from a list it copies from the object 0x141E6A740 returns, its +40). With +96 clear, fighters are those of the asset's select-grid list (+48) that are installed, and costumes and colours are the spec's and the install's alone.
- **Nothing on these paths reads the account.** No Steam call and no saved data. The exe's one Steam licence query (0xE112C0: `ISteamApps` vtable +0x38 for app 475230 only, `BIsDlcInstalled` by the layout of the Steamworks the install ships, Steamv130) is reached from three menu screens (0xAAC150, 0xAADDE0, 0xAC6310), where it sets a menu's new-content mark; the start-up check is vtable +0x30 (`BIsSubscribedApp` by that layout) of 310950, called at 0x10BFF66. A login-gated fighter list (0x565F20: lists held by the game instance's +144 object; the exe also names a `UKWFreeTrial` class, not tied to it here) and the shop's fighter list (0x551E50) are read only while 0x742A20 holds: the default online subsystem (0x73BEC0, the "OnlineSubsystem" module's) reports status 2 for the local user from its interface's vtable +288 (UE4's `ELoginStatus::LoggedIn` is 2). Offline it does not (live: `logged in 0`).
- **The battle jump's own check** (0x5E2D10, run in the step after the second, 0x602790, when the init message's +46 is set) asks the same lists: per player the fighter (0x56AF20, or the login-gated list's, 0x56B3A0), then `kCostumeValid` and `kColourValid` (membership in `kPlayableCostumes` and the spec's colours: the install, not the asset's restriction); then the stage. Its result (jump state +856): 0x210 a fighter it does not allow (that player's costume and colour set to 0, and the jump leaves with the error notice Notice_ERROR_0030, 0x602830), 0x102 a costume set to 0, 0x101 a colour set to 0, 0x108 a stage it does not allow replaced by TRN or S25, in one branch chosen by `rand()` (0x5E3082 to 0x5E360A), 0x400 no setup asset. It judges **both** players by this copy.
- **The install.** The Steam manifest (`appmanifest_310950.acf`) installs depot 310951 (48.9 GB, every pak) for every owner of the game; this account also has two DLC depots, 1172540 (16.5 MB) and 1474240 (10.2 MB), neither of them fighter or costume packages. Every copy therefore has every fighter's and costume's data, as SF4's installs did, and `kSelectableFighters`/`kPlayableCostumes` agree on every copy of the same build.

**What this PC's game reports** (one copy, Steam running, `content` on the pipe; the log line is `content: ... ownership read: setup restricted 0 (0 entries), filter 0, logged in 0`; every copy now logs the same as its catalog is filled, `ember: ownership gate on; ...`)
- The asset's restriction is off, no filter is installed, the identity is not logged in.
- Every fighter (45), every costume (394) and every colour of every costume is allowed: `not owned: 0 fighters, 0 costumes, 0 colours`.
- 10 of the 37 playable stages are outside the asset's stage list (SH4, NZ1, KZ2, RU2, S05, S16, S17, S18, S19, S25: night, seasonal and alternative versions). That list is the asset's, the same on every copy, not the account's. S25, the stage the test tools use, is one: the pipe's `launch versus RYU:0:1 KEN:0:2 S25 1 30` with the game's check on logged `the game's ownership check gave 0x108`, and the battle loaded on the grid instead (`D:\sfv-re\shots\ownership-check-s25.png`).
- So offline SFV judges only its own data and the install, never the account's purchases: an account that owns no DLC would get the same answer. Whether that is so for a non-owning account was not tried (no such account here); the code paths above read nothing that could differ. SF4's gate has the same gap the other way: it was never run against an account lacking DLC either.

**Ember's gate** (SFV's catalog; no library change: the shared Ready check, Random and pick fitting already ask the game's catalog)
- **The truth.** `GameContent` reads the setup asset's lists once with the content lists (`FighterContent::owned`, `CostumeContent::owned` and `ownedColours`, `ContentLists::ownedStages` and `ownership`), on the game thread at the main menu (StaticLoadObject finds the already loaded asset). Without the asset there are no lists at all and Ember's runtime waits, rather than start with lists that say nothing or everything is owned. Ownership never enters the build's fingerprint: the build id is unchanged (`sfv-ember/13173604/0.2/8dafe602f7615d90`).
- **The rules.** `FighterLocked`: not installed here, or (gate on) not owned. `AvailablePick`: a valid pick, the fighter not locked, the costume installed and owned, the colour owned. The runtime's Ready check asks both for this copy's own player only (`runtime.ready.fighter_unavailable`, `runtime.ready.selection_unavailable`). `LaunchPicks` checks the spec alone, so the opponent's pick launches as picked.
- **Fitting.** `NormalizePick` with no availability is the spec's fitting, the same on every copy, as before. With availability (this copy's own player: the pick store, a Random roll's look) it then fits to what they can ready (`FitOwned`): the roster's first fighter they may play, that fighter's first costume they can ready, that costume's first colour of theirs. The pick store (`SfvPick`) keeps only such a pick, so a stored unowned pick falls back. `RandomCandidates` (the library's) skips locked fighters through `fighterLocked`, and `RolledPick` fits the look the same way.
- **`FighterAvailability`** reports `ready` for a fighter this player may play (`OwnedAvailability`). SFV's costume and colour numbers do not follow the SF4 layout of `Availability`'s bits (costume numbers with gaps, EX colours from 100 per costume), so the bits are all set for a playable fighter and SFV's rules read their own table for costumes and colours. `Availability` was not extended; SF4's output is untouched.
- **The pick screen** (`SfvSelectionScreen`). Every row's Select opens its choices (the costume and colour rows now too). A fighter, costume or colour this player cannot ready is listed dimmed and cannot be chosen; while one the game does not let them pick has the focus, the pane says SF4's `selection.not_owned`. Left and Right step only what they can ready. The choice dialog draws an option's detail under its list, which a long list pushes below a 540-line screen, so the pane shows it. SFV Ember's English overlay words it "Not owned in this copy of Street Fighter V." (SF4's says "Not owned on Steam.", which is not where SFV's answer comes from). No new msgid.
- **Flags** (ModConfig). `-sfve_ownership=0` turns Ember's gate off (development, two-copy tests); the default is on, as in the public version. `-sfve_unowned=CODE[,CODE:costume[,CODE:costume:colour]]` marks content unowned on one copy on top of the game's answer, to try the gate with an account that owns everything; it never makes anything owned. An entry that does not parse is left out and logged. `-sfve_game_ownership=1` runs the game's own check in a launch that is not a room's (the pipe's `launch`); default off. `-sfve_ownership`'s old meaning (run the game's check at every launch) is gone.

**A room's match never runs the game's check.** VersusLaunch clears the init message's +46 for every linked launch, whatever the flags say, and logs it ("a room's match: the game's ownership check does not run"). The check judges both players by one copy and, for a stage outside the asset's list, picks the replacement by `rand()`: two copies would load different battles. Each player's pick was judged by their own copy before Ready; the picks both copies agreed are the picks loaded.

**Evidence**
- sfv-ember `scripts/build.ps1`: 4 of 4 ctest suites. `sfve_ember_tests` adds: Ready refused for an unowned fighter, costume or colour and the opponent's unowned pick launching as picked; NOT OWNED only for what the game does not allow (not for what is only not installed); a stored pick falling back fighter, costume and colour; Random rolling only playable fighters and a rolled look fitted; `-sfve_unowned` parsing (a bad entry left out) and marking only towards unowned; the fingerprint unchanged by ownership.
- `tools/runshell.py --shots D:\sfv-re\shots\ownership --unowned CNL,KEN:1,KEN:0:1`: PASS (2 of 2). Copy 0's pick, stepped two fighters right and one colour right, came out KEN's DEFAULT in colour 03: the steps skipped CHUN-LI and colour 02. `4-pick.png`; `4b-not-owned-fighter.png`, `-costume.png`, `-color.png`: each list open with the focus on CHUN-LI, KEN's Story costume and his colour 02, dimmed, and the pane reading "Not owned in this copy of Street Fighter V.". Both copies were back in the room after the match, 1 - 0.
- **The opponent's pick plays as picked** (step 5): `tools\runroom.ps1 -lag 30 -games 2 -unowned0 CNL:0:2 -unowned1 RYU` (the new `-unowned0/1` options). Copy 0 picked RYU in colour 1, which copy 1 marks unowned; copy 1 picked CHUN-LI in colour 2, which copy 0 marks unowned. Both copies logged "a room's match: the game's ownership check does not run" and the same launch for both games, "player 1 ... -> RYU costume 0 colour 1", "player 2 ... -> CNL costume 0 colour 2", stage S25; both decided at frame 4586, 154 checkpoints compared, 0 differing, PASS. The catalog's own log line (new: `ember: ownership gate ...`, written as the catalog is filled) shows the marks in effect; a one-game rerun with it, also PASS (77 checkpoints compared, 0 differing), logged on copy 0 "not owned: 0 fighters, 0 costumes, 1 colours, 10 of 37 stages" and on copy 1 "not owned: 1 fighters, 0 costumes, 0 colours, 10 of 37 stages", both "setup restricted 0 (0 entries), filter 0, logged in 0", and the same launch on both.
- `tools\runroom.ps1 -presses 10 -games 2 -lag 30` (no marks): PASS, 154 checkpoints compared, 0 differing, both games decided at frame 4586 on both copies.
- `tools/runclose.py`: PASS. Copy 1's own battle from the pipe's `launch` while joined played offline and copy 1 stayed in the room; the host closed in 2.0 s (graceful leave done, its helper gone); the guest saw 1 member, term 2 writable and local, and closed from its battle in 2.3 s.
- Shots kept in `D:\sfv-re\shots\ownership\`: `4-pick.png`, the three `4b-not-owned-*.png`, `8-back-in-room-game1.png`; and `D:\sfv-re\shots\ownership-check-s25.png`.
- The library is unchanged (`feat/sfv-ownership` has no commit): the shared Ready check, Random fighter and pick fitting already ask the game's catalog, and sfv-ember's build compiles the library's x64 runtime from that tree (its `ember_runtime` suite passes).

**Left**
- **The account's purchases.** Offline SFV does not know them, so the gate refuses nothing on a real account today; it is ready for the day something reports them. Reading them would mean a source outside the game: Steam's DLC licences (a mapping from Steam DLC app ids to fighters and costumes that the exe does not hold, and it would miss what an account bought with Fight Money on Capcom's servers), or Capcom's servers, which SFV Ember never contacts. The maintainer's call.
- Whether to gate stages. The asset's stage list is the same on every copy and a room's stage is P1's choice, so the stages outside it are logged, not gated. S25, the test tools' stage, is outside it.
- The choice dialog's detail line under a long list is off a 960x540 screen (the library's dialog); SFV's pane repeats it.

## 24. Public rooms (2026-11-06)

The maintainer asked for SFV Ember's public rooms "up to parity with usf4 ember". A public room's authority is not a player's game: the ember-rooms supervisor starts one `sf4e-room-host` per room, keyed by the client's build id, with its own helper as the room's sole coordination voter (the library's docs/design/PUBLIC_ROOMS.md). That host runs the same `SessionServer` and `RoomModel` a private room's host player runs, and they judge every pick through `ember::Catalog()`. SF4's host links SF4's catalog (`src/product/Sf4Catalog.cxx`); SFV's catalog exists only inside the game, filled at run time from its spec data (section 15). This section is how an SFV room host judges SFV's picks without the game, how the services keep the two games' rooms apart, and what the client does.

**What the host actually asks of the catalog** (the library's code, `ember::Catalog()` callers reached from the room host's sources):
- `fighterCount`: every member's fighter, main and kept fighter (`RoomModel.cxx:111,233`, `RoomModelJson.cxx:387,497`, `SessionServer__Handlers.cxx:346`), and the listing's faces (`RoomHostStatus.cxx:81`).
- `costumeLimit`, `colorLimit`, `ultraChoices`: the look a seat shows (`RoomModel.cxx:114`) and the bounds a member's look is read back from a checkpoint with (`RoomModelJson.cxx:399`: `ReadInt(..., costumeLimit - 1)` throws past it).
- `validPick`: a seat's pick (`SessionServer__Handlers.cxx:420`) and the match's picks (`:577`); `validStage`: the match's stage (`sf4e__SessionProtocol.cxx:38`).

The limits must be the clients' to the number, or a member's checkpoint read throws on one side; and SFV's `validPick` is membership (a costume of that fighter, a colour of that costume, section 15), not a range. So "validate only what the server needs" still needs the whole pick space. The pick space is exactly what the build id's fingerprint hashes (section 15: codes, costume numbers, colour numbers; no names, installs or ownership).

**Design: the host judges by the pick space the game exported, embedded in the host and checked against the build id.**
- `src/ember/SfvPickSpace` is the pick space and SFV's rules over it with no game code (C++17, g++ alone): the fighters' codes with their costume and colour numbers and the stage codes; `Problem`, `Fingerprint` (moved verbatim from `SfvCatalog`'s `ContentFingerprint`, XXH3 header-only), `BuildId`, `ValidPick`, `ValidStage`, `FitPick` (the spec half of `NormalizePick`), the limits and `CostumeCount`; the table rules SFV plays (1/3/5/7 rounds, 30/60/99/300 s; a test checks them against `VersusLaunch`'s `PlayableRounds` and `RoundClockFrames`); and the JSON the host reads. `SfvCatalog` now answers every validity question through it and keeps installs, ownership and names on top, so the game and the host apply one definition. The build id is unchanged by the move.
- The pipe's `catalog-export <path>` writes the filled catalog's pick space and build (`PickSpaceJson`). The export for build 13173604 is committed as `data/pick-space.json`.
- `sfve-room-host` is the library's room host (`server/roomhost`: `main`, `RoomHost`, `RoomHostStatus`, the helper link) linked with `src/roomhost/SfvHostCatalog.cxx` in place of SF4's catalog, and the pick space embedded as bytes at build time (CMake on Windows, `scripts/build-room-host-linux.sh` on Linux through the library's `build-linux.sh`). The host is no player: no fighter is locked, every valid pick is available, a pick is fitted by the pick space alone, and names are the game's codes.
- **The check.** Before the room opens, the library's new hook `roomhost::PrepareCatalog(config, reason)` runs (`main.cxx`, right after the configuration line parses). SFV's refuses unless the room's game is `sfv`, the embedded data parses and makes a pick space, the build that pick space makes (`kMatchBuild` + "/" + its fingerprint) equals both the build the export recorded and the configured `build_id`. A refusal is the host's `closed` line with the reason (`other_game`, `catalog_invalid`, `other_build`) and exit 2, so a host deployed under the wrong build id never opens a room. SF4's hook (`server/roomhost/RoomHostSf4Catalog.cxx`, the new `host_catalog` source group) takes only `usf4` rooms; SF4's host is otherwise unchanged (its catalog, sources, configuration line and status lines).
- Why embedded and not a file beside the host: the supervisor installs and starts exactly two files per build (`room_host`, `helper`), a room host is already per build, and the data cannot then drift from the binary. A new game data build (an SFV patch, a new match version) needs a new export and a new host, as an SF4 release needs a new host.
- Rejected: building the host without the pick space (bounds only): it would need the limits anyway, which only the game knows, and a pick with a costume the fighter lacks would be taken by the authority and refused by both copies' launch (`Entry::Rejected`), ending the game. A file the operator ships: one more file to install and to get wrong. A hash change to SHA-256 so the library could compute it without xxHash: it changes every SFV build id for no gain; xxHash is one header.

**Keeping the games apart: the game key.** In-game the build id already partitions rooms (the helper lists `?build_id=` and the bridge refuses a ticket for another build), but the website's list, the ranking and the ticket's class are per bridge, not per build. The library now names a room's game end to end (PUBLIC_ROOMS.md, "Games"):
- `ProductIdentity::roomGame` ("usf4" for SF4 Ember, "sfv" for SFV Ember). The runtime adds `game` to its `room_list`, `room_create` and `room_ticket` requests only when it is not "usf4", so SF4 Ember's requests are byte for byte what they were.
- The helper passes it on (`&game=sfv` on the listing, `game` in the create and ticket bodies, again only when not "usf4"), and refuses an admission or listing whose room is of another game.
- The bridge (migration 057) records each room's game, lists one game per request (no `game` is "usf4", so every existing caller, the live website included, sees exactly what it sees now), and refuses a ticket for a room of another game (`unsupported_build`). It rates reported games, and gives a ticket's class, only in "usf4" rooms: the classes are USF4's emblems and SFV has no ranking yet.
- The supervisor's `builds.<id>.game` (default "usf4") says which game a build is; a create for another game is refused `unsupported_build`, and the room host's configuration line names a game other than "usf4".
- The website's rooms page has a Game switch: Street Fighter V's rooms come from `/rooms-sfv.json` (the bridge's `/v1/rooms/public?game=sfv`). An SFV card shows no faces (the face sheet is USF4's) and no Join or Copy link (SFV Ember opens no links yet).

So an SF4 client never lists an SFV room (other build, and no `game` is "usf4"), is refused its ticket (other build and other game), and an SF4 host never takes an SFV room; and the same the other way.

**The client.** The shell's Public rooms, Ember ID and Linked accounts screens are the library's and were already in SFV's shell (section 13); they needed only a bridge and the game key. No new shell text: the public room strings name no game. The pipe's test commands (`src/ember/PublicCommands`: `ember-id`, `bridge`, `trust`, `public list|create|join|enter`, status field `public`) stand in for those screens' buttons, as the room commands do for the room's.

**New text.** No msgid was added, and SF4 Ember's text is unchanged. SFV Ember's English overlay changes one string of its own: the header's subtitle (`brand.subtitle`) said "SFV  /  PRIVATE ROOMS & ROLLBACK NETPLAY" and now says "SFV  /  ROOMS & ROLLBACK NETPLAY" (SF4's own still reads "PRIVATE ROOMS", which was already short of its public rooms; that is the maintainer's call with the translators). The website's rooms page gains its Game buttons ("Ultra Street Fighter IV", "Street Fighter V") and an SFV card's "Join in SFV Ember: Public rooms" (the maintainer to approve with the site deploy). The room host's new `closed` reasons (`other_game`, `other_build`, `catalog_invalid`) are read by the supervisor's log only.

**Evidence** (library `feat/sfv-public-rooms` 9e2e97f85 and 4b4477c52; sfv-ember `feat/public-rooms` 996cbc0)
- Library x86 build clean. Targeted ctest (`Room|Session|Public|ProductIdentity|ProductRules|JoinLink|RuntimeCapabilities|GameCatalog|Iroh`, without HelperRust, EmberRust and the soak): 47 of 47, RoomPrivateGolden, RoomServerOwned, PublicRoomsJourney and the new RoomHostConfig cases (the game parsed, SF4's host refusing an "sfv" room) among them; ShellJourney alone passes. `scripts/probe-x64.ps1` passes. No golden or wire fixture changed.
- Rust (`cargo -j 4`): ember-protocol 122, ember-bridge 416 (the `server/ember` workspace 732), sf4-net 482, all passing; ember-rooms 63 of 64. The one failure, `ports_another_process_holds_are_skipped`, binds UDP 46801 itself before any supervisor code and is refused: on this PC UDP ports from about 44500 to 48500 cannot be bound by any process (none is listed as holding them), so it cannot pass here whatever the code; it was not run on another PC. The new tests: `tests/room_games.rs` (bridge: an SFV room is listed only for `game=sfv` and its build, an SF4 ticket request for it is refused `unsupported_build`, the public list leaves it out unless asked, its games are not rated, its ticket has no class), the supervisor's build game, the helper's paths and bodies (SF4's unchanged).
- sfv-ember `scripts/build.ps1`: 5 of 5 ctest suites, with `sfve_room_host_tests` (the embedded export takes only its own build and game, every listed pick passes, an Ultra, a handicap, a fighter, costume or colour past the pick space and a stage past the list are refused) and new `sfve_ember_tests` cases (the export reads back as the catalog's own build and rules; SFV's table rules agree with `VersusLaunch`).
- The game's own log after the move: "catalog of 45 fighters and 37 stages ... build sfv-ember/13173604/0.2/8dafe602f7615d90", the build id before it; `catalog-export` answered with the same id.
- Windows `sfve-room-host.exe` by hand: a line for another build closes `other_build` (exit 2), one with no game `other_game` (exit 2), the right build and game passes the check and stops only at its helper. Linux (WSL Ubuntu, g++ 15.2, `build-linux.sh` for SF4's host and `scripts/build-room-host-linux.sh` for SFV's, 35 and 33 files, from the working trees rather than `git archive`): both link to libc alone; `test-linux.sh` passes; SFV's host gives the same three answers, so the fingerprint computed on Linux is the game's; SF4's Linux host closes an "sfv" room `other_game` and takes a "usf4" one up to its helper.
- **Two copies in a public room** (`tools/runpublic.ps1 -lag 30 -presses 10 -games 2`): a local bridge (`ember-bridge serve`, rooms on), an ember-rooms supervisor with build `sfv-ember/13173604/0.2/8dafe602f7615d90` as game "sfv", SFV's room host, and the helper built `-Features selftest`. Each copy made its own Ember ID in its own folder (two different IDs) and trusted the bridge; copy 0 created "SFV public test" (1 round of 30 s) and entered it; copy 1 listed it ("SFV public test (1)"), took a ticket and entered. Both took a seat, picked RYU and CHUN-LI on S25, Readied and played 2 games at 30 ms: PASS, 154 checkpoints compared, 0 differing, both games decided at frame 4586 on both copies. The supervisor's listing showed the room with 2 members, both games reported (`games`, table 0, generations 1 and 2) and `opened`.
- **The same through the shell** (`runpublic.ps1 ... -shellJoin`): copy 1 joined with its virtual pad alone, Start, Online play, Public rooms, the room's card, as a player does: PASS, 154 compared, 0 differing. Shots kept in `D:\sfv-re\shots\public-rooms\`: `public-list-1.png` (SFV's Public rooms screen on the Band theme, the room "SFV public test", Classic, 1/8, "SFV 1 · US East", Near you), `public-list-focus-1.png` (its card focused), `public-room-shell-1.png` (the room, "Listed in Public", Rounds 1 / 30 sec on every table), `game1-fight-0.png` and `game2-room-1.png` (Home after the games, taken before the subtitle change below; the new subtitle was built and tested, not shown in game).
- The private room is unchanged: `tools/runroom.ps1 -presses 10 -games 2 -lag 30` PASS (154 compared, 0 differing); `tools/runshell.py` PASS.

**Deploy steps (for the maintainer's yes; nothing here was done).** In this order, so no service ever meets a peer that does not know the game:
1. **Supervisor** (`server/ember-rooms`, VPS and server1): the new binary first. An older supervisor ignores `game` and would host on the build id alone; the new one refuses a create whose game is not its build's. Every existing build reads as "usf4", so SF4's rooms are unaffected.
2. **Bridge** (staging first, then production): migration 057 adds `rooms.game` (old rows "usf4"). Listings without `game`, SF4 Ember's tickets and the website's `/rooms.json` answer as before.
3. **SFV's room host, Linux**, built in WSL from committed sources (the library's README steps 1-3, with sfv-ember's tree beside it):
   ```
   git -C <library> -c core.autocrlf=false archive --format=tar.gz -o lib.tgz <library commit> src rust server
   git -C <sfv-ember> -c core.autocrlf=false archive --format=tar.gz -o sfv.tgz <sfv-ember commit> src scripts data
   # in WSL: unpack both, then (libxxhash-dev installed, or XXHASH_INCLUDE=<folder holding xxhash.h>)
   bash sfv/scripts/build-room-host-linux.sh lib ./sfve-room-host
   (cd lib/rust/sf4-net && cargo build --profile server --locked -j 4)   # the helper, without selftest
   ```
   Check it: `ldd ./sfve-room-host` lists libc alone, and a configuration line with `"build_id":"sfv-ember/13173604/0.2/8dafe602f7615d90","game":"sfv"` and a missing helper closes `helper_unavailable` (the catalog check passed), while another build closes `other_build`.
4. **Install it under the supervisor.** `deploy/setup.sh` cannot take this build yet: its build ids are folder names matching `^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$` (no `/`), it expects a file named `sf4e-room-host`, and it rewrites each staged entry as `{room_host, helper}`, which would drop `"game": "sfv"`. Two ways, the maintainer's choice:
   - (a) Teach setup.sh a game and a config key apart from the folder name (for example `builds/sfv-13173604-8dafe602f7615d90/` holding `build_id` and `game` files beside the binaries), with `deploy/test-setup.sh` cases; then stage and run it as for SF4.
   - (b) By hand, as root: copy both binaries to `/usr/local/lib/ember-rooms/builds/sfv-13173604-8dafe602f7615d90/`, add to `/etc/ember-rooms/config.json` `"builds": { "sfv-ember/13173604/0.2/8dafe602f7615d90": { "room_host": ".../sfve-room-host", "helper": ".../sf4-net", "game": "sfv" } }`, and `systemctl reload ember-rooms` (SIGHUP applies `builds` without touching rooms). Until (a) lands, a later setup.sh run must be checked not to rewrite or prune that entry.
5. **Website** (`server/ember-short/deploy`): `rooms.html`, `assets/rooms.js` and the `/rooms-sfv.json` location (its own 5 s cache key, `rooms-sfv`), after the bridge. The location was checked by eye, not with `nginx -t`.
6. **SFV Ember's players** trust the bridge (Linked accounts, Look up `https://bridge.embernetplay.link`, Trust) or press Public rooms' one-press setup, which finds Ember's own service; a release of SFV Ember with this build is needed for any of it.

**Left**
- **Rankings and classes for SFV.** SFV rooms' games are not rated and their tickets carry no class: the ratings and the class emblems are USF4's. A per-game rating and SFV's own emblems are a later decision.
- **The listing's faces.** A room host reports its members' profile mains (`details.fighters`); SFV Ember sets no main, so SFV's Public rooms cards show fighter 0 (RYU) for everyone, where its room shows picks (section 20). The bridge also drops `fighters` above id 63 (`FIGHTER_LIMIT`); SFV has 45.
- **setup.sh** (deploy step 4), and links: SFV Ember registers no `ember-sfv://` handler, so the website's SFV cards have no Join or Copy link.
- **Table rules SFV cannot play.** The room host takes a table's rules by `room::ValidRoundCount/Time` (15 and 99 rounds, 9999 s), as a private room's host does; a modified client could set them, and both copies' launch would then refuse the game (`LaunchPicks`). A check against the catalog's playable lists (`PlayableRoundCount/Time`) in the room's rule test would change nothing for SF4, whose catalog plays every rule, but it is a change to a shared header and was not made here.
- **Merging.** `ProductIdentity::roomGame` is a new field after `urlScheme` in a positional table: sfv-ember's `main` (with section 22's launcher names) needs `"sfv",` after `"ember-sfv",` in `SfvProduct.cxx` when it meets this library branch, which must land together with this branch.
- **A new game data build** (an SFV patch, or a match version) changes the build id: it needs a new export, a new room host and a new supervisor entry, as an SF4 release does.
- **This PC:** UDP 44500-48500 cannot be bound (the supervisor's default 45800-45899 among them), so `runpublic.ps1` gives its supervisor 52100-52199, and the supervisor's `ports_another_process_holds_are_skipped` test fails here.

## 25. Watching a table's games (2026-11-07)

The maintainer: spectating "pretty much up to parity with usf4 ember in all functions". A room member who is not seated can Watch a table; their copy launches the same battle (picks, stage, rules, seed) and plays it from the fighters' confirmed inputs, never sending any. `Capabilities::spectators` is now on for SFV, so the room board offers Watch and Stop watching and the runtime enters the games a member watches.

**What SF4 Ember does, and what SFV reuses unchanged.** Everything above the battle is the library's and game-neutral, and SFV takes it as it is:
- The room: Watch puts the member in the table's `spectators`, or in `watchingNext` while a game is in play, which moves into `spectators` when that game's match closes (`RoomModelActions.cxx`, `RoomModel.cxx`). So a member who watches during a game watches from the next one, as in SF4; a set going on in place is one match, so it is watched from the match after it. Stop watching is `Unwatch`, scoped to the watched generation (`LeaveWatchedGame`).
- The match session: watchers are participants from slot 2, each with a loopback link to P1 only. The start barrier is the two fighters (`spectators_optional`); P1 waits at most `IrohMatchSession::SpectatorGraceMs` for watcher links, and one whose link is not up has no port and sits the game out. A watcher not at the main menu within the sync deadline sits the game out (`ObserveDeferredEntry`).
- The runtime: a watcher's battle is entered as a fighter's is (`Battle::Enter`); its stream's failure (`NotifyRuntimeSpectatorStreamFailed`), P1's drop of a watcher (`EndRuntimeSpectatorStream`), a watched game going on in place (`NotifyRuntimeWatchedGame`), a finished stream's exit (`PollSpectatorExit`), P1's drain of its watchers after the battle (`NotifyMatchEnded`, `TickFrame`), the HUD's spectator fields and the watcher count. A watcher reports no result (`NotifyRuntimeMatchResult` ignores it) and gets no post-match prompt (`RematchTable` excludes it).
- The policies, as SF4's GGPO layer uses them: `SpectatorPolicy` (P1 drops a watcher not synchronized 5 s after the fighters, or one 32 frames behind on five samples or 48 at once), `SpectatorLinks`, `SpectatorStreamWatch` (a watcher's stream that has not run in 30 s ends) and `SpectatorCatchUp` (a watcher behind its stream plays up to 3 extra frames an update).
- GGPO: SFV's port already carries every spectator patch SF4's has (handle control, input-gap and pending-output disconnects, catch-up backlog, poll capacity).

What was SF4-only is the battle's GGPO layer (`sf4e__Game__Battle__System__Ggpo*.cxx`), which SFV's `RollbackSession` and `RoomBattle` now do the same way.

**The rollback session** (`core/RollbackSession`)
- **P1 streams.** `Config::watchers` lists each watcher's slot and port; only P1 adds them, as GGPO spectator players after both fighters, each with its own handle (`spectator-handle-control.patch`). One GGPO cannot add sits the game out. The disconnect settings are now set before any player is added: GGPO gives a spectator's link the settings current when it is added (`AddSpectator`, p2p.cpp) and later changes reach only the fighters' links, so a watcher's link takes the fighters' timeout.
- **Events by handle.** A handle that is neither fighter's is a watcher's (SF4's `IsSpectatorHandle`). Its `DISCONNECTED_FROM_PEER` drops it from the policy and notes its slot; its interruption and return are logged only. Neither touches the gate's warning, the countdown or the session's failure. Before, any disconnect failed the session as "the other player disconnected". A watcher's `SYNCHRONIZED_WITH_PEER` marks it synchronized; a fighter's starts the watchers' deadline; `RUNNING` starts the policy's backlog samples.
- **Drops.** `PollWatchers` runs in every `Step`, `StepHeld` and `Poll`, after GGPO's poll and never inside a callback: `SyncOverdue` and the backlog `Sample` (GGPO's `send_queue_len` and ping) go to `ggpo_disconnect_player`. `TakeDroppedWatchers` hands each dropped slot over once; `RoomBattle::EndDroppedWatcherLinks` passes it to `EndRuntimeSpectatorStream` each frame, so the watcher's copy gives up at once.
- **A watching copy** starts GGPO's spectator backend (`ggpo_start_spectating`, one link to P1, no delay or disconnect settings, which it refuses). Its `Step` plays the next frame P1 sent: `synchronize_input` answers `PREDICTION_THRESHOLD` for a frame not here yet (Waiting) and `GENERAL_FAILURE` for one overwritten before it was played (the stream fails). The frame's inputs reach the game exactly as the fighters' sessions give them (`InputsMatter`, `InputBits`), without telling GGPO anything. It keeps no saves and never rolls back.
- **Its hashes.** The fighters hash the state GGPO saves as frame N, the state after N frames, frame 0 on their first input. The watcher hashes the same states: frame 0 before its first frame runs, then frame N after its Nth, through the same `RecordHash` (one scratch copy, `SaveTo`, `HashSaved(Peers)`, the region hashes and the same `checkpoint:` log line). `LastConfirmedFrame` is its current frame (every state it reaches is final), so the desync check (`SfvVerification`) takes its checkpoints and compares them with the fighters' as SF4's spectator does: a mismatch is logged and the spectator-only notice shown, and nothing ends the fight.
- **Its held frames.** `StepHeld` on a watcher plays the next streamed frame as a held one: the fighters' answer bits reach `onInputs` and the game does not advance, so the decision (`Decide`, a function of the two inputs and the held count) is the fighters'.
- **Kept for the watchers.** `KeepForWatchers` marks a session whose fight is over; the other fighter's interruption and disconnect are then expected and recorded as nothing.
- `SpectatorStreamWatch`: a watcher's stream that has not run 30 s after its start fails.

**The room's battle** (`ember/RoomBattle`, `mod/NetplayMode`)
- **The start.** `SessionConfig` takes any slot of the match: 0 and 1 fight, 2 and up watch (it said "SFV's session has no spectators" before and left any other slot). P1 lists every watcher whose link has a port (`Config::watchers`) and the table's sync deadline (`SessionClient::SpectatorSyncDeadlineMs`). A watcher's session is P1's stream (`remotePorts[0]`); with no port P1 left it out (its link was not up at the start), and it watches the next game, with SF4's "stream lost" notice unless the room already has the game's end. Its HUD keeps the delay the fighters were granted (`room::WatchedGameDelay`), as SF4's does. It binds no pad: it sends nothing.
- **Its frames.** `NetplayMode::WatchFrames` plays the next streamed frame and, while the backlog after it is past the reserve, up to three more (`SpectatorCatchUp::ExtraFrames`). Each goes through the same decisions a fighter's frame does: a pending match end is taken on the frame it is seen (every watcher frame is confirmed, so it is the fighters' frame), the WIN is held, its held frames read the fighters' answers and decide as theirs, a rematch goes on in place and reseeds as theirs, a decline goes back to the main menu; the start time is kept as theirs. Looping the whole decision, not the session alone, matters: the match end's hook fires inside a frame, and the frame after it must be held, as the fighters hold it.
- **What it reports.** No result (the fighters report it), no answer, no prompt (`ResultHeld` is false for it). A game that goes on in place is counted for its HUD (`NotifyRuntimeWatchedGame`). Its own stream failing before the room has the game's end is `NotifyRuntimeSpectatorStreamFailed` plus the "stream lost" notice: it keeps its place at the table and leaves this game. A watcher who left (Stop watching) closed its session with no failure, and hears nothing of it.
- **P1 after the battle.** `RoomBattle::Close` now tells the runtime of the match's end before the session goes (SF4's `CloseBattle` order): `NotifyMatchEnded` asks `Battle::SpectatorStreams` and, with watchers still streaming, the runtime drains them (`SetDrainingSpectators`, at most 10 s). The session is then kept (`KeepForWatchers`), moved out of the battle into `NetplayMode`'s `drain_`, and polled every tick (`PollBattle`) so the watchers get what they still miss. The adapter's battle stays Live meanwhile (the runtime holds the room's receipt, so the next game cannot take the port), and the runtime's `Retire` (every watcher gone, or the 10 s), the drain's end by any other path (`SetDrainingSpectators(false)`: the next battle, a shutdown) or an `Abort`/`Fail` with no battle in play close it. The next battle closes one still kept before it binds.

**The adapter** (`SfvGame`). `Capabilities::spectators` is on. `Sample` gives a watcher's `spectator`, `spectatorBacklog`, the granted delay and own-delays flag, no ping, and `localFighter` false; `PollTelemetry` takes no ping for it (SF4's `MatchTelemetry` keeps none for a spectator). `SpectatorStreams`, `DrainingSpectators`, `SetDrainingSpectators` and `SpectatorBacklog` are SF4's, from the battle's session or the one kept after it. `SetWatcherDrain` names the kept session.

**Test commands and tools.**
- The room commands gain `watchtable [table]` (the room board's Watch: Watch at that table, fenced by the room and table revisions as `sit` is) and `stopwatching` (the shell's Stop watching: a command carrying the snapshot's `watchedGame`). The room status gains `watching` (the table, whether from the next game, the game watched, the table's score, generation and phase) and `spectators` (each table's watcher count). The battle status's `link` gains `watching`, `backlog`, `watchers` and `keptForWatchers`.
- `tools/runroom.ps1 -watchers 1`: copy 2 joins and watches table 0 before the first Ready; every game must be watched, every WIN decided on the fighters' frame, and copy 2's checkpoints are compared with copy 0's of the same game and frame (`watcher checkpoints: N compared, M differing`). `-watchLate` watches only once the first game's fight is on, so copy 2 must watch from the second; `-watcherLeaves N` sends Stop watching N seconds into the first watched game and checks that both fighters' sessions are still live and that copy 2 is back in the room watching nothing.
- `tests/WatcherTests.cxx` (in `sfve_tests`): two fighters and a watcher over loopback, on MockFighter. The watcher's hash equals both fighters' at every frame from 0 (input delay 1 and 3) and every region of a checkpoint; over a held WIN all three decide on the same frame, rematch (and stay equal for 300 frames after, the frozen frames included) or go back; a watcher that goes away is dropped after P1's disconnect timeout with no warning or failure on either fighter, its slot handed over once; a watcher that never comes is dropped at its deadline and the fight starts; a session kept for its watchers ignores the other fighter leaving while the watcher plays out what P1 had confirmed. `EmberAdapterTests` checks the capability, the adapter with no battle, and a kept session as the battle's until the runtime retires it or its drain ends.

**No new address.** Nothing here reads the game beyond what the fighters' path already does; the watcher's battle is launched, hooked, held and restarted by the same modules (sections 12, 16, 19) and their BuildCheck-verified sites.

**The library is unchanged.** Every runtime, room and session piece a watcher needs was already game-neutral; `feat/sfv-spectators` (from 711398482) has no commit, and sfv-ember's build compiles the library's x64 runtime from that tree (its `ember_runtime` suite passes). SF4's behaviour, wire fixtures and goldens are untouched, and no SF4-visible text changed.

**Evidence** (three copies on this PC at 960x540, 30 ms lag, input delay 2, 1 round of 30 s, RYU vs CNL on S25, seeded mash; copy 2 watches)
- `tools\runroom.ps1 -presses 10 -games 2 -lag 30 -watchers 1`: PASS. P1 logged `1 watcher(s)` and "the watcher with handle 1000 is synchronized" 0.9 s after its session started, as it reached RUNNING; copy 2's stream ran 0.4 s after its start. Both games were watched; every game's end was taken at frame 4586 and its WIN decided at frame 4589 on all three copies. Fighters: 154 checkpoints compared, 0 differing; watcher: 154 compared with the fighters' (frame 0 included), 0 differing. Copy 2 played about 60 ms behind P1 (its checkpoint lines 55-70 ms after P1's). After each game P1 kept its session for the watcher ("deferring GGPO close for 1 spectator streams") until the watcher's link went quiet, 5.0 s later, and the runtime closed it; the next game started as usual.
- `tools\runroom.ps1 -rematches 1 -lag 30 -watchers 1`: PASS. The first WIN went on in place at frame 4816 after 229 held frames on all three copies (copy 2 reseeded and logged "Spectator: game ended in place, HUD score 0-1"); the second went back to the room at frame 8941 after 228 held frames on all three. 150 checkpoints compared, 0 differing; watcher 150 compared, 0 differing (the frozen WINs and the game after the restart included).
- `tools\runroom.ps1 -games 2 -lag 30 -watchers 1 -watcherLeaves 15`: PASS. Copy 2 sent Stop watching 16 s into game 1 ("Leaving spectator view."), closed its session and was back in the room 0.7 s later, watching nothing; P1 logged its watcher not answering, dropped it 0.9 s after the leave ("too far behind", 60 frames unacknowledged), and played on with no warning. Both fighters' sessions stayed live; game 2 started with 0 watchers. Fighters 154 compared, 0 differing; watcher 16 compared (until it left), 0 differing.
- `tools\runroom.ps1 -games 2 -lag 30 -watchers 1 -watchLate`: PASS. Copy 2 pressed Watch 20 s into game 1 and the room listed it to watch from the next game (`watchingNext`), as SF4's does; it sat game 1 out at the main menu and watched game 2 from its start. Fighters 154 compared, 0 differing; watcher 77 compared with game 2's, 0 differing.
- The shell's own Stop watching (`runroom.ps1 -games 1 -lag 30 -watchers 1 -keep`, then copy 2's virtual pad): Start 15 s into the watched fight opened Ember over it on the room page with the focus on Stop watching; A asked to confirm ("Stop watching? ... The fighters keep playing."), Right and A confirmed. Copy 2 was back in the room 0.7 s later, watching nothing; P1's session stayed live (frame 2443, 0 watchers). 77 fighter checkpoints compared, 0 differing; watcher 38 compared until it left, 0 differing. (The run's own last line failed on the watcher's missing result line, which the tool did not expect after a leave it had not sent; `runroom.ps1` now prints that line only when there is one.) Shots `final\watcher-shell-over-fight.png`, `final\watcher-stop-watching-confirm.png`, `final\watcher-after-stop-watching.png`.
- Without a watcher: `tools\runroom.ps1 -presses 10 -games 2 -lag 30` PASS, 154 compared, 0 differing. `tools\runclose.py` PASS (the host's graceful leave done 281 ms before the engine's exit; the guest saw one member and closed from its battle in 2.3 s). `tools\runshell.py` PASS.
- sfv-ember `scripts/build.ps1`: 4 of 4 ctest suites; the five `WatcherTests` cases (2,808 assertions) and the adapter's new case among them.
- Shots (`D:\sfv-re\shots\spectators\`): `final\game1-fight-2.png`, the watcher's view of the fight with SFV's plates named "SFV 1"/"SFV 2" and Ember's strip reading "0 - 0 | Delay 2f | Behind 1f | Spectating | Watching 1", as SF4's spectator strip reads; `final\game1-fight-0.png`, P1's view at the same moment ("Ping 82 ms | Rollback 1f | Delay 2f | Watching 1"); `final\game1-room-2.png`, the watcher back in the room ("0 playing · 1 watching", Table 1 "Watching 1"); `late\game1-fight-2.png` (the late watcher at the main menu during game 1) and `late\game2-fight-2.png` (its view of game 2).
- `tools/sfvelib.py` captured only the top-left 80% of a window at 125% display scaling (a process the system scales reads a scaled client size while PrintWindow draws real pixels): the capture is now DPI-aware, and its shots are the whole window.

**Left**
- A watcher's own disconnect (its process killed rather than leaving) was not run in game; P1's side of it is the same GGPO disconnect `WatcherTests` covers (dropped after the fighters' disconnect timeout, no warning), and the backlog policy drops it sooner, as the leave run shows.
- A watcher P1 drops for its sync deadline (a slow load) was not seen in game: in every run copy 2 synchronized before P1 reached RUNNING. The loopback test covers the drop and the fight's start.
- More than one watcher (the room allows up to 14) was not run: this PC runs three copies at most. Nothing in the code counts watchers.
- After a game P1 holds its session for its watchers about 5 s (until their links go quiet, at most 10 s), during which the room's receipt stays open, as in SF4. A watcher's copy closing its stream at once and P1's disconnect timeout set the 5 s.

## 26. Replays (2026-11-07)

The maintainer: SFV Ember "pretty much up to parity with usf4 ember in all functions"; replays are this section. Library `feat/sfv-replays` (from 5817bf9df); sfv-ember `feat/replays` (from 5649b6f).

**What SF4 Ember's replays do for a player** (read from the library: `sf4e__ReplayStore`, `sf4e__ReplayPlayback`, `platform/ReplayFiles`, `ui/ReplaysPanel`, `docs/design/REPLAY_PLAYBACK_CONTROLS.md` and `REPLAY_ARCHIVE.md`)
- USF4 records every Versus game itself into its save slots (a ring of the last 10, 300-309, and 300 hand-saved slots). Ember copies each into its own archive (`%APPDATA%\sf4e\replays`, `.emberreplay`: a record plus the game's `#BRP` replay) as the game saves it, with the room's names for both sides and whether this PC played or watched it. The archive keeps every game; the player's own mark is Keep.
- Home › Replays lists the archive newest first: the players, the score, the fighters' portraits, the date, the Seen, Spectated, Video and Kept tags, Show and Search, a Sets tab and an Export video tab. A row's Select offers Watch now, Watch with frame meter, Add to the game's list, Export video, Inputs and stats, Keep and Show in folder; the screen also has Open the Battle Log and Open the replay folder.
- Watch now puts the replay back into USF4's own list and plays it in the game's Battle Log. Ember's menu steps aside and comes back on Replays at the main menu. Over the playback, the transport (`common/ReplayTransport.hxx`): F1 or RB pause, F2 or RT step, F3 and F4 or LB speed (1, 1/2, 1/4), F5 the frame meter, F9 or LT the input lanes; the game's own pause menu leaves.
- Beyond the PC: a tournament set's games go to its bridge; with Share my replays, games go to the public archive (archive.embernetplay.link); Export video encodes an .mp4 from the game's Direct3D 9 picture.

**SFV has no offline replay of its own to reuse.** SFV's battle has a replay unit (`ReplayUnit`, `kReplayUnitType`; its tick counters are a masked region of the rollback state, `SfvOffsets.hxx`), but SFV's replays are its online service's (Capcom Fighters Network), which SFV Ember never contacts, and how the recorder stores or plays a replay offline has not been read. Rather than reverse-engineer that, SFV Ember records at the level it already owns and has proven deterministic: the rollback session's confirmed frames, which section 25's watchers already play back state for state.

**The recording** (`core/ReplayRecorder.hxx`, `core/ReplayFile.hxx`, `mod/NetplayMode`)
- Every RollbackSession reports each frame it plays (`Config::onFrame`) as a `RecordedFrame`: an advanced frame with the inputs the game was given (`InputsForThisFrame`: the pad's bits, or neutral where the frame ignores inputs) and whether it read them (`kFight`); a held frame (the WIN, section 19) with both fighters' confirmed inputs, every bit, which carry the answers (`kHeld`). A rollback's replay of a frame reports it again. Checkpoints are reported with their peers' hash (`Config::onCheckpoint`), the "checkpoint:" line's.
- The netplay mode keeps the last report of each frame (`replay::Recorder`) and, after every step, marks final the frames both played and confirmed (`min(Frame(), LastConfirmedFrame() + 1)`). GGPO rolls a misprediction back before it moves its confirmed frame on (`Peer2PeerBackend::DoPoll` in the port's p2p.cpp: `_sync.CheckSimulation` at line 110, which rolls back through `AdjustSimulation`, then `_sync.SetLastConfirmedFrame` at line 144), so a final frame's last report is of its confirmed inputs; and the fighters' and the watcher's recordings agree frame for frame (`ReplayTests`, and both copies' files in game). A frame reported out of order (a gap) breaks the recording, and nothing is saved.
- Each game's end is noted as it is taken (`DecidedMatchResult`: decided, winner, rounds won, rounds judged, best of, its frame). The header holds the build id (`MatchBuildId`), the room's names for both sides (`matchSides`, as SF4's `NoteMatchStart` takes them), both picks in Ember's catalog numbers (the list's portraits) and as launched (the fighter's code, the game's costume and colour numbers), the stage, rounds, round time, the room's seed, this copy's slot and delay, and whether the last WIN went back (a whole battle) or the battle was cut short.
- One file per battle, not per game: a game that goes on in place starts from the WIN of the one before it, in the same battle and session (section 19), so its frames only mean something after that game's. The file is written as the battle closes at the main menu (`SaveReplay`, durably: `durable::PublishReplace`), named `YYYYMMDD-HHMMSS-<hash>.sfvreplay` (its start in UTC, as SF4 names its archive's files by time and CRC), in `%APPDATA%\SFVEmber\replays`. Fighters and watchers each save their own; a watcher's is Spectated.
- The format: the magic `SFVEREPL`, version 1, a JSON header, the frames run-length coded, the checkpoints, and an XXH64 of everything before it. `Decode` refuses another magic or version, a size past the file or past the limits (2,000,000 frames, 64 MB), a header field missing or out of range, unknown frame flags, checkpoints out of order or past the frames, bytes after them, and a hash that does not match.

**The playback** (`RollbackSession::Config::replay`, `ember/RoomBattle::BindReplay`, `ember/SfvReplays`)
- Watch now (`SfvReplays::Start`) reads the file, checks that it decodes, that this copy's build id is the recording's and that the catalog can launch its picks, stage and rules, arms it, and launches the battle from the main menu as a room's match is launched (`QueueLaunch` with `MatchPicks::replay`: the game's ownership check never runs for it, as for `linked`). The battle that launch loads binds as the replay's (`TakeReplayBattle`, `TakeArmedReplay`).
- The replay's battle is played as a watcher plays the room's (section 25), through the same code. `RoomBattle::BindReplay` makes it a watching battle that reports nothing to the runtime or the room. Its session plays frame N from `replay[N]` in place of P1's stream (`StepWatched`), with no GGPO session and no network. The match end is taken on its frame, the WIN is held, the held frames' recorded answers are decided by `rematch_in_place::Decide`, a rematch restarts in place and reseeds from the recorded seed (`RoomBattle::HeldFrame`), and the last decline goes back to the main menu. The session's random generators and the match's start time are seeded from the recorded seed, as the fighters' were.
- Each frame is checked against the recording as it plays: the game decides from its own state whether a frame is held or reads inputs, and a frame whose kind is not the recorded one has left the recording (`ReplayInputs`: the session fails and the battle goes back). Each checkpoint the playback reaches is compared with the recording's (`NotePlaybackCheckpoint`; a difference is logged as a desync). The end is logged: why it ended, the frames played of the recording's, the checkpoints compared and how many differed.
- Ember's menu closes as the replay's battle is launched (`logOpens`) and comes back on Replays when the main menu does (`returns`), as SF4's overlay does around the Battle Log (`SfvShell`). The replay is marked Seen (`watched.txt`). One that did not play to its end says so on Replays.

**Controls.** Ember steps every frame of the replay's battle itself, so the transport gates whole frames: `PlaybackFrameWanted` takes the commands and asks the library's `replaytransport::Transport::Hold()` once per rendered frame, and a held frame is simply not stepped (the battle stands still, as when a fighter's session waits). Pause, step (one frame a press, up to 30 queued), 1/2 and 1/4 speed are the library's transport unchanged: the pad's RB, RT and LB through `PadControls` (from VirtualPads' record of the pad, which now carries the triggers), the keyboard's F1 to F4 read as SF4's overlay reads them, and View or Esc to stop watching. Unlike SF4's, the controls act in every part of the battle, not only the fight, by construction (SF4's left the knockout and the round's end to the game's own cadence; SFV's battle has no cadence but Ember's); the runs exercised them in the fight. During a playback the shell hides every pad from the game (the battle's inputs are the recording's, and SFV's own pause menu has no place over it) and keeps the playback keys from it (`OverlayUi::takesKey`). The strip is the library's (`DrawReplayStrip`), shown for three seconds after each control and as the playback starts, and while paused or slower. It says where the playback is: the game's round (the stretches of fight frames since its game began) and the time into it.

**The Replays screen** is the library's, through `SfvReplays` (the adapter's `Replays`). A listing thread reads the folder at most every two seconds, reads a file again only when its size or time changed, and lists only files that decode whole (hash included), newest first: the names, the fighters for the portraits, the date, Spectated for a watched battle, Seen and Kept, and the score. A battle of one game shows its rounds (as SF4's game does); a battle of games in place shows the games each side won; "Rounds N · m:ss" counts the rounds judged and the fight's frames. Keep and Seen are `kept.txt` and `watched.txt` beside the replays, in SF4's list format (`platform/ReplayNotes.hxx`). Show in folder and Open the replay folder are the library's services over `ArchivePath()`.

**Library change (SF4 unchanged)**
- `Capabilities` gains `replayGameList`, `replayVideo`, `replayMeter` and `replayInputs`, which SF4 sets (and so does the tests' fake game). `ShellView` carries them, true by default, and `ReplaysPanel` offers only what the game does: the Battle Log row, Add, Export video with its tab and a set's videos, Watch with frame meter, and Inputs. SF4's rows, choices and their order are as before (the journey test's existing checks pass unchanged); `WatchOnlyJourney` checks SFV's.
- `ReplayHudView` gains `meterOffered` (true by default: SF4's F5 stays) and `stopOffered` (View and Esc, with the shared "room.stop_watching" words).
- The strip asked the Training meter for its place by calling `TrainingMeterBottom()`, which SF4's TrainingMeter.cxx defines and SFV does not link. The meter now hands it over as it starts (`SetReplayStripMeterBottom`, in TrainingMeter.cxx), so SF4's strip reads the same function as before; with none, the strip keeps below the meter's default place (`TrainingHudBottom`). SF4's render goldens pass, but they cannot tell the registration from the fallback: at the meter's default position the two agree (TrainingMeter.cxx's own `static_assert`), so that the registration takes effect is shown by the code, not by a test.
- `feat/ember-library` (ec04b962f, Training's `Capabilities::frameMeter`) is merged into `feat/sfv-replays`; the two capability lists sit side by side in `Capabilities`, `ShellView` and `ShellHost`.
- No SF4-visible text changed and no msgid was added. SFV's English overlay rewords the Replays lines that name SF4's Battle Log, and gives two notices SFV never shows otherwise their SFV meaning: a replay that cannot be played (`inputs.unreadable_detail`) and one that did not play to its end (`replays.not_added`).

**What does not apply to SFV yet, and why**
- Add to the game's list and Open the Battle Log: SFV keeps no offline replay list.
- Export video: SF4's export reads the game's Direct3D 9 picture (`platform/FrameGrab`); SFV draws with Direct3D 11. The encoder (`VideoEncoder`) is neutral; a Direct3D 11 frame grab is what is missing.
- Watch with frame meter, the input lanes, Inputs and stats: SFV has no Ember frame meter (section 27, D: it needs a reader of SFV's fighter state), and SFV's input bits are known only as pad bits (`GameInput.hxx`: observed from pad buttons on a default layout), never verified as LP to HK; lanes and counts that name buttons would claim what is not verified.
- Sharing (the public archive, a set's bridge, YouTube): the bridge and the archive take only USF4's `#BRP` replays (`replays::Replay::parse`), and an SFV replay needs a server-side format and game identity of its own. Nothing was deployed and nothing was called against production. The maintainer's call.
- Replay links and set packs: SF4's; not offered for SFV.
- SF4's archive copies every Versus game, offline ones too; SFV Ember records room battles, fought or watched: an offline battle has no session to record.

**Tests and tools**
- `tests/ReplayTests.cxx` (`sfve_tests`): the file reads back as written and refuses a byte changed anywhere, a cut file, another magic or version, checkpoints past the frames and frames of no known kind; the recorder keeps a frame's last report and only frames made final, and breaks on a gap; two fighters and a watcher over loopback (input delay 1 and 3, frames that ignore inputs, a held WIN answered 15 and 40 held frames in, a rematch in place and 250 frames after it) record the same frames and checkpoints, and a fresh MockFighter playing P1's recording reaches every state the fighters did, frame by frame (frame 0 and the frozen WIN included), with the recording's checkpoints, and reports the same frames back; a playback that holds where the battle was played, decides its WIN early, or reads inputs where the recording ignored them fails at that frame; one past the end fails as "the replay ends".
- `tests/SfvReplaysTests.cxx` (`sfve_ember_tests`): two battles saved to a scratch folder (`SFVE_REPLAY_DIR`) are listed newest first with their names, fighters, date, provenance and score (one game's rounds; two games' games won), a damaged file is not listed, Keep goes on and off, a file outside the folder is never marked, and Watch now refuses outside the main menu, in a room, another build's replay and a damaged file, saying so and launching nothing; what SFV does not offer (Add) is refused. `EmberAdapterTests` checks the capabilities.
- `tools/runreplay.py [--rematches N] [--shots DIR]`: two copies play a room's battle (seeded mash, 30 ms lag, input delay 2), each saving its replay into its own scratch folder; both leave the room; copy 0 opens Ember with its virtual pad, goes to Replays, opens the newest row's choices and picks Watch now; copy 1 asks the pipe (`replay watch latest`). During copy 0's playback the pipe pauses it in the intro (before any fight frame) and 20 s in (no frame may play while paused), steps three frames (exactly three), and slows it to 1/2 and back to 1x. Both playbacks must play every recorded frame; the mod's own comparison must find 0 differing; the playback's "checkpoint:" lines must equal the same copy's lines of the battle; and the two copies' files must hold the same frames and checkpoints. The pipe gains `replays` (the listing), `replay watch|keep|pause|step|slower|faster|stop` and the `replays` status field.

**Evidence** (this PC, two copies at 960x540, 1 round of 30 s, RYU vs CHUN-LI on S25, seeded mash, 30 ms lag, input delay 2)
- sfv-ember 3392e47 (main merged, df54116) on library de798dc13 (feat/ember-library ec04b962f merged):
- `python tools/runreplay.py --shots D:\sfv-re\shots\replays`: PASS. Each copy saved its battle: 4589 frames, 1 game, 77 checkpoints, complete, 5464 bytes. Copy 0's playback through the shell and copy 1's through the pipe each ended "the recording's last WIN went back; 4589 of 4589 frame(s) played, 77 checkpoint(s) compared with the recording, 0 differing", and their checkpoint lines against the battle's: 77 compared, 0 differing on each. The two files hold the same 4589 frames and 77 checkpoints. The transport: paused in the intro at frame 90, before any fight frame, nothing played; paused 20 s in, 0 frames in 1.5 s; three steps, 3 frames; 1/2 speed, 60 frames in 2 s; 1x, 120 frames in 2 s.
- `python tools/runreplay.py --rematches 1`: PASS. One battle of two games, each to P2 (frames 4586 and 8530); over the first WIN both answered Rematch, over the second copy 1 declined. Each file: 8557 frames, 2 games, 143 checkpoints, complete, 10326 bytes, the same on both copies. Both playbacks held the first WIN, went on in place on the recorded frame with the recorded seed, held the second and went back on the recording's last frame: 8557 of 8557 frames, 143 checkpoints compared with the recording, 0 differing; checkpoint lines 143 compared, 0 differing on each copy.
- `tools\runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, 154 checkpoints compared, 0 differing. `python tools/runshell.py`: PASS.
- sfv-ember ctest: 5 of 5 suites (`sfve_tests` with the replay cases, `ember_runtime`, `sfve_ember_tests` with the adapter's and Training's, `sfve_room_host_tests`, `guard_probe`).
- Library x86 (SF4 unchanged): targeted ctest 23 of 23: ShellJourney (with `WatchOnlyJourney`), UiRender, UiRenderLocales, UiRenderGallery and UiRenderAtlas (render goldens unchanged), RoomPrivateGolden (wire goldens), ReplayTransport, ReplayInputLane, ReplayInputs, ReplayScreenText, ReplayRecorder, ReplayFiles, ReplaySlots, InPlaceReplays, RuntimeCapabilities, PresentationSnapshot, GameCatalog, Localization, OverlayPresentation and the Watch, Identity, PublicRooms and Rankings journeys. `scripts/probe-x64.ps1`: passes, 442 neutral units built with SFV's flags, and the runtime's test under the fake game (a first attempt stopped in its vcpkg install, which passed when run again).
- Before the merge of main (Training, section 27), on b79d8ab and library a83a1be4d, the same four runs passed as well: runreplay 4589 of 4589 frames, 77 checkpoints, 0 differing on both copies; `--rematches 1` 8554 of 8554 frames, 143 checkpoints, 0 differing on both; runroom 154 compared, 0 differing; runshell PASS.
- Shots (`D:\sfv-re\shots\replays\`): `1-replays-list.png` (Replays over the main menu: the battle's card "SFV 1 0-1 SFV 2", RYU vs CHUN-LI, Rounds 1 · 0:30, its pane with Watch now; no Battle Log row), `2-replay-choices.png` (the row's three choices: Watch now, Keep, Show in folder), `3-playback-paused.png` (paused 20 s in: the strip with Play, Step, Speed 1x, Stop watching, "Round 1 · Frame 282"), `4-playback-half.png` (at 1/2: Pause and Speed 1/2x lit, the clock into the round), `5-replays-after-playback.png` (back on Replays after the playback, the card tagged Seen).

**Left, and the maintainer's calls**
- Sharing SFV replays (the public archive, a set's bridge, YouTube): needs a server-side SFV replay format and game identity (the bridge's `replays::Replay::parse` takes `#BRP` only). Nothing deployed, nothing called. Settings still shows SF4's "Share my replays" row on SFV, where it does nothing; hiding it is a capability like `frameMeter` (the maintainer's call whether to add it).
- Export video: a Direct3D 11 frame grab for the neutral encoder.
- The frame meter over a replay, its input lanes and Inputs and stats: they wait for a reader of SFV's fighter state (section 27, D) and for SFV's input bits verified as buttons.
- A battle the game closed in the middle (the process ending) is not saved: the file is written as the battle closes at the main menu. A battle whose link drops is saved to its last confirmed frame and marked cut short, and its playback plays to there and goes back ("the replay ends at frame N"); not run in game (`ReplayTests` covers the end of the frames).
- A watcher's recording (Spectated) is covered by the loopback test (its frames equal the fighters'), not run in game; it honours "Save matches you watch", as SF4's does. A Practice room's battle is not recorded, as SF4 records no practice battle.
- Nothing is pruned, as in SF4 Ember's archive; Keep marks one to find again.
- `SFVE_REPLAY_DIR` moves the replay folder for the tests and tools only.

## 27. Training (2026-11-07)

The maintainer: SFV Ember "pretty much up to parity with usf4 ember in all functions". SF4 Ember's Training is five things for a player:
- **A.** Training straight from Ember's Home: Ember's Training select (the player's fighter, the dummy, the stage), whose Start loads the game's Training battle.
- **B.** Waiting in Training while in a room (`TRAINING_IN_ROOMS.md` in the library): the room shows the member "In Training"; a fighter who sits down opposite calls them back, with a banner and a countdown, and the battle leaves for the main menu, where Ember opens on their table with 15 s to ready; go now cuts the countdown short; not readying gives the seat up.
- **C.** Practice rooms (`TRAINING_OVER_ROLLBACK.md`, step 2): a private room whose every game is a Practice match over rollback: health and gauges refill, nobody is knocked out, the longest round time, and End practice from Ember over the fight.
- **D.** The frame meter: since 1.5.2 a fighter never sees it in a match they fight, except in a Practice room, where both do; watchers see it with Settings' Match frame meter.
- **E.** The offline lab over SF4's Training: the F6 controls, dummy recording, playback and replies, position save and reset, hitboxes, the Data panel, combo check, the training pad binding.

SFV has a Training mode of its own with E's substance built in: its pause menu (`1P PAUSE MENU`, shot `solo-6-pause-menu.png`) has recording and playback (`TrainingPauseMenuRecord/Play`), dummy settings, Attack Data, Frame Advantage in Color, Key Display, Restart Battle and position reset. So SFV Ember drives SFV's own Training battle for A and B and leaves E to the game, as the maintainer's brief for this work asks ("prefer driving SFV's own training battle and settings over rebuilding them"). C and B's room side, the call and its rules are the library's and game-neutral already. D has no SFV meter to show (below).

### The game side, from our IDA database

| What | Where | Evidence |
|---|---|---|
| `TrainingBattleState`, the jump's target 2 | vtable rva `0x2DA8B70` (`sfv::scene::kTrainingBattleVtable`) | Its class getter (rva `0xC22130`) registers `L"TrainingBattleState"` through rva `0xBF3B20` under `FightRequestBattleStateBase`, instance size `0x458`, with the constructor thunk rva `0xC0F4E0` → rva `0x6606D0`, which writes this vtable. The same path for `VersusBattleState` (rva `0xBF42E0` → `0xC0F5C0` → `0x660780`) gives `0x2DA9918`, the vtable the mod already used, so the method is checked against a known answer. |
| Leaving a Training battle for the main menu | the pause menu's command handler, rva `0x66A310`, at the vtable's `+0x5E0` (the only vtable that holds it) | `EXIT_BATTLE` calls the MainMenuState class getter (rva `0xC20E70`, `kMainMenuClass`) and the state's change-scene helper (rva `0x584D20`: `state+768` is the scene manager, then `kChangeSceneState`) with no message. That is exactly `MatchEnd::LeaveBattle`, which the room's versus battle already uses. (`BACK_TO_CHARASELECT` asks for the class at rva `0xC220F0` instead.) |
| A player's battle parameters | `SetPlayerParam`, rva `0x309330` (`sfv::kSetPlayerParam`, BuildCheck signature added) | 0x2A entries of `{value, default, min, max}` (16 bytes); refuses a parameter past 0x29 or a value outside `[min, max]`; -2 restores the default. Its readers go through rva `0x2F9E30`. |
| Which tables | `GetBattleData` (`kGetBattleData`, rva `0x307F90`) `+560 + 1424 * side`, and each fighter's copy at `CharaUnit + 864`; the fighter's side is `CharaUnit + 1880` | Training's own menu writer (rva `0x4A86F0`) sets params 22, 24, 26 and 27 of both players in `GetBattleData(...)+560` and `+1984` and in each fighter's `+864`. The fighter's status reads them through rva `0x3512A0`: it finds its `CharaUnit` (type `0x3A647C0`, `sfv::unit::kCharaUnitType`, rva `0x342720`), takes its int at `+1880` and reads `battle data + 1424 * side + 560`. A round's start (rva `0x357540`) reads the `CharaUnit + 864` copy for the starting gauges. |
| Vital (param 22, `EKWGaugeVital`) | damage rva `0x361670`, tick rva `0x3635C0`, heal rva `0x346890` | Values 6-10 (`INFINITY_MIN..INFINITY_MAX`) set the damage's bit 1, so a fighter whose vital goes under 0 is never marked down (the KO flag, `CharaStatusPart` dword 192) and the vital stops at 0. Each tick, once the fighter has been left alone for 60% of the recovery timer (dword 50 against `dword_143966360`), vital (dword 48) and recoverable vital (dword 52) grow by 1% of their maxima a frame up to the value's cap: 10 is the whole gauge. 11 holds both at 0, 12 at full (damage is not applied at all). |
| EX gauge (param 24, `EKWGaugeExtra`) | rva `0x362AC0` | 3 (`INFINITY_MAX`): once its timer (dword 57) is full the gauge (dword 55) grows 1% of its maximum (dword 56) a frame; 4 holds 0; 5 holds full. A round's start gives 2, 3 and 5 a full gauge. |
| V-gauge (param 26) | rva `0x3632E0` | 4 refills to half, 5 to full the same way (dwords 61-63); 6 holds 0; 7 full. |
| Training's own menu values | rva `0x4A86F0`, tables at `0x2C73090`, `0x2C72FA0`, `0x2C73010` | vital `{0, 12, 10, 9, 8, 4, 3}`, EX `{0, 2, 5, 3}`, V `{0, 3, 7, 5}`, stun `{0, 2, 3}`: the values chosen below are ones the game's own menu writes. |

The parameter tables are outside the game's rollback snapshot (the battle data's snapshotted part is `+6176..+6404`; the `CharaUnit`'s is `+1892..`, `SFV_REGION_MAP.md`) and nothing writes them during a battle but Training's menu. The gauges, timers and the KO flag they drive are `CharaStatusPart` words in the snapshot (`+192`, 372 bytes).

### A. Training from Ember's Home

- `Capabilities::training` is on (`SfvGame`); the adapter's `Training` is `SfvTraining` (`src/ember/SfvTraining.*`), replacing the library's `NoTraining`. The runtime's gates, Home's Training row, the room's Wait in Training row, `SayTraining` and `CallOutOfTraining` all read it; none of them changed.
- Home's Training row sends the runtime's Home Training request as on SF4. SF4's `Request` sends its game to the Training item of its main menu, which Ember intercepts with its Training select; SFV's `Request` (from no room) asks the shell for Training select directly (`TakeTrainingSelectRequest`): the shell closes behind the request and opens on `ShowTraining()`.
- Training select is the library's (`ApplicationShell__Training`): Fighter, Dummy, Stage, Start Training. Its rows are SFV's: `FillView` gives `dummySummary` and `dummyError`, and `DrawSelection(dummy)` opens a second `SfvSelectionScreen` that edits the dummy (no stage row: Training loads on the player's stage, as SF4's dummy selector has none).
- The dummy is a pick of its own (`SfvPick::CurrentDummy`/`SetDummy`), fitted by the same rules as the player's (`FitOwnPick`: a fighter, costume or colour this player cannot pick becomes their first one they can). Until one is picked it is the player's own fighter and look. It is not saved between runs; nor is the player's pick.
- Start Training: `StartTraining` launches the battle jump with target 2 (`LaunchTarget::Training`): P1 the player's pick, P2 the dummy, the player's stage. The host's own check refuses it outside the idle main menu. The game loads its Training battle; a refused or failed launch says `home.training_unavailable`.
- In the battle SFV's own Training runs untouched: the mod's netplay mode plays any battle that is not a room's with the original update (`RunOriginal`), as any battle of the player's own while rooms are on: the adapter binds its layout (528 of the game's regions and 22 of its own, 917,804 bytes, in the solo run) but no session starts. Start opens SFV's pause menu; Ember stays away (the runtime's menu context is `OfflineTraining`, where `ControllerOpensMenu` is false and `MenuShownIn` is false), as on SF4, where the game's pause and Ember's F6 controls are what Start and Back open there.
- `SfvTraining` follows the battle once a frame on the game thread, after Ember's tick: it is in Training while `TrainingBattleState` is the current scene with no change pending and a battle frame updated within the last three game frames (`mod::LastBattleSeen`, which `BattleDriver` records after each update: the battle's number, whether its flow announced the fight, the ticks it ran). `ControlsAvailable` is that, and false once the battle was told to leave; `ReadPlace().generation` is the battle's number.

### B. Waiting in Training from a room

- **In.** The room's Wait in Training row is the library's (enabled by `CanTrain`: seated or queued, no Ready, nobody opposite). SFV's `Request` from a room launches the Training battle at once with the room's pick and the last dummy (SF4's goes to SF4's own Training fighter select there, which SFV Ember does not drive); the room stays joined.
- **The room's word.** `SayTraining` reads `ControlsAvailable`: copy 1's room shows copy 0 as `In Training` (shot `call-1-room-of-copy1.png`), with no SFV code.
- **The line.** Over the battle a muted line under SFV's timer says where the player stands (`DescribeTrainingRoom`, the library's): "Private room · Table 1 · Waiting for an opponent" (shot `call-1-waiting.png`). SF4's own `DrawTrainingRoomStatus` and `ChallengerBanner` are in SF4's `TrainingMeter.cxx` (SF4's side, with its roster and its HUD scale), so `SfvHud` draws SFV's: the same words and the same fitting of the two names into two fifths of the screen, on a band of the Band theme's ink. It hides under the shell and the game's pause menu (no battle tick ran in the last update: `TrainingPaused`).
- **The call.** The room's call owner is the library's (`TrainingCall`, `CallOwner`, `CallLifecycle`): it sets the call each tick (`SetCall`). `SfvTraining` runs the library's `training::LeaveCountdown` on it, as SF4's lab does: a new call starts 120 frames (SF4's `LeaveFrames`), the call ending stops it, go now cuts it to its last frame, and the last frame waits for the fight to be running (the flow's announced state is Fight). On it the battle is told to leave by `LeaveBattle` (the pause menu's `EXIT_BATTLE`, above). While the countdown runs `SfvHud` draws the banner: SF4's words "YOUR MATCH IS READY" across the middle of the screen on a band of ink closed by the theme's gold hairlines (SFV's own bands, section 17), in the gold and still, not flashing; under it who sat down ("SFV 2", their fighter when the room shares it) and go now's prompt (shot `call-2-banner.png`). The others' table card says "Calling SFV 1 back from Training" (shot `call-2-calling-on-copy1.png`).
- **No announcer.** SF4 plays the challenger call; `GameState::PlayChallengerCall` is still a stub on SFV (no sound module), so the banner alone tells the player. The log says so.
- **Back at the main menu.** The shell opens when the runtime's `trainingCallSequence` moves at the main menu (as SF4's overlay opens it); the shell itself goes to the table page, where Ready up says "Ready within 14 s or you lose your seat" (shot `call-3-back-in-room.png`). The window, the forfeit and Auto-ready from Training are the runtime's.
- **Go now.** The library's `GoNowGate`: SFV's shell takes a fresh View on the gameplay pad (`TakesPad`) while the call is offered and neither Ember's menu nor the game's pause has the pad; a taken press is hidden from the game until released (the overlay's `FilterGamePad`). Enter is taken in the game's window procedure by the library's `GoNowKey` through a new overlay hook (`OverlayUi::takesMessage`), while the window has the focus. The banner offers View on a pad and Enter on the keyboard, and View only behind another window (`GoNowGlyph`), as SF4's does.

### C. Practice rooms

- **Playable.** The library offers the Practice room row only when the game plays the room's longest round time (`ember::PlayableRoundTime(room::PracticeRoundTime)`, 9999 s), and a Practice room projects that time into every game (`room::MatchRoundTime`). SFV's catalog now lists 9999 s (`picks::kRoundTimes`, the room host's too), which SFV plays as its own no time limit: `VersusLaunch::RoomClockFrames` maps 9999 to `MatchRules::kNoTimeLimit` (-2, the versus settings' infinite clock); every other time keeps `RoundClockFrames` (30-546 s). So a normal table can also be set to 9999 s, as SF4's can, and SFV plays it with no clock.
- **The flag.** The room's flag reaches the match as SF4's does, in `LobbyData::trainingMode`. `SfvBattle::Enter` puts it in `RoomMatch::practice`, `LaunchPicks` into `MatchPicks::practice`, the launch keeps it with the linked battle (`TakeLinkedBattle(&practice)`), and `RoomBattle::Practice()` answers for the battle it binds. A watcher's launch takes the same path.
- **The gauges.** In `NetplayMode::StartSession`, on both copies (and every watcher) at the battle's first session frame, right after the random generators' reseed and before anything is simulated: `SfvAdapter::SetPracticeGauges` sets vital 10, EX 3 and V-gauge 5 for both players with the game's own setter, in the battle data's tables and each fighter's copy, as Training's menu sets them. Stun is left as in a match, as SF4's Practice leaves it. A refused value or a missing fighter is logged and the match plays with a match's gauges.
- **Why this stays deterministic** (SF4's argument, `TRAINING_OVER_ROLLBACK.md`): nothing is written during a frame. The values are written before the session's first frame on every copy alike, never change during the battle, and the game's own update does the refilling on state its snapshot saves.
- **Watchers.** A member who watches a Practice room's table launches the same match with the same flag and sets the same gauges at its session's start, so it plays the same battle as the fighters (section 25); not run in game (Left).
- **The match.** `Training::MatchPracticeActive` is `PracticeBattleInPlay` (the room's battle is a Practice room's and its session is live), so the runtime's menu context is `PracticeMatch`: Start opens Ember over the fight on the room page with the focus on End practice (the library's), which sends the room's `AbortMatch`; every copy tears the game down from its `MatchEnded`. SFV's HUD tells the fighter "Start or F10 opens Ember, where End practice ends this game." (SF4's `training.match.end_hint`, shot `practice-2-fight.png`); SF4's second line, about its retired shared save and reset, has nothing to say on SFV and is not drawn.
- **Words.** SFV's product overlay (`SfvProduct.cxx`) replaces two shared strings on SFV only: Home's Training detail (SF4's names SF4's Fight Request question) and the Practice room's detail (SF4's names its 9999-second clock). No SF4-visible text changed.

### D. The frame meter

SF4's rule today: a fighter never sees the meter in a match they fight; a Practice room's game shows it to both fighters; a watcher sees it with Match frame meter on. SFV Ember has no frame meter (SF4's reads SF4's fighter state, `training::ObserveMatch`), so on SFV no fighter sees one, Practice or not, and no watcher. That matches SF4's rule for fighters in normal matches and falls short of it in Practice rooms and for watchers. The setting, which did nothing on SFV, is no longer offered there: the library's `Capabilities::frameMeter` (SF4 on, SFV off) hides Settings' Match frame meter row (library commit below). SFV's own Training shows its own frame data (Attack Data, Frame Advantage in Color).

### E. The offline lab

Left to SFV's Training menu: recording and playback, dummy behaviour and reversals, position reset, frame data. SF4's F6 controls, its training pad binding, hitboxes, Data panel and combo check are built on SF4's battle internals (`TrainingRuntime.cxx`) and have no SFV counterpart; rebuilding them would duplicate what SFV's menu already does.

### The library

One neutral change on `feat/sfv-training`: `Capabilities::frameMeter` (`sf4e__GameAdapter.hxx`), set by SF4's adapter and the fake game; `ShellHost` puts it in `ShellView::offersFrameMeter` (default on, so every SF4 view and render test is as before), and Settings' Training and replays panel leaves out Match frame meter without it. `ShellJourney` checks the panel without the row. Everything else Training needs on SFV (the gates, the room's Training flag, the call, the window, the forfeit, Auto-ready, End practice, the Practice flag's projection) was already game-neutral and is used unchanged.

### Tests and tools

- `EmberAdapterTests`: the capability, `Training` with no battle (not available, generation 0, no Practice match), the dummy pick (the player's until picked, fitted, kept apart, no stage), the room's 9999 s as no time limit and a Practice match's launch, the catalog's five round times. `RoomHostCatalogTests`: 9999 s is playable on the room host.
- Status field `training`: `available`, `battle`, `leavingIn`, `call`, `paused`.
- `-sfve_mash_pad=1` (`ModConfig`): with `-sfve_mash`, the copy plays its pad's input before `-sfve_mash_from`, so a test's scripted presses come first and the seeded random input after. Without it nothing changes.
- `tools/runtraining.py --part solo|call|practice|control|all [--shots DIR]` (its docstring says what each part does): menus through each copy's virtual pad, the fight's scripted hits through copy 0's pad with its window in front (the game reads its pad only then), P2's vital and recovery timer through the pipe's `peek` and `perturb`, the checkpoints from the logs. It takes `D:\sfv-re\sfv-launch.lock` while SFV runs.

### Evidence
All in game on this PC at 960x540 (one copy for solo, two for the rest), through `tools\runtraining.py --part all --shots D:\sfv-re\shots\training\final` on the final build (sfv-ember `feat/training`, library `feat/sfv-training`), then the Practice part again with 30 ms of lag each way.

| What | Result |
|---|---|
| A: Home's Training row | Training select opened over the main menu (`solo-2-training-select.png`: Fighter RYU / DEFAULT / Color 01, Dummy, Stage, Start Training). The Dummy row's screen (`solo-3-dummy.png`) took CHUN-LI as the dummy. |
| A: Start Training | The scene became `TrainingBattleState` (status `scene` `0x2DA8B70`), `training.available` true with battle 1, the shell hidden; RYU vs CHUN-LI on the player's stage (`solo-5-training.png`, the game's own Training HUD: PLAYER 1 against CPU, infinite clock). The log: `training: battle 1 runs`, `adapter: bound: 528 game regions + 22 outside the game's snapshot` and no session. |
| A: Start in Training | SFV's own `1P PAUSE MENU` opened (`solo-6-pause-menu.png`: Restart Battle, Version Select, Character / Stage Select, Attack Data, Frame Advantage in Color, Key Display, Save Status, Go to Main Menu); Ember's shell stayed hidden; `training.paused` true while it was up. |
| B: Wait in Training | Copy 0, seated alone, left the room screen for the Training battle; the room stayed joined. Its line read "Private room · Table 1 · Waiting for an opponent" (`call-1-waiting.png`); copy 1's room listed it "Seated / Table 1 · In Training" (`call-1-room-of-copy1.png`). |
| B: the call | Copy 1 sat down: copy 0 logged the countdown's start and showed the banner with "SFV 2" and View's go now (`call-2-banner.png`); copy 1's table read "Calling SFV 1 back from Training" (`call-2-calling-on-copy1.png`). Copy 0 was at the main menu 2.0 s after the banner's shot (120 frames from the call), and the shell opened on its table: "Ready within 14 s or you lose your seat" (`call-3-back-in-room.png`). |
| B: the forfeit | Not readied: copy 0's seat went, by the runtime's "not readied in time after the call from Training; the seat is given up" (`call-4-forfeit.png`). |
| B: go now | A second call, cut short by the pad's View: at the main menu 1.1 s after the press (`shell: go now on the Training call (the pad's View)`). A third, by Enter in copy 0's window: 1.0 s (`... (Enter)`). |
| B: the game after | Both readied from the table and played the room's game to its result; both back in the room. |
| C: no KO | In a Practice room (`practice-1-room.png`), P2's recovery timer was held off and its vital set to 1 of 67,174,400 on both copies at the same frame (1431, the lagged run; 1479 without lag), with no input in flight; P1's medium punches then landed. Sampled twice a second: P2's vital was 0, never below, no result was reported, and 15 s later it was full again. |
| C: the control | The same in a normal room's match (1 round of 30 s): P2 was knocked out by the first hits; both copies' result `{"winner":0,"finish":"perfect"}` at frame 3578. `perfect` is a KO's finish (MatchResult: finishes 1 and 3-6 count only with the judge's KO cause; a clock that runs out reads `time over`). |
| C: random play | With 30 ms of lag each way (`-sfve_lag=30`, as `runroom.ps1 -lag 30`), both copies played seeded random input from frame 3000 for 90 s (`-sfve_mash`): no KO, no clock, no result; the lowest vital sampled was P1 54,919,168 and P2 60,096,512 of 67,174,400. Rollbacks: 858 on P2's copy, 0 on P1's; section 12's versus runs also had them almost all on one copy. |
| C: End practice | Start opened Ember over the fight on End practice (`practice-3-end-practice.png`); A asked to confirm ("End practice? ... No result will be recorded", `practice-3-confirm.png`), Right and A ended it; both copies were back in the room with no result (`practice-4-ended.png`). The fight's HUD said "Start or F10 opens Ember, where End practice ends this game." over SFV's infinite clock (`practice-2-fight.png`). |
| C: determinism | Every confirmed checkpoint both copies logged (`checkpoint: frame N hash H`, every 60 frames, `SfvVerification`), compared as `runroom.ps1` compares them: 147 compared, 0 differing, from frame 0 through the hits at 1 point of vital and the random play to End practice. No desync notice. The run without lag: 146 compared, 0 differing. |
| Regressions | `tools\runroom.ps1 -presses 10 -games 2 -lag 30`: PASS, both games decided at frame 4586 on both copies (time over), 154 checkpoints compared, 0 differing, no desync (rollbacks 283 and 248 on copy 0, 0 on copy 1). `python -I tools\runshell.py`: PASS (a private room through the shell, a game, both back in the room). |
| sfv-ember ctest | `scripts\build.ps1`: 5 of 5 suites (`sfve_tests`, `ember_runtime`, `sfve_ember_tests`, `sfve_room_host_tests`, `guard_probe`). |
| Library | `feat/sfv-training` dad1ee570 (from `feat/ember-library` 5817bf9df). x86 build (`D:\sf4-build\ember-library`, ninja -j 8) and 14 targeted ctest suites passed: ShellJourney (with the new check), RuntimeCapabilities, ProductIdentity, ProductRules, ProductRulesOn, UiRender, UiRenderLocales, UiRenderAtlas, UiRenderGallery, RoomPrivateGolden (the wire fixtures), Localization, TrainingCall, RoomTrainingFlag, TrainingSession. `scripts\probe-x64.ps1` passed (the runtime under the fake game). SF4's views are unchanged by construction: SF4 sets the capability and `ShellView` defaults it on; no wire, fixture or SF4 text changed. sfv-ember's build above compiles this tree. |

The shots are in `D:\sfv-re\shots\training\final\`. As in earlier sections' shots, SFV's own plate over P1's life bar in a battle the player starts themselves shows the Steam profile's name; a room's battle shows the room's names ("SFV 1", "SFV 2").

### Left
- **For the maintainer: 9999 s on every table.** Practice rooms need the room's longest round time to be one SFV plays, so SFV's tables now offer 9999 s too, played as the game's infinite clock (SF4's tables offer 9999 s as well). The alternative is a catalog flag in the library that offers Practice rooms without the time. The public room host built before this (`sfve-room-host`) does not list 9999 s and would refuse a public table set to it until it is rebuilt and redeployed (`server/roomhost`, section 24); private rooms are hosted by the players' own runtime and take it now.
- **For the maintainer: the library change.** `Capabilities::frameMeter` touches `ShellHost` and Settings. SF4's adapter and the fake game set it, and `ShellView` defaults it on, so no SF4 view changes (UiRender passes). It sits beside the replays agent's `Capabilities` work and may meet it in the merge.
- **Words.** Home's Training detail and the Practice room's detail are SFV-only overrides in SfvProduct's overlay; no SF4-visible text changed. Training select, the banner, the room line and End practice use the shared words as they are.
- **The frame meter** (D) is not shown to anyone on SFV: neither to a Practice room's fighters, as SF4 shows it, nor to watchers. It needs a reader of SFV's fighter state (startup, active, recovery, advantage), which is new reverse engineering.
- **The announcer's challenger call** is not played (no SFV sound module); the banner alone tells the player.
- **The dummy is not saved** between runs (nor is the player's pick).
- **A room member who leaves SFV's Training by its own pause menu** for Character / Stage Select lands in SFV's character select, out of Ember's reach until they go back to the main menu themselves. The room then shows them out of Training. Its Go to Main Menu is the scene change Ember's call makes (`EXIT_BATTLE`), not pressed by hand in these runs.
- **Not run in game:** a watcher in a Practice room (it takes the same `SetPracticeGauges` path at its session's start, and the watcher checks of section 25 apply); a Practice match's game going on in place (a Practice game has no KO and no clock, so it only ends by End practice); go now while the game's pause menu is up (refused by `TrainingPaused`, by reading); the call reaching a player in SFV's own Training character select (they are not in a battle, so not called, as on SF4).
- **The one-frame order.** `SfvTraining` follows the battle after Ember's tick in the same frame, so the runtime's call reads the previous frame's `ControlsAvailable` and `ReadPlace`: one frame later than the battle, as SF4's lab publishes its view after the battle update.
- **The test aids.** `-sfve_mash_pad=1` (a copy plays its pad before `-sfve_mash_from`, for a test's scripted presses) and `runtraining.py`'s `perturb` of P2's vital and recovery timer: the mode keeps one pending perturbation, so the timer and the vital go in turn, each at a frame no input in flight crosses.

## 28. Sets, records and rankings (2026-11-07)

The maintainer: SFV Ember "pretty much up to parity with usf4 ember in all functions"; this section is sets, match records and rankings. Library `feat/sfv-rankings` (from `feat/ember-library` 7c1a97990); sfv-ember `feat/rankings` (from `main` 0914122).

**What SF4 Ember has, and where SFV stands now**

| SF4 Ember | Where it lives | SFV Ember |
|---|---|---|
| A table's set: Unlimited or first to 1-10, winner stays, loser stays or both rotate, King of the hill's winner keeping the fighter | the room model (`RoomRules.hxx`, `RoomModel`), game-neutral | Works as it is. In one battle through Instant rematch (section 19's `-rematches 3 -firstTo 3` run); across battles, a game a battle, run here (below). |
| A member's sets won and lost in the room, on the Members card | `Member::setsWon/setsLost`, `RoomPanel` | Works as it is; the room status gains `sets`, and the across-battles run checks both copies record each set. |
| Profile's online record (wins, losses, the recent keys) | `netplay::ProfileRecord`, `MatchResultOutbox`, the runtime | Works as it is: a room game's result counts once, in-place games by their number; checked in the public room run (status `ranking.record`). |
| Replays › Sets: a room set of two or more games, Watch set, Pick a game | `platform/ReplaySets.hxx`, `ReplaysPanel` | SFV's listing makes room sets (two battles of the same two names within half an hour; `SfvReplaysTests`). A set's videos are behind `replayVideo` (off), and a pack only exists for a bridge's set. |
| Public room classes: a Glicko-2 rating per Discord account, the class signed into the room ticket, shown on seats, Members, the match HUD and Profile's card | the bridge's `skill`, the helper, `NameMarks`, `MatchHud` | **Was USF4's only** (section 24: SFV rooms' games unrated, tickets without a class). Now each game has its own rating; SFV's tickets carry SFV's class, drawn as SFV's league. |
| Rankings (Online play): the standing, Take part / Stop taking part (once a day), the board of classes | the bridge's `/v1/ranking/*`, the helper's `ranking_*`, `RankingsPanel` | **Was a live mismatch:** SFV's shell offered Rankings wherever a bridge offered `ranking`, and every request was USF4's, so an SFV player saw (and could opt out of) their USF4 standing and USF4's board. Now every request is SFV's. |
| The website's rankings page | `server/ember-short/deploy/site/rankings.*` | Gains a Game switch and SFV's board (`/rankings-sfv.json`). |
| The public room listing's faces | the room host's `details.fighters` | **Was RYU for everyone** (SFV Ember sets no main). SFV's room host now lists each member's shared pick, else none. |
| Organised matches and their records: partner and organiser matches (BluMint, Challonge, start.gg, the desk), lobby standings, `GET /v1/players/{id}/record`, tournament sets and set packs, match links | the bridge's matches, lobbies, tournaments; the client's tournament flow | Not for SFV when written; **since section 31 (2026-11-08) organised matches are per game** (migration 059) and SFV plays them (section 32). |
| The replay archive's classes (archive.embernetplay.link) | `ranking::public_classes` | USF4's: the archive holds USF4 replays only (section 26). |

### Rankings per game: the bridge (migration 058)

The rating, the sitting count, the choice to take part and the board's listing are now each game's (`ember_protocol::rooms::GAMES`: `usf4`, `sfv`):
- `058_ranking_games.sql` rebuilds `skill_ratings`, `rated_pairs` and `ranking_choices` with `game` at the head of their keys, `DEFAULT 'usf4'`, copying every row as USF4's. A query or insert that names no game (an operator's script, an older test) reads and writes USF4's rows as before. `rated_games` is already per room, and a room has one game.
- `skill::record_games` rates a room's reported games in the rating of the room's game (`rooms.game`, any game of `GAMES`), never another's; `show`, `badge_of`, `load`, `store`, `rated_account` and `sitting_weight` take the game. Ended sittings are forgotten across games, as before.
- The ticket carries the class of the room's game (the `is_default_game` gate of section 24 is gone).
- `GET /v1/ranking/me`, `PUT /v1/ranking/me`, `GET /v1/ranking/board` and `GET /v1/ranking/board/public` take `?game=`; without it they are USF4's, so every existing caller (SF4 Ember, the live website) is answered byte for byte as before. An unknown game is `invalid_request`. The public board's 15 s cache is kept per game, so one game's board is never answered for the other.
- Taking part, its once-a-day rule and the name and flag the board lists are each game's: stopping in SFV leaves USF4 as it was. A player with no SFV choice of their own inherits their USF4 choice (`skill::rated_account`, `ranking::Choice::load`): one who stopped taking part in USF4 is not rated in SFV and carries no SFV class until they choose in SFV, where their own choice wins at once (no wait for a first choice). A player who never chose takes part in both. (the maintainer's call, reversible: without the inheritance everyone takes part in SFV until they stop there; the alternative the other way is one choice per Discord account for every game.)
- The rules, the weights, the ten bands' floors and the developer mark are the same in every game. The floors are USF4's until calibrated; SFV's need their own calibration before the maintainer turns classes on for SFV players, and `rooms.skill_badges` is one switch for both.
- `ranking::public_classes` (the archive) is pinned to USF4's games: the archive shows USF4 classes.
- The bridge lists `ranking_games` (`RANKING_GAMES_FEATURE`) beside `ranking`: its ranking routes read `?game=`. A bridge from before 058 does not, and it would ignore `?game=`: SFV Ember would see USF4's standing and board, and its Take part or Stop would change the player's USF4 choice, name and flag (and `/rankings-sfv.json` would serve USF4's board).

### The helper and the runtime

- The helper's `ranking_standing`, `ranking_choose` and `ranking_board` carry an optional `game` (as `room_list`, `room_create` and `room_ticket` do since section 24), written only when it is not `usf4`; it checks the key and adds `?game=` to the bridge path only for another game, so SF4 Ember's paths are `/v1/ranking/me` and `/v1/ranking/board` exactly as before. A request for another game is sent only to a bridge whose capabilities list `ranking_games` (read once a run, and again while it lacks it, `play::Need::RankingGames`); to any other it is refused `ranking_unavailable` before anything is sent, so on an old bridge SFV's Rankings says the service does not offer rankings, and no SFV choice can reach USF4's.
- The runtime's `WithGame` (moved from the public rooms file into `internal`) is applied to every ranking request: SFV Ember's name `sfv`, SF4 Ember's name nothing.

### SFV's ranks: SFV's own leagues, from the game

USF4's classes are USF4's online classes drawn with the game's emblems (`Ranking.emz`). SFV's equivalent is its league ladder, whose icons are in the install: the pak indexes list `/Game/CommonAsset/LeagueIcon/LI_rookie` to `LI_warlord` (20 leagues and `LI_dummy`; only the indexes were read, no asset). The bridge rates every game in the same ten bands, so the library's change is a product table:
- `ProductIdentity.hxx`: `RankingClass` (`emblem`, the game's `name`, or a shared `msgid`) and `Branding::rankingClasses` (`RankingClassCount` = 10, band 1 first; none is USF4's, so SF4 Ember is unchanged). `NameMarks` takes the class's name and emblem from the product's table or from USF4's own, which is the old table moved verbatim (`static_assert` that the band counts agree). Where a product's emblem is missing from the install, the class is its name in a pill, faint as the emblem would be; one of USF4's still shows nothing until its emblem loads.
- **The maintainer's decision:** "The ranks for sfv will follow the ranks from the game visually". So an SFV rank is the game's own league icon, drawn at its own shape (`NameMarks`: a product's icon is not fitted into USF4's square slot, and nothing of Ember's frames it), named as the game names it, and a pill with the name appears only for an icon that fails to load.
- SFV Ember's table (`SfvProduct.cxx`): its ten tiers without the Super and Ultra steps, Rookie, Bronze, Silver, Gold, Platinum, Diamond, Master, Grand Master, Ultimate Grand Master, Warlord, each with its `LI_` icon (the game's own spelling, `LI_ultimet_grand_master`). The 1500 every player starts at is the floor of band 5, Platinum, as it is the floor of B in USF4. Its English names stand in until the game gives its own.
- **The game's own names, localized.** SFV builds its league names itself (rva 0x6BDE20) as text literals: `CreateText(out, key, "KW", key)` (rva 0x2DBB50, an NSLOCTEXT: the game's own localization data by namespace and key, the key itself where the data has none; `sfv::kCreateText`, BuildCheck signature checked against the installed exe) for `ID_CMN_LEAGUE_0000` to `_0019`, then `FText::ToString` (0x116E270, section 15). No Capcom connection is involved: the text is in the install. SFV Ember does the same once at the main menu (`mod::LocalizedText`, `SfvArt`'s `ReadLeagueNames`), logs all twenty ("ranks: SFV's leagues as the game names them: ..."), and publishes the ten bands' (`ranks::Publish`, `SfvRankNames.hxx`, defined in `SfvProduct.cxx` so the launcher and updater link it with no game code). The library's new `Branding::rankingClassName` hands them to `ClassName`; a name not given yet falls back to the table's. The bands' leagues are 0, 1, 4, 7, 10, 13, 16, 17, 18, 19 in the game's order (`ranks::kBandLeague`), on the reading that the game's twenty keys run Rookie to Warlord with each tier's Super and Ultra after it; the in-game log checks that.
- **The whole ladder (the Super and Ultra steps) is not shown**: the bridge rates every game in ten bands, and SFV's twenty steps need a band count per game through code SF4 shares (the protocol's `SKILL_BANDS` in `SkillBadge::check` and `Board::check`, which the helper applies to tickets and boards; the bridge's `BAND_FLOORS`/`band_of`; the C++ `SkillBadge::Classes` in `Valid`, the decoders in `TournamentAnswers`, `SkillBadgeJson`, `MatchHud` and `NameMarks`; the website's tier tables). SF4's values would stay ten, but every one of those would take the game or the product, so per the brief this stops at the ten base tiers until the maintainer decides (the library's SKILL_BADGE.md, "Games", lists it).
- `SfvArt::LeagueImage` loads a league icon through section 15's path (`StaticLoadObject` with UTexture2D's class at the main menu, the D3D11 texture checked and viewed); it takes only a name in the product's table, so nothing else is ever loaded by it. `SelectionArt::RankingEmblem` on SFV is that.
- No new text in the shared catalog, and no SF4-visible text changed. The shared words SFV now shows with its leagues: the tooltip "Class {0}" ("Class Gold"), Rankings' "PLAYERS BY CLASS" and "How classes work"; the maintainer may want SFV overlay wording ("League {0}"). The website shows SFV's leagues in English (it has no game to ask).

### The listing's faces

`roomhost::DetailsOf` takes which fighter stands for a member (`ListedFaces`: Mains or Picks), and each host build says which beside `PrepareCatalog` (`CatalogFaces`): SF4's lists mains (unchanged), SFV's lists each member's shared pick (`Member::fighter`), else 255 (no face), as SFV's own room shows them (`roomFacesArePicks`, section 20). SFV's ids are under 45, inside the bridge's `FIGHTER_LIMIT` of 64, so the bridge needs no change for them; the website's SFV cards still draw no faces (the face sheet is USF4's).

### The website

`rankings.html` and `rankings.js` gain the Game buttons the rooms page has (`?game=sfv` in the address, the view's filters kept in the fragment): Street Fighter V's board comes from `/rankings-sfv.json` (a new nginx location, its own cache key, `/v1/ranking/board/public?game=sfv`), named in SFV's leagues with the chart's short labels (R, B, S, G, P, D, M, GM, UGM, W). A board that arrives after the player switched games is dropped and the shown game's is fetched. Two lines are per game (`data-game-text`): the lead and the first step of "Getting on the board". New site wording for the maintainer: "Players by their league in SFV Ember's public rooms, from Rookie to Warlord. ..." and "In SFV Ember, open Online play, Rankings and connect Discord if it asks. Your league belongs to your Discord account and is kept apart from your class in Ultra Street Fighter IV."

### What does not apply to SFV, and why

- **Organised matches and their records** (as written on 2026-11-07; section 31 made matches, lobbies and tournaments per game and section 32 plays one in SFV). The bridge's matches are USF4's by contract: `CreateMatch::check` refuses any `game` but `usf4` (`protocol/src/matches.rs`), the capabilities list `games: ["usf4"]`, lobbies and tournaments carry `usf4`, and the player record (`/v1/players/{id}/record`) and lobby standings are computed from those matches. SFV Ember registers no `ember-sfv://` handler (no match links), and its build would be refused a USF4 match's room. Bringing SFV in needs a game on matches, partner contracts that name it and the client's tournament flow on SFV: a separate piece of work and the maintainer's call with the partners.
- **The Ember ID screen's match list** is the same library screen on SFV; a platform that assigned a (USF4) match to an SFV Ember ID would list it there. No platform links SFV Ember IDs today.
- **A set's videos and packs** (Replays › Sets): `replayVideo` is off on SFV (section 26), and a pack is a tournament set's.
- **Archive classes**: the archive is USF4's.

### Tests and tools

- Bridge (`server/ember`): `db.rs` `ratings_sittings_and_choices_from_before_games_existed_are_sf4s` (058 copies every row as USF4's, a row written without a game is USF4's, each game keeps its own row); `tests/room_games.rs` `games_in_an_sfv_room_are_rated_in_sfvs_rating_alone` (both players move in SFV's rating, a game reported again counts once, the same players' SF4 game starts SF4's rating from scratch and leaves SFV's, the sittings are per game) and `a_room_admits_only_its_own_games_build_and_each_ticket_carries_its_games_class` (placing in SFV and B+ in USF4 at once, then each game's settled class on its own room's ticket; none when not asked); `tests/ranking.rs` `each_game_has_its_own_standing_choice_and_board` (each game's listing, `game=usf4` said out loud, stopping in SFV leaves SF4 free to change today, each board and each public board, cached apart; an unknown game refused on every route) and `a_player_out_of_usf4s_ranking_is_out_of_another_games_until_they_choose_there` (a USF4 opt-out reads as opted out in SFV, its SFV ticket has no class and its SFV game counts for neither; its own SFV choice takes part at once with its class while USF4 stays out; stopping in SFV alone leaves USF4 in); the capabilities list `ranking_games` with `ranking`. `tests/archive.rs`' seed now names the new key.
- Helper (`rust/sf4-net`): `a_game_other_than_sf4s_is_named_and_sf4s_never_is` (the requests round-trip with `game`, `usf4` is never written, SF4's paths exactly as before, an unknown key refused before any call; a bridge that lists `ranking` alone is refused another game's request as `ranking_unavailable`), `a_bridge_offers_what_its_capabilities_list` (`ranking_games`), and `tests/ranking_bridge.rs` against a real bridge (SFV's standing and board asked; a bridge without rankings refuses both games).
- Library C++: `ProductRules`/`ProductRulesOn` check `ClassName` under USF4's table and under a product's (a game name, a catalog msgid, a name the game gives at run time leading, and an empty one falling back); `SessionServerModeration` checks `DetailsOf` with Picks (shared pick, none, past the roster) and that Mains is unchanged.
- sfv-ember: `EmberAdapterTests` (SFV's ten ranks and icons, `ClassName` names them, the product hands the game's names through `ranks::Name`, empty until the game gives them, and the bands' leagues 0, 1, 4 ... 19; `RankingEmblem` waits for a known league icon and refuses `class_B.dds`, `LI_dummy`, `LI_super_gold`, paths), `RoomHostCatalogTests` (`CatalogFaces` is Picks: shared picks and 255, while Mains would list RYU for all), `SfvReplaysTests` (two battles of the same players 600 s apart are one room set on Replays › Sets).
- The pipe: `ranking standing|board|take-part <country|-> <name>|stop` and the status field `ranking` (standing, board in league names, the last choice, each room member's mark, the online record); the room status's `sets` (this member's sets won and lost).
- `tools/runpublic.ps1 -ranked` (its header says what it checks) with `tools/bridgedb.py`, which seeds Discord accounts and ratings only into a test bridge's `bridge\bridge.sqlite3` and reads it with SELECTs only; it also checks the listing's faces on every run. `tools/runroom.ps1 -firstTo N` without `-rematches`: a set played a battle a game.

### Evidence

Library `feat/sfv-rankings` 152a06b13, sfv-ember `feat/rankings` be0f338 and 53c83dd (the tool), on this PC; two copies at 960x540, 30 ms lag, input delay 2, 1 round of 30 s, RYU vs CHUN-LI, seeded mash.

| What | Result |
|---|---|
| `tools\runpublic.ps1 -ranked -lag 30 -presses 10 -games 2 -shots D:\sfv-re\shots\rankings` | PASS (the fourth run; the first three passed every rating, record, board and faces check and failed only in the tool's own pad route to Rankings, fixed in 53c83dd). Both copies' first SFV standing was `no_account`; after the seed both rooms showed copy 0 as SFV's band 8, settled, named "Grand Master" by the game, and copy 1 placing. Two games (both to copy 1): the bridge rated 2 games, SFV's rating holds both accounts (copy 0 1832, deviation 61; copy 1 2041, deviation 229, still placing), USF4's row unchanged to the byte (1650.0, 60.0, its seed time), one SFV sitting of 2 games. Each copy's online record grew by its 2 games. Copy 0 took part ("SFV 1", GB): its standing was SFV's Grand Master; copy 1's board listed it at Grand Master; the public board for `game=sfv` listed it and USF4's was empty. 154 checkpoints compared, 0 differing. |
| The listing's faces | The room host's line listed `fighters: [0, 2]`, the two shared picks (RYU, CHUN-LI), where both copies' profile main is 0: before, it was `[0, 0]`. |
| SFV's league names, from the game | Both copies logged "ranks: SFV's leagues as the game names them: 0 Rookie, 1 Bronze, 2 Super Bronze, 3 Ultra Bronze, 4 Silver, 5 Super Silver, 6 Ultra Silver, 7 Gold, 8 Super Gold, 9 Ultra Gold, 10 Platinum, 11 Super Platinum, 12 Ultra Platinum, 13 Diamond, 14 Super Diamond, 15 Ultra Diamond, 16 Master, 17 Grand Master, 18 Ultimate Grand Master, 19 Warlord" (the game in English): the keys run as `kBandLeague` assumes, so the bands are Rookie, Bronze, Silver, Gold, Platinum, Diamond, Master, Grand Master, Ultimate Grand Master and Warlord, and the shell's names are the game's. Another language was not tried. |
| SFV's league icon | `art: /Game/CommonAsset/LeagueIcon/LI_grand_master.LI_grand_master: 96x52, format 90` on both copies: a UTexture2D, B8G8R8A8, wider than tall, drawn at that shape. Shots: `ranked-room-1.png` (the room: SFV's Grand Master emblem after SFV 1's flag on the seat and in Members, "?" for the placing SFV 2), `ranked-rankings-0.png` and `ranked-standing-0.png` (copy 0's Rankings: the large Grand Master emblem, "Grand Master", "On the board as SFV 1"), `ranked-rankings-1.png` (copy 1: placing), `ranked-board-1.png` (Players by class: the Grand Master group with the emblem beside the entry). Opening Rankings sends the profile's name and flag (the library's rule), so copy 0's board entry then read "Player" (US), its profile's. Only Grand Master's icon was drawn in these runs (the one class present). |
| `tools\runroom.ps1 -firstTo 2 -games 3 -lag 30` | PASS on the second run: game 1 left the set 0-1, game 2 ended it 0-2 for copy 1, which both rooms recorded (copy 0 0 W / 1 L, copy 1 1 W / 0 L; the chat "SFV 2 won the set at table 1 (2 - 0)"), cleared the score, kept both seated, and game 3 began the next set at 0-1. 231 checkpoints compared, 0 differing. Shot `D:\sfv-re\shots\sets\game2-room-1.png` ("First to 2, Winner stays", the Members' records). The first run desynced at checkpoint 960 of its first game (P1's action part and a run of game objects, `game.244`-`game.499`) and its match ended; nothing of this work runs in a private room's battle, and the second identical run and the regression below passed (Left). |
| `tools\runroom.ps1 -presses 10 -games 2 -lag 30` | PASS, 154 checkpoints compared, 0 differing. |
| `python -I tools\runshell.py` | PASS. |
| Tests | Bridge and protocol (cargo -j 4): 541, all passing; one archive timing test (`a_sets_copy_takes_back_a_duplicates_row`, a 10 s wait for the archive) failed once under load and passed alone (29 of 29). Helper: 483 passed, with the real-bridge `ranking_bridge` tests. Library x86: targeted ctest 22 of 22 (ShellJourney, ProductRules, ProductRulesOn, UiRender, UiRenderLocales, UiRenderAtlas, UiRenderGallery, RoomPrivateGolden, RankingsJourney, RankingRequests, PublicRoomsJourney, SessionServerModeration, RoomHostConfig, ProductIdentity, CatalogRoster, GameCatalog and others) and the Room/Session/Public/Ranking/Replay/Iroh set 56 of 56; `scripts\probe-x64.ps1` passed. sfv-ember: 5 of 5 suites. |
| The website | The rankings page served from the site folder against fixed boards: the Game switch, SFV's league names and short labels, `?game=sfv` with a class filter in the fragment, back to USF4, and a phone width with no sideways scroll. |

The game folder was left with the main build (`D:\sfv-ember-build`) deployed again after the runs.

### Left, and the maintainer's calls

- **Per-game taking part, inheriting USF4's** (chosen here): stopping in SFV leaves USF4 as it was, each game lists the name its game sends, and a player with no SFV choice follows their USF4 one. Reversible either way: drop the inheritance (everyone in SFV until they stop there) or make it one choice per Discord account.
- **The replay channel** (`nightly-replay-channel`, unmerged): its `server/ember/bridge/src/skill/channel.rs` lines 231 and 253 read `ranking_choices` with no game filter and need `game = 'usf4'` when it is merged on top of 058.
- **SFV's whole ladder.** The maintainer decided SFV's ranks follow the game's visually (icons, shape, the game's names: done). The Super and Ultra steps need a band count per game through code SF4 shares (above); until the maintainer chooses that, SFV shows the ten base tiers. Also SFV's band floors: the floors are USF4's, and `rooms.skill_badges` is one switch for both games; SFV's ratings start counting when the bridge is deployed, whether classes show or not.
- **Words.** On SFV the shared tooltip reads "Class Gold", Rankings' header "PLAYERS BY CLASS" and its explanations say "class"; SFV's overlay could say "League" (no SF4 text changes either way). The league names are the game's own in the player's language (English until the game gives them; English on the website). The site's two new lines (above).
- **Organised matches for SFV** (partners, desk, records, links): needs a game on matches and the maintainer's word with the partners.
- **One desync, not repeated.** The first `runroom -firstTo 2 -games 3` run diverged at checkpoint 960 of its first game (P1's action part, then game objects `game.244` to `game.499`); its second identical run, `runroom -presses 10 -games 2` and four `runpublic` runs (8 games in all, 616 checkpoints) did not. Nothing this section adds runs in a private room's battle (the league names are read once at the main menu, no class is drawn in a private room). Worth watching in later runs; the logs were overwritten by the next run, and the region list is in that run's output only.
- **Not seen in game:** the other nine leagues' icons (only Grand Master's class was present), the league names in a language other than English, and an SFV player whose USF4 opt-out carries over (covered by the bridge test).
- **The website's SFV room cards** still draw no faces: SFV's faces are the game's textures, which the site does not have (the maintainer's "no generated assets").

### Deploy steps (for the maintainer's yes; nothing here was done)

In this order: the bridge (058), then nginx and the site, then SFV's room host and an SFV Ember release.
1. **Bridge** (staging, then production), first: migration 058 rebuilds three small tables (`skill_ratings` about a hundred rows) in one transaction. Every existing request is answered as before, and the capabilities gain `ranking_games`. Follow `upgrade-bridge-ranked-20261020.ps1`'s pattern: back the database up and dry-run 058 on a copy of it before the new binary starts. **058 cannot be rolled back by putting the old binary back**: the old binary's `ON CONFLICT (discord_user_id)` and `(first, second)` no longer match a key, so its rating, ticket and choice writes would fail. A rollback is the old binary with the database restored from that backup (ratings and choices since are lost). The maintainer's admin script `ranks-list.py` should filter `game = 'usf4'`; anything that writes these tables with `ON CONFLICT (discord_user_id)` must use `(game, discord_user_id)`.
2. **nginx and the website**, after the bridge: the `/rankings-sfv.json` location, `rankings.html`, `assets/rankings.js` and `assets/site.css` (setup.sh's check list names the new path). Checked locally against fixed boards, not with `nginx -t`.
3. **SFV's room host** (Linux, as section 24's steps 3-4): `CatalogFaces` is compiled in, so the faces change needs the new host. SF4's room host (`sf4e-room-host`) is rebuilt only with the next SF4 release (its listing is unchanged).
4. **SFV Ember's release** with this build and its helper. Against a bridge without `ranking_games` its Rankings says the service does not offer rankings, so the release cannot write to USF4's ranking whatever the order; the bridge still goes first.

## 29. Crash records and dumps; room links into a running game (2026-11-08)

The maintainer chose "Crash dumps + join link" from section 22's leftovers: the mod had no crash handler, a room link handed to a running SFV Ember was never tried, and `--join-link`'s help named `ember://`. Built on the desktop, built and run on the maintainer's laptop (`ssh director`; the desktop was in use). Library `feat/ember-library` 2e0dae10d; sfv-ember `main` d0eab3a..f0b346e.

### How SFV handles its own crashes

SFV's crash reporter is Unreal's: the exe imports `dbghelp!MiniDumpWriteDump` and formats `%sDump%d.dmp`, which is the `Dump-1053984112.dmp` style files in `%LOCALAPPDATA%\StreetFighterV\Saved\Logs` (a negative `%d`). The engine's guarded main loop and its threads' guards end each crash they catch in that one call, with the fault's `MINIDUMP_EXCEPTION_INFORMATION`. No `CrashReportClient.exe` is installed. Found from the exe's import table and strings; the IDA database was not needed.

Seen on the laptop before the mod ended the game itself: after the dump the reporter does not exit (the game was still running 60 s after the fault; presumably an error box), and a fault on a thread the engine did not start (the mod's) ends the process with 0xC0000005 and no dump or record of any kind.

### What the mod does (`src/mod/CrashRecord`)

- **The hook.** MinHook on `dbghelp!MiniDumpWriteDump` (installed after the network guard, inert or not). A call for this process whose exception code ends the game is a crash: 0xC..., an uncaught C++ exception, a breakpoint, or 1, which the engine raises for its own fatal errors (its other use, `ensure`, is compiled out of this build: the exe has "Assertion failed", "Fatal error" and "LowLevelFatalError" and none of ensure's texts). Anything else passes straight through. For a crash it: writes the record; asks the launcher for the dump, naming the faulting thread from the call (`DumpClient::Request(pointers, threadId)`, new in the library); without a launcher writes that dump itself through the trampoline (`WriteDump(..., writer)`, new); lets the game's own call go on; then, **with a launcher only**, ends the game with the fault's code (3 when the launcher would not read the code as a crash), so the launcher shows its crash window at once. Under the proxy nothing else would show the crash, so the reporter's own handling goes on.
- **Two faults at once**: the first records; a second, on another thread, waits for that record and dump (at most the launcher's 30 s and 5 more) before going on, so ending the game cannot cut them off.
- **The record**: `Product().crashLog` (`sfv_ember-crash.log`) in `%APPDATA%\SFVEmber\logs`, one `FormatCrashHeader` line (kind, code, address, module+offset, thread, the faulting access), from static buffers, one per process. The mod's log gets the same line only when no other thread holds the log (`TryLogLine`); the log is written synchronously, so its tail is already on disk and the record carries no ring of last lines (SF4's does, its log is asynchronous).
- **Threads the engine does not guard**: an unhandled-exception filter, set at the start and taken back at the game's first frame (the engine sets one of its own in between; ours chains to it). A per-thread guard stops a filter that chains back to ours from looping.
- **Heap corruption** (a vectored handler asks the launcher first, as SF4's does), the mod's own CRT faults (pure call, invalid parameter, `std::terminate` on the installing thread) and GGPO assertions are recorded. The invalid-parameter handler is the shared UCRT's, so every module that uses it gets ours: after the record it ends the process with 0xC0000417, as the default does. The game's own C runtime (MSVCR120) is not hooked.
- **Review** (Opus, read-only, of d0eab3a^..f0b346e): no P1. Its P2s are fixed as above (second fault, filter loop, invalid parameter, ending the game only with a launcher, code 1). Its P3s: the launcher refuses `-sfve_dump_channel` as a test option and the parser refuses a sign or `0x`; runcrash reads the launcher's log by bytes. Left: without a logs folder the launcher closes the channel after the start, so a crash waits the full 30 s before the mod dumps itself; a freeze dump can replace a crash dump when the game's own dump takes over 10 s.
- **The channel**: the launcher (`SfvLaunch.cxx`, `InheritedDumpChannel`) duplicates the channel's two events and mailbox as inheritable copies for the start only, gives the game just those through `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, and names them on the command line, `-sfve_dump_channel=R:D:M` (hex). The mod clears their inherit flag, so the helper it starts gets none.
- **Status and test hook**: the pipe's status has `crashRecord` (`installed`, `dumpWriterHooked`, `launcherChannel`); `-sfve_crash_command=1` adds the pipe's `crash game|thread` (a write to 0x10 on the game thread, or on a thread of the mod's).

### Room links

The launcher already had everything: `RegisterJoinScheme` registers `Product().urlScheme` (`ember-sfv`) per user, a second launcher hands the link to the running game through the per-product mailbox (`platform/JoinLinkMailbox.hxx`), and a first start passes it in `SF4E_JOIN_LINK`, which the shared runtime reads. The help text now builds its examples from `ember::SchemeLink` (SF4 Ember's text is unchanged: its scheme is `ember`).

### Tests and evidence (laptop, 2026-11-08 16:30-16:33)

- `tests/CrashRecordTests.cxx` (in `sfve_ember_tests`): the channel's option parses (and five malformed ones do not); a real access violation's record plus the launcher side's `DumpChannel::Serve` on a thread writes the dump, and a second crash leaves the record as it was. Build ctest 5/5.
- `tools/runcrash.py` (`--where game|thread`; refuses to run while Send problem reports is on; deletes the dumps it caused unless `--keep`):
  - through Launcher.exe, game thread: PASS. Record `kind=unhandled_exception code=0xC0000005 ... module=sfv_ember.dll+0x32BD0 ... write of 0x0000000000000010`; the launcher wrote `sfv_ember-crash-*.dmp` (49 MB) and `*-report.dmp` (441 KB); the game's own `Dump-*.dmp` (503 KB) written; launcher: "Game exited with code 0xc0000005 (access violation)".
  - through Launcher.exe, a mod thread: PASS, the same through the filter; exit 0xC0000005.
  - under the proxy (no launcher): PASS; the mod wrote the 49 MB dump itself and the game's reporter went on as it would (the run kills the copy).
  - Final code (8965a7b), 16:49-16:51: all four PASS again (game and thread through Launcher.exe, exit 0xc0000005 in the launcher's log; the link; the proxy); `runroom -presses 10 -games 2 -lag 30` 154/0 on 41ed31b (the review fixes; 8965a7b changes only runcrash).
- `tools/runlink.py`: PASS. `--help` names `ember-sfv://join/...` and no `ember://`; copy 0 started by Launcher.exe hosts a private room; `Launcher.exe --join-link ember-sfv://join/7Q4M-2KXD-9FNB` logs "Started with a room link" and "Handed the room link to the running game" and exits 0; the runtime logs "Room: a room link arrived from the browser" and copy 0 stays in its room (the made-up code is never looked up). Shot `D:\sfv-re\shots\link\link-arrived-0.png` on the laptop.
- `runroom -presses 10 -games 2 -lag 30` on the final build: PASS, 154 checkpoints compared, 0 differing.
- Both new tools start their copy as the other runs do (`-sfve_pads=0`, `-sfve_background=1`; without them an unfocused SFV never leaves its title) and leave its window where it is (`boot_to_menu(..., bring_to_front=False)`: Windows refuses the foreground on a locked desktop, which the laptop's was).

### Left

- A link on a **first start** (no game running) and a real room's link (a short code a bridge resolves) were not run: the first would look up the made-up code on the production bridge. Worth one run against `runpublic`'s local bridge.
- The crash window's **Send this report**: never pressed (production bridge; whether it takes SFV's reports is untested). Small dump preview untested on SFV.
- SF4's record (`sf4e__CrashDiagnostics.cxx`) has the same two patterns the review found here: its invalid-parameter handler returns (the CRT then hands EINVAL back instead of ending the process), and `NoteMatchBoundary` re-takes the filter with no loop guard. Not changed here (SF4 behaviour); worth a look in SF4's next review.
- Ending the game after its own dump skips whatever the reporter would have shown (presumably an error box) and the engine's error lines after the dump; its own dump is still written.
- One record per process: a second fault is waited for, not recorded.

## 30. SFV Ember's words in every language (2026-11-08)

The maintainer chose "Translations". Everything SFV Ember shows goes through the shared catalogs (`T("...")`; the English literals in `src/ember` are log lines), so what was missing were SFV's own words: the 52 msgids SFV overrides because the shared text names SF4, its game, its files or features SFV lacks (sections 17, 22, 23, 26, 27). They showed in English in every language (`LayerProductOverlays` puts a product's English over each translation it gives none for). sfv-ember `main` 3b8a4b8..; no library change.

**Where they live.** `locales/<tag>.po` in sfv-ember, one per language the library declares in `locales/locales.json`, each with English's 52 msgids in English's order (`locales/en.po` holds what `SfvProduct.cxx`'s `kEnglish` held). The build (`CMakeLists.txt`, `sfve_product`) reads each as text, splits it into one raw literal per entry (MSVC takes about 16 KB in one literal) and generates `SfvLocaleOverlays.inc`: the `kOverlay_<tag>` texts and the `LocaleOverlays` table, constant-initialized as before. A language the library declares and SFV has no catalog for stops the configure.

**How they were written.** Each language's catalog started from that language's shared translation of the same msgid, with only what SFV's English changes rewritten (the product, the game, files and folders, SFV's own sentences), so the shared terms (room, table, Ready, invitation, build, Practice...) read the same as on every other screen. Three Sonnet agents drafted them from `D:\sfv-re\i18n` (`BRIEF.md`, `source-<tag>.json`); `make_po.py` there checked and wrote the catalogs. Japanese and Korean keep "Street Fighter V" in Latin letters as their shared catalogs keep Ultra Street Fighter IV's name; Simplified Chinese writes 《街头霸王V》 as it writes the IV one. German's three informal entries (`selection.not_owned`, `home.replays_detail`, `home.training_detail`) follow the shared German of the same msgids (the pick and Home screens say "du", the launcher "Sie"). As the library's README says of its own catalogs, every non-English one is a draft for native review.

**Tests** (`tests/LocaleOverlayTests.cxx`, in `sfve_ember_tests`): one catalog for every library language, each with English's msgids, its placeholders and line breaks, no SF4, sf4e (but Ember's origin in `about.ember_detail`), SSFIV, USF4, Ultra Street Fighter IV, Battle Log or Sidecar, and all but six entries differing from English; each language's `T()` returns its own catalog's text once the overlays are laid; and every character draws, judged from the fonts that ship (Inter's two weights in the atlas's ranges for Latin and Cyrillic, the CJK subset `locales.json` names for the script), read from the library checkout. The SFV catalogs add no character the library's CJK subsets lack, so no font change.

**Seen in game** (laptop, 2026-11-08 17:0x; `D:\sf4\interactive\sfv-lang*.cmd` writes `%APPDATA%\SFVEmber\ui-preferences.json` with the language, runs `runshell --shots D:\sfv-re\shots\lang-<tag>`, then deletes the file): Japanese, Russian and English each PASS a whole runshell (Home, Online play, create room, invitation, join, seats, pick screen, a match, back in the room). Japanese: "SFV  /  ルームとロールバック対戦", "SFVに戻る", every kana and kanji drawn. Russian: "SFV  /  ЛОББИ И СЕТЕВАЯ ИГРА С ОТКАТОМ", "Вернуться: SFV", nothing cut; its longer labels wrap the room's button bar onto two rows, which runshell's way to the invitation's Copy now allows for (it walked DOWN to More...). Build ctest 5/5 with the new tests.

Seen in passing, not SFV's: the room's title "PRIVATE ROOM" stays English in every language because it is the room's name (the host's default name, a player's words to the title code; locales/README.md keeps persisted defaults English); and on this library branch the Russian table is "Стол" in the room's table headers but "Ячейка" in a member's status, because the Russian translator's wording pass (Table = Ячейка, 2026-10-14) is on the nightly line and the library branch was cut from release. Both are the shared catalogs' to settle.

**For translators.** To change or add SFV text, edit `locales/en.po` and the same msgid in every other `locales/<tag>.po` (the test refuses a catalog that lacks one). An SFV msgid must exist in the library's `en.po` (the library refuses an overlay msgid it does not have). Weblate can take `locales/*.po` as a second monolingual component with `en.po` as its base.

**Open, for native review**
- Russian `screen.game`: the shared Russian catalog gives "Главное меню" (main menu) where the English is the game's short name ("SF4"), so SF4's footer reads "Вернуться: Главное меню", a fair rendering of a button that returns to the game's menu; SFV's catalog follows the English ("Вернуться: SFV"). Which one Russian should use is the translator's call.
- Seen on screen: Japanese, Russian and English only. The other eleven are checked for characters (they draw) but not for fit; German and Polish have the longest Latin labels. The launcher's recovery words (`launcher.game_runs_as_admin` is the longest entry) were not shown in any language (`Launcher.exe --locale <tag> --recovery` shows them).
- The drafting brief and scripts (`D:\sfv-re\i18n` on the desktop) were one-off; the method is in `locales/README.md`.

## 31. Tournaments and Discord (plan, 2026-11-08)

The maintainer: "Yes, start it" (Discord + tournaments, the last item of the parity queue). Mapped from the library (two read-only agents, then checked by hand):

**What already holds for SFV**
- **One Ember ID per player across products.** The helper keeps it in `%LOCALAPPDATA%\Ember\Identity\v1` (`rust/sf4-net/src/identity/platform.rs`), no product's folder, so SF4 Ember and SFV Ember are the same Ember ID and the same Discord link (`discord_accounts`: one account per Ember ID, migration 008). Connect Discord is the bridge's browser sign-in (`/v1/discord/start`, `/callback`), not the Discord companion, and is game-neutral.
- **A match's result.** A tournament game is a room match on the tournament table; its report is the runtime's confirmed capture (`ObserveTournamentTerminal` reads `resultOutbox`), which SFV's room battle already fills (`RoomBattle` calls `NotifyRuntimeMatchResult`). No SFV result feed is needed.
- **A match's room.** One fighter's own game hosts it under the bridge's lease; the bridge requires both claims to name the same build (`play/mod.rs:221`) and checks a build id only for ASCII and length (SFV's `sfv-ember/13173604/0.2/...` passes). No room host or supervisor.
- **Picks.** The match path validates none (`character_policy` unrestricted); report facts take 3-letter codes.

**What ties organised matches to USF4**
1. The protocol's one game check, `check_provider_head` (`protocol/src/matches.rs:145`), refuses any game but `usf4` for matches, lobbies and tournaments; `edition_policy` must be `ultra_only` (a USF4 rule).
2. The bridge's capabilities list `games: ["usf4"]` (`routes/discovery.rs:72`).
3. `matches`, `lobbies` and `tournaments` have no `game` column; the player record (`/v1/players/{id}/record`) and lobby standings count every match, so SFV sets would be counted into USF4's record.
4. `/v1/assignments` (a player's matches) names no game, so a player's list can hold both games' matches and neither product can tell them apart: SFV Ember would offer a USF4 match (and fail at the claim) and the reverse.
5. The brackets service creates every set as `usf4` (`brackets/src/bracket/start.rs:321`).
6. The website's match page and home copy name USF4 only (`ember-short/deploy/site`).

**Plan, in order**
- **A. Bridge and protocol** (library, Rust; a deploy, the maintainer's yes): `check_provider_head` takes the games of `rooms::GAMES`; `edition_policy` stays required for `usf4` and is not asked of others; capabilities list both games; migration 059 adds `game TEXT NOT NULL DEFAULT 'usf4'` to `matches`, `lobbies` and `tournaments` (key-neutral, unlike 058: the old binary keeps working on it); a match stores its game; `/v1/assignments` rows carry `game`; the player record and lobby standings take `?game=` defaulting to `usf4`, so every existing response (BluMint contract v2 reads the record) is unchanged; tests pin that.
- **B. Helper and shared client:** the assignment row's game reaches the client; each product lists and plays only its own game's matches (`ProductIdentity::roomGame`), and a match link for another game says which game it is for. Shared C++, so the library's x86 (SF4) build and tests are owed before any SF4 release takes it.
- **C. SFV:** an end-to-end test on a local stack (`tools/runmatch.ps1`, `runpublic`'s bridge): an organiser creates an SFV match for two Ember IDs, both copies see it, claim, play, report, and the bridge completes it; plus `ember-sfv://tournament/open` handed to a running copy.
- **D. Brackets service:** a bracket's game from its connector or event config, `usf4` when unset.
- **E. Discord presence and invites:** a per-product Discord application id (`ProductIdentity`; the plan's `discordAppId` was never added) and image key; the mod starts the companion as `EmberHost` starts the helper (SFV's launcher hands the game nothing); Settings › Discord hidden when the product has no companion; package inventory. **Blocked on the maintainer** for the companion itself: the id is built in, and SFV Ember needs a Discord application of its own (or SF4's, which would show "SF4 Ember" as the game).

**Status (2026-11-08 evening)**
- **A** done: library `0b7af34c9`, `f48e63ef4`, `720535bad` (typed `matches::Game`, migration 059, games in capabilities, assignments and the record; brackets per tournament). Laptop: protocol 125, bridge 425, brackets 158 tests passed. Deploy notes: 059 is additive, so the old binary still runs on it (no restore needed to roll back; back up first anyway); without `?game=` the record is byte-identical; capabilities' `games` becomes `["usf4","sfv"]` (BluMint: check its client takes a longer list).
- **B** done: library `4b7af50ce` (merged `492d03bbc`): `DecodeAssignments(data, game)` keeps the product's game; the fuzz test covers it.
- **C** done: section 32 (`tools/runmatch.ps1` PASS first to 1 and to 2) and the match link (`tools/runlink.py`, section 32's last list).
- **D** done with A (brackets: `game` per tournament in the connector config or a desk event's settings, `usf4` unset).
- **E** partly: Settings › Discord shows only the account row (Connect Discord, used by rankings and tournaments) on a product without the companion (library `1af39bd99`), so SFV no longer shows activity and invitation rows that do nothing; and the companion compiles again (`b73fe0246`: `main.cxx` lacked the product identity's header since 11-04). The per-product Discord application id, the companion started by the mod and the package files wait on the maintainer's Discord application. Not seen in game: the Settings › Discord screen on SFV.
- **SF4's side of all library changes** (crash dumps, `--join-link` help, B, E, A's Rust suites): the library's full x86 build and ctest on the laptop (a detached worktree at `D:\sf4\x86\ember-library`, `build-current.ps1`) at `b73fe0246`: 236 of 236 passed (VideoEncoder and SpeechWine skipped as always), 1724 s.

**The maintainer's calls, with the default used until she says otherwise**
- SFV Ember's Discord application: needed for presence and invites (default: none; presence and invites off on SFV, the Settings screen hidden).
- Deploy order: bridge 059 (backup first) → brackets → site → an SFV release; nothing deployed by this work.
- How a partner names a bracket's game (BluMint, Challonge, start.gg): default `usf4` for every existing connection; SFV only where a connection or event says so.
- Website copy for SFV matches: drafted with the change, published only with her yes.
- Polish, Czech and Russian decline or transliterate "Anthony Danducci" in `about.ember_detail`, as their shared catalogs do.
- The words for "fighters and stages" in `runtime.build_mismatch` and `runtime.invite_other_build`, and the new SFV sentences (`replays.row_detail`, `replays.watch_detail`, `replays.not_added`, `inputs.unreadable_detail`, `home.replays_detail`, `home.training_detail`, `selection.not_owned`), have no shared wording to follow.

## 32. An organised match end to end (step C, 2026-11-08)

Step C of section 31: an organiser's SFV match, made on a bridge, played by two SFV Ember copies and completed by the bridge, on a local stack. Library `feat/ember-library` b73fe0246 (migration 059, `matches::Game`, `/v1/assignments` rows with `game`, `DecodeAssignments(data, game)`; no library change here); sfv-ember `main` 4ce25e9, 12c4c0a, a431068. Run on the laptop through the Ember Interactive task, as every SFV run there is.

**The pipe** (`src/ember/PublicCommands`, beside `ember-id`, `bridge`, `trust`, `public`, `ranking`). Each submits the runtime command the screen's button submits; nothing new happens in the runtime.
- `link <connection> <code>`: Linked accounts' Link account (`IdentityOp::LinkClaim` with the connection and the code, to the trusted service). `links`: its list (`LinkList`). `Ask` now takes a whole `IdentityRequest`, so these share the origin, bridge and ticket handling with `ember-id`, `bridge` and `trust`.
- `match refresh`, `match play <match id>`, `match stop`: the Matches screen's Refresh, a match's Play and Stop (`tournament::Command::Op::Refresh/Play/Stop`). From Play on, the runtime's own tournament play claims, opens or joins the room, binds it and reports.
- Status field `match`: the build the copy claims with, the play's phase (`idle`, `claiming`, `opening`, `in_room`, `finished`, `failed`), match and reason, the assignment list of the last refresh (bridge, error, a count of finished refreshes, and each row's match, state, game, slot, first-to, wins, profile and whether it is playable now), the service's connections, links and pending claims, the last claim's fingerprint, and the saved table rules.
- `rules <rounds> <seconds>` (`src/ember/RoomCommands`): Settings' table rules, saved (`SavePreferences`). A match's room is opened from the saved settings (`DispatchTournamentRoomCommand` copies `runtime->preferences`), whose default is 3 rounds of 99 s; the test plays 1 round of 30 s as the public room test does. `room` and `rules` share one rule check (`WithTableRules`).

**Linking: the real path.** No database seeding: the provider side is four HTTP calls with a provider credential (`ember-bridge credential <config> provider mock-local`), as the library's self-test makes them. For each copy the tool creates a link intent (`POST /v1/link-intents`, subject `player1` or `player2`), the copy claims its code (`link mock-local <code>`), the tool reads the intent and approves only a pending claim whose Ember ID is that copy's and whose fingerprint is the one the copy shows (`POST .../approve`), and the copy then lists the link. `tools/bridgedb.py` is used only for a read (`rows`): the USF4 match's row.

**`tools/runmatch.ps1`** (its header lists the checks). A scratch bridge on `127.0.0.1:18799` in `D:\sfv-re\scratch\match`, whose `mock-local` connection is the platform; two copies (`-sfve_pads=01`, `-sfve_background=1`, `-sfve_room=1`, seeded mash, nothing brought forward), each with its own Ember ID (the helper built `-Features selftest`). Linking as above; then a USF4 match for the same two players (ember-room-v1, `ultra_only`, this build): the bridge's database holds it for both, awaiting them, and both copies' refreshed lists are empty. It is cancelled, because the bridge allows one open match per player and connection (`ledger::busy`). Then the SFV match (`game: "sfv"`, `edition_policy` `not_applicable`, ember-room-v1, first to `-firstTo`, copy 0 in slot 0): both lists hold it alone, SFV's, playable, in their own slot. Both save the rules, pick and press Play; Ready at the bound table whenever it opens, until both say the match is finished. Then: both copies' logs agree on each game's winner, those games decide the set, the bridge holds the match `completed` with exactly those scores, and after a refresh both list it completed with the same score. The checkpoints are compared, and `%APPDATA%\SFVEmber\settings.json` is put back as it was. `-shots <folder>` takes the bound room, the fight and after.
- `tools/localstack.ps1` holds what `runpublic.ps1` and `runmatch.ps1` share (the pipe helpers, the scratch bridge's init and serve, the copies' launch, Ember IDs and trust, the end: desync lines, the copies closed, checkpoints, PASS or FAIL); `runpublic.ps1` now uses it and was run again (below).

**Evidence** (laptop, 2026-11-08; SFV build `sfv-ember/13173604/0.2/8dafe602f7615d90`; RYU vs CHUN-LI on S25, 1 round of 30 s)

| Run | Result |
|---|---|
| `runmatch.ps1 -shots D:\sfv-re\shots\match` | PASS. Two Ember IDs, each linked (`player1` fingerprint `vsrlowx7-4obzyrbq`, `player2` `ldr554cc-jgdf5usq`). USF4 match: the bridge held `{"game":"usf4","state":"awaiting_players","players":2}`; both copies listed 0 matches. SFV match `emt_7dc239d0...`: each copy listed `{"game":"sfv","gamesToWin":1,"playable":true,"profile":"ember-room-v1","slot":<its own>,"state":"awaiting_players"}`. Copy 0 "Tournament: opening the match's room", "room bound to match ... revision 1", "room match as P1"; copy 1 "joining the match's room", "room match as P2". Both logged the result `{"winner":1,"wins":[0,1],...,"finish":"time over"}` at frame 4586; the bridge: `completed`, scores copy 0 0 and copy 1 1, the game `accepted`; each copy's next claim was answered `stale_revision` (the match is over), it left the room, and its refreshed list showed the match `completed`, `wins [0,1]`. 77 checkpoints compared, 0 differing. |
| `runmatch.ps1 -firstTo 2 -lag 30` | PASS. The same checks; two games, both to copy 1 (the seeded mash is the same each game); the bridge `completed` 0 - 2 with both games accepted, both lists `completed` `[0,2]`; 154 checkpoints compared, 0 differing. |
| `runpublic.ps1 -presses 10 -games 2 -lag 30` (on `localstack.ps1`) | PASS: 2 games in a public room, 154 compared, 0 differing, the listing's faces the two shared picks. The supervisor `ember-rooms` was rebuilt on the laptop for it (its binary there was from 2026-10-20). |
| sfv-ember `scripts/build.ps1` | 5 of 5 suites; the bridge (`cargo build -p ember-bridge`) and the helper (`-Features selftest`, then plain again) built clean. |

Shot `D:\sfv-re\shots\match\match-fight-0.png` (laptop): the match's fight, "SFV 1" and "SFV 2" on SFV's HUD, Ember's line `0 - 0 · Ping 16 ms · Rollback 0f · Delay 1f`. `match-room-*.png` shows the game's own menu (an "About Grand Master League" notice), not Ember's room: the pipe plays the match without the shell open. Afterwards the selftest helper was replaced by the plain one (`sf4-net.exe` built and deployed again, hashes equal), the copies and the bridge were stopped and the scratch folders deleted. No crash dump came from these runs (the four on the laptop are from 15:53 to 16:50, before them).

**Left**
- **`ember-sfv://tournament/open`** handed to a running copy: **run afterwards (19:47)**, `tools/runlink.py` (sfv-ember 244c1d9): a second Launcher.exe with `ember-sfv://tournament/open?bridge=brg_...&match=emt_...` (a made-up service and match) logged "Started with a link to tournament match emt_..." and "Handed the tournament match link to the running game" and exited 0; the runtime logged "Tournament: a link opened match emt_..."; the copy, in its room, held it for Play (no bridge asked). PASS, with the room link in the same run. Playing the linked match from the link (a real bridge's match) is not run.
- **The shell's Matches screen** was not driven with the pad (the pipe stood in for it); its rows for SFV and a USF4 match's absence there are the same list the status reads.
- **A drawn game** (`replay_no_score`: the room plays it again) and **Stop** during a match were not exercised: the seeded mash decided every game, and no run stopped.
- **The input delay**: the copies asked for 2 (`delay 2`), the match played at 1 (the log's "delay 1", the HUD's "Delay 1f"); the match's room takes its delay by the match's own policy (`ember_existing_ready_policy`), not checked further.
- **The signed result** to the platform's `results_url` was not checked: the mock connection here has none (the library's self-test has a receiver for it, written in TypeScript; Node is not on the laptop).
- `runpublic.ps1` still leaves the shared settings as its copies saved them (its games count in the profile's online record, as section 28 noted); only `runmatch.ps1` puts them back. So the laptop's settings are as the `runpublic` regression left them (its games in the online record), not as they were before these runs.
- `runpublic.ps1`'s `-ranked` and `-shellJoin` paths were not run on `localstack.ps1`: they parse, and use only what the plain run used (`Shot`, `Rows`, `Press`, `Seek`), but their next run is their check.
- Steps A and B are the library's; their deploy (bridge 059 first, backup) and SF4's x86 build and tests before an SF4 release takes B are section 31's.

## 33. Discord's companion on SFV (step E, 2026-11-08)

The maintainer chose to reuse SF4 Ember's Discord application for SFV Ember ("Reuse SF4's app"). Discord names a player's activity after the application, so SFV players show under SF4 Ember's application name; the activity's own words (the library's `discord/Presence.hxx`, "In menus", "Fighting" and so on) and the large image (`ember`) are product-neutral.

- **Build** (`CMakeLists.txt`): `ember-discord.exe` from the library's `src/discord/main.cxx`, linked with SFV's product identity (`sfve_product`; the companion reads `Product().launcherExe` for Discord's launch command, so an invitation accepted in Discord starts SFV Ember's Launcher.exe with `--discord-launch`), and the application id, SDK version and archive hash of the library's `cmake/discord-sdk-pin.json`. The SDK comes only from the pinned archive, checked against the pin (the library's `sf4e_unpack_verified`); `scripts/build.ps1` passes it from `SF4E_DISCORD_SDK_ARCHIVE` (`-DiscordSdkArchive`). Without it the build makes no companion and says so. Beside it in the build tree: `discord_partner_sdk.dll`, the SDK's notice and `discord-build.json` (the library's form).
- **Start** (`src/ember/EmberHost.cxx`, `StartDiscordCompanion`): the mod starts it beside the helper, as SF4 Ember's launcher does for its game (`HelperProcess::Start` with the game's own pid, then `NetplayFacade::ConfigureDiscord` with its bootstrap), for the player's copy only (a test's second copy would show a second activity), and only when the file is beside the mod: Launcher.exe's package has it; the proxy's game folder does not (`tools/sfvelib.py` deploys the mod, the proxy and the helper only), so proxy test runs have no Discord.
- **Product and package**: `ProductIdentity::discordExe` is `ember-discord.exe`, so Settings > Discord shows activity and invitations again (section 31's E hid them while there was no companion). `scripts/package.ps1` stages the companion, its DLL, `discord-build.json` and `notices\Discord-SDK.txt`, and refuses a build without the companion; the package inventory requires them; the build receipt records the companion's hash.

**Evidence** (laptop, 2026-11-08 20:04-20:07): build ctest 5/5 with the companion built; `scripts\package.ps1` made a package holding `ember-discord.exe`, `discord_partner_sdk.dll`, `discord-build.json` and `notices\Discord-SDK.txt`, and its validation (the updater's checks) passed; a Launcher.exe run (`tools/runlink.py`, PASS) logged "ember: started the Discord companion".

**Not seen**: an activity in a Discord client (the laptop's Discord desktop app was not checked or signed in), an invitation sent and accepted (two Discord accounts, as SF4's own `docs/validation/DISCORD_VALIDATION.md` still owes), and Settings > Discord's status line on SFV.

## 34. Sol's review of sections 29-33 (2026-11-08)

The maintainer asked for Sol's reviews through Codex ("chatgpt has useage as well so use that for sol reviews"): `codex exec -m gpt-6.1-sol` at high effort, read-only, with the thermo-nuclear standard as the prompt and each round given the previous round's findings, until it approves. The prompts, findings and verdicts are in `reviews/2026-11-08-sfv-batch` beside the sf4 workspace. Two scopes: the library `f01ebcf29..HEAD` and sfv-ember's commits since 8c1fa05.

**sfv-ember: approved at round 3 (126e44b).**
- Round 1, fixed in f346b4c:
  - `runmatch.ps1` took its settings backup after setup, so a setup failure could lose the player's settings. It now takes it first.
  - The build receipt took an `ember-discord.exe` lying in the tree for this build's. It now asks CMakeCache whether this build made the companion.
  - The crash record's guard against two faults at once was a data race. It is now one atomic owner thread; a second faulting thread waits for the first record.
  - `Start-TestBridge` lost a bridge that never became ready. It now stops it.
  - `rules` overwrote other saved preferences. It now sets only the rounds and seconds.
- Round 2, fixed in 126e44b:
  - A fault inside a log write could reach `TryLogLine` on the thread already holding the log's lock. A thread-local flag now makes it return.
  - `runcrash.py` read a settings file it could not parse as "reports off". Only a file that parses and says so, or no file, counts.
  - `discord_partner_sdk.dll` and `discord-build.json` were outside the receipt's hashes. The receipt and `package.ps1` now cover all three companion files.

**Library: approved at round 3 (713f0dbda).**
- Round 1, fixed in 5d244d16e, 3da063256 and d83d1d71f:
  - A match's game stopped at the database. The ledger match now carries its typed `Game`, and `GET /v1/matches/{id}/link` answers `{over, game}`.
  - `/v1/assignments` limited the rows before filtering by game, so SFV history could push SF4's matches out. It now takes `?game=` and filters first (default `usf4`). The helper's assignment list and the C++ refresh (`WithGame`) pass the game.
  - SFV matches still had to carry `edition_policy` on the wire. It is now optional for SFV.
- Round 2, fixed in 713f0dbda: the match page drew an SF4 match first and patched it into SFV when the bridge answered, so Open could start SF4 Ember for an SFV match while the answer was pending. A match on Ember's bridges is now drawn once, after the answer or the 3 s wait, through one `draw` path. `asSfv` only changes the match model, and Open keeps `#` until then. A late SFV answer redraws the page for SFV Ember. A stub-page check in node covered pending, SFV, USF4, no game, a failed request, a late SFV answer and a finished match.

**Laptop retests after the fixes (2026-11-08):**
- sfv-ember (126e44b): `runcrash` (game and thread through Launcher.exe, game under the proxy) PASS; `runlink` PASS; the package holds the three companion files; build ctest 5/5.
- The library at d83d1d71f: x86 `build-current.ps1` (ctest 236) passed. Its one first-run failure, `ranking::a_player_out_of_usf4s_ranking_is_out_of_another_games_until_they_choose_there` (86399 for 86400), passed on the rerun. It predates this batch: the test read the standing a second after the choice on the real clock. 86c246224 lets that one read be `DAY - 1` (the two reads of a choice's own answer stay exact), and `cargo test -p ember-bridge --test ranking` passes 10/10.
- With the assignments game filter: the bridge and the selftest helper rebuilt from d83d1d71f, then `runmatch.ps1` PASS (an organised SFV match, first to 1, completed on the bridge, 77 checkpoints compared, 0 differing). After it the plain helper was built and deployed again (`sfv-build`, 5/5).

**The page's SFV wording** ("Your tournament match is ready to play in SFV Ember.", the steps, the phone note) is a draft for the maintainer; it goes live only with the site deploy, which waits for her yes.

## 35. SF4-only settings rows (2026-11-08)

Settings showed SFV players rows that only do something in SF4 Ember. The library (1afd3988c) names the game features behind them in `common/SettingsFeatures.hxx`, carried by the adapter's `Capabilities::settingsFeatures` and off until a game sets them; SF4's adapter sets `All()`, so SF4 Ember keeps every row. Every Settings builder's rows pass through one table (`Offered` in `ApplicationShell__Settings.cxx`), and a panel left with no row has no head. SFV's adapter (`SfvCapabilities`) sets none, so SFV hides:

- **Player page** (`inputDisplayFixes`): Lower input lag, Lower keyboard lag, Faster display, Borderless window. Only SF4's own pad, platform and battle code reads them.
- **Player page** (`padLayer`): Controllers (SFV lists no native controllers), Button layout, Game's button settings (its words are SF4's Options path and its layouts) and Game reads controllers. SFV has no pad layer; `padSetup`, the wizard inside it, stays off too.
- **Player page** (`keyboard`): Use keyboard. SFV lists no keyboard device and fights with pad 0 only.
- **Profile**: Share my Steam profile (`Capabilities::steamProfile`, now also `ShellView::offersSteamProfile`), and the room member's Steam profile action with it: SFV has no Steam module. Main character, and the main on the profile card, where the product's players have no main (`ember::PlayersHaveMain`: `Branding::roomFacesArePicks`, which SFV sets).
- **Interface > Sounds** (`challengerCall`, `lobbyMusic`): Ready sound, its volume and test (`PlayChallengerCall` returns false; the away sound is not built) and Lobby music (always greyed). The panel goes.
- **Interface > Room layout** (`characterPictures`): Pictures. SFV has no full-length character art, so both choices drew the portrait.
- **Accessibility > Help** (`shortcutHint`): Show shortcut hint. Only SF4's overlay draws "F10 or Start". The panel goes.

**Play in the background** stays and now works on SFV: the mod reads the saved setting from the library's settings store as the game starts (`SavedBackgroundPlay` in `SfvGame.cxx`, used by `dllmain.cxx` beside `-sfve_background`), so a change takes effect at the next start; SF4 applies it at once. The row's text says nothing of a restart.

Tests: the library's ShellJourney (`RowsFollowTheGame`: every feature off then on, the pad walking each page) and ProductRules / ProductRulesOn (Main character and the card); sfv-ember's `EmberAdapterTests` checks SFV sets none of the features.

## 36. Pads in the menu, the focus mark, the window (2026-11-09)

The maintainer's first test on her own PC (package 20261108-230721) found four things.

- **The pad could not move through Ember's menu.** Start opened it; the D-pad and A did nothing. SFV's exe imports `xinput1_3.dll`, and on Windows 11 26200+ System32's `xinput1_3.dll` (10.0.26200.1) is a shim whose `XInputGetState` and `XInputGetStateEx` call `xinput1_4.dll`'s through a function pointer. `mod/VirtualPads` hooks both libraries, so the game's one read entered the hooks twice: the inner call recorded the pad and ran the pad filter, which empties the state while Ember's menu holds the pads; the outer call then recorded that emptied state. `LastPadState` read nothing pressed whenever the menu was open, each read counted twice (about 120 a second on her PC against the laptop's 60), and `padSeen` changed on nearly every read. Start worked because it is read while the menu is closed. The laptop's xinput1_3 reads on its own and virtual pads never call the library, so no test met it. Now only the outermost hook call fills, records and filters; a nested call goes straight to the library and counts in the status field `padNested`. `VirtualPadsTests` reads through xinput1_3 and checks one read and at most one filter call per read; on a PC with the shim it proves the fix.
- **SF4-only rows.** Her package was built before section 35. Change controller (row `capture`) was still offered: SFV's battle always plays pad 0, so it joins the `padLayer` rows (library `Offered`; ShellJourney's `RowsFollowTheGame` lists it).
- **The focus mark bled.** The focused card went darkest, with a gold bar, a gold foot hairline and the card's border-tone edge around it. `ShellTheme::underlineFocus` is now `barFocus`: the focused card or row keeps only the full-height gold bar (whole pixels), the focused card drops its edge, and Band's `header` (every focused or chosen ground) moves from 0x060604 to 0x45433D, a step above hover on the same ramp. SF4's theme leaves the flag off and draws as before.
- **Full screen upset her monitor.** The launcher now always starts the game with `-windowed` (a plain window at the game's saved resolution; a test's own `--game-option -windowed` is not repeated).

Not done: the launcher does not yet warn about Steam's stale `ActiveProcess` pid, which crashes the game in its LoggedIn (`+0x742A34`); restarting Steam clears it.
