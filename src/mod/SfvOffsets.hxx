#pragma once

// Addresses in StreetFighterV\Binaries\Win64\StreetFighterV.exe (Steam build
// 13173604, PE timestamp 0x648AD3A4), derived from our own IDA database
// (D:\sfv-re\StreetFighterV.exe.i64). RVAs; the image is relocated.
//
// Every function the mod hooks or calls carries a 16-byte signature read from
// that exe. BuildCheck verifies the timestamp and every signature before the
// mod touches the game, and leaves it alone otherwise.

#include "mod/BuildCheck.hxx"

#include <stdint.h>

namespace sfve::mod::sfv {

constexpr uint32_t kExeTimestamp = 0x648AD3A4;

// void BattleFrameUpdate(BattleFrame*): one rendered frame of battle.
// Scheduler phase 1, then 1 + max(+0x1A4, +0x1A8) battle ticks (phase 2,
// each bumping +0x1B8), then phases 4 and 8; clears +0x1A8. Skips the ticks
// while paused (+0x190 set, +0x194 clear) and runs them only every
// (+0x1A0 + 1) frames in slow motion. The game's own rollback replays by
// setting +0x1A8. Reached through a vtable; hooked directly.
constexpr CodeSite kBattleFrameUpdate{"BattleFrameUpdate", 0x4DC6A0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x22, 0xFC, 0xFF, 0xFF, 0x48, 0x8B}};

// void InputRecord(InputUnit*, bool predictRemote): once per battle tick,
// writes each local pad's value for this tick (+0x118 + 4i) into that pad's
// history ring (+0x178 + 0x800 i: 128 slots of {tag, frame, value, valid}) at
// frame & 0x7F, then advances the frame counter (+0x2184).
constexpr CodeSite kInputRecord{"InputRecord", 0x43B310,
	{0x89, 0x54, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xB1, 0x7C, 0x21}};

// void RollbackUnitPre(RollbackUnit*): the battle tick's snapshot task. Opens
// a snapshot frame from the input unit's frame id; offline that id is -1 and
// nothing is stored. Hooked only to learn the unit (its manager).
constexpr CodeSite kRollbackUnitPre{"RollbackUnitPre", 0x486C40,
	{0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x49, 0x8D, 0x6B, 0x88, 0x48, 0x81, 0xEC, 0x68, 0x01, 0x00, 0x00}};

// RollbackManager* GetManager(void* unitContext): called with unit + 0xE8.
constexpr CodeSite kGetManager{"GetManager", 0x308070,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20, 0xE8}};

// bool ReceiveBattleFlowMessage(Receiver*, Message*): every battle-flow
// message; a state change (type id kStateMessageType) carries the new state's
// index at +12 into the state table (rva 0x39665E0, filled at rva 0x142840:
// 0 none, 1 BattleStart, 2 MatchStart, 3 IntroDemo, 4 RoundStart, 5 Ready,
// 6 Fight, 7 Finish, 8 RoundOver, ...; seen in game: 3, 5, 6 in that order).
// The flow runs outside the game's snapshot; SfvFlow keeps it in ours.
constexpr CodeSite kFlowMessage{"ReceiveBattleFlowMessage", 0x3D3ED0,
	{0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24}};

// SharedPtr* FindUnit(Battle*, SharedPtr* out, const TypeId*, uint32 index):
// the battle's unit of a type (how units find each other, e.g. rva 0x2E7F60).
constexpr CodeSite kFindUnit{"FindUnit", 0x4CE5D0,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48}};

// SharedPtr* FindPart(Unit*, SharedPtr* out, const TypeId*, uint32 index):
// the unit's part of a type (its parts at unit+192..+200, 16-byte shared
// pointers), the way the game's own lookups do it (e.g. rva 0x457F50).
constexpr CodeSite kFindPart{"FindPart", 0x4CE260,
	{0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}};

// bool ProjectileReleasable(ProjectileUnit*): whether a dead projectile may go
// back to its group's free pool (asked by the "Projectile PreMain" task, rva
// 0x484C70, for each retired projectile). True when it has no visual part
// or the visual's state (projectilevisual::kState) is 0.
constexpr CodeSite kProjectileReleasable{"ProjectileReleasable", 0x457F50,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8D, 0x3D, 0x9F, 0xED, 0x60}};

// BattleData* GetBattleData(void* unitContext): the battle's match data
// (rounds, results, flow flags), called with unit + 0xE8 the way the flow
// states call it. Mostly outside the game's snapshot (see battledata).
constexpr CodeSite kGetBattleData{"GetBattleData", 0x307F90,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20, 0xE8}};

// void* GameAlloc(size_t): the battle code's operator new (FMemory::Malloc
// with no alignment): units, parts (e.g. the factories at rva 0x2F4710 and
// 0x2F4830) and the closures of the flow's delegates, which the delegate's
// own destroy function frees. Hooked to zero what it hands out.
constexpr CodeSite kGameAlloc{"GameAlloc", 0x4EA630,
	{0x33, 0xD2, 0xE9, 0xB9, 0xCA, 0xC4, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};

// uint64 ShowErrorDialog(ErrorDialogService*, uint32 code): the error-dialog
// service's listener on the game's network-error broadcast (registered at
// rva 0x57ABF0; the broadcast drains the queue that rva 0x10F7070 fills).
// Looks the code up in its table (filled at rva 0x5CD7E0; 0x80003 ->
// "Dialog_ERROR_0030") and posts that dialog; returns 0 for a code it has no
// dialog for. While a dialog it opened is up, its count (+112) is above 0,
// and boot (rva 0x68B310) waits for that count to drop.
constexpr CodeSite kShowErrorDialog{"ShowErrorDialog", 0x570400,
	{0x40, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC1, 0x48, 0x81, 0xEC, 0xF8}};

// void* ShowNamedDialog(const FString* dialogId, const FString* argument):
// posts a dialog by its text id with no result callback.
constexpr CodeSite kShowNamedDialog{"ShowNamedDialog", 0x763170,
	{0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x49, 0x8D, 0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00}};

// void ChangeSceneState(SceneManager*, UClass* stateClass, UObject* initMessage):
// how every scene change starts (states call it through rva 0x584D20 with
// their manager, state + 768).
constexpr CodeSite kChangeSceneState{"ChangeSceneState", 0x584B70,
	{0x48, 0x85, 0xD2, 0x0F, 0x84, 0x99, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x89}};

// void SceneManagerTick(SceneManager*, float deltaSeconds): once a frame on
// the game thread, in every scene: ticks the current state (+904; a state
// whose tick says it has ended is destroyed) and then promotes the next one
// (+912).
constexpr CodeSite kSceneManagerTick{"SceneManagerTick", 0x59F540,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x48, 0x8B, 0xD9, 0x0F, 0x28}};

// The title state's step that leaves the title for the main menu: what its
// boot window's "Login" command (handler rva 0x5EA630) selects.
constexpr CodeSite kTitleLeaveStep{"TitleLeaveStep", 0x5E4D70,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48}};

// The battle jump (VersusLaunch).
// UClass* getters by package (L"/Script/KiwiGame"): the jump's init message
// and the jump state.
constexpr CodeSite kJumpMessageClass{"BattleJumpSetupStateInitMessage", 0xC24FB0,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xF5, 0x03, 0xE8, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};
constexpr CodeSite kJumpStateClass{"BattleJumpSetupState", 0xC1F470,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x3D, 0x5F, 0xE8, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};
// UObject* NewObject(UObject* outer, UClass*).
constexpr CodeSite kNewObject{"NewObject", 0x5DF2E0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x15}};
// The jump state's step that copies the players from the flow's loader into
// the battle setting (rva 0x7072A0), resets the flow and makes the next
// loader (type by the message's mode: 0 -> 4, 2 -> 0, other -> 3).
constexpr CodeSite kJumpCopyPlayers{"JumpCopyPlayers", 0x603120,
	{0x40, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x81, 0x50, 0x03, 0x00, 0x00, 0x48, 0x8B, 0xF1}};
// SharedRef* GetBattleFlow(SharedRef* out): the game's battle flow.
constexpr CodeSite kGetBattleFlow{"GetBattleFlow", 0x70C790,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xF2, 0x4E, 0xE1, 0xFF, 0x48, 0x85}};
// void EnsureLoader(Flow*, int providerType): makes flow+16 a loader whose
// provider (+16) is the flow's registered provider of that type.
constexpr CodeSite kEnsureLoader{"EnsureLoader", 0x4AA510,
	{0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF1, 0x48, 0x8B}};
// The game data object whose players hold the battle setting's entries, and
// its player reference by index (a virtual call).
constexpr CodeSite kGameData{"GameData", 0x521690,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x2B, 0x62, 0x86, 0x03, 0x48, 0x8B, 0x98}};
constexpr CodeSite kPlayerRef{"PlayerRef", 0x68D1E0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x8B, 0xDA, 0xFF, 0x90, 0x38, 0x02, 0x00}};
// void GetPlayerSetup(PlayerRef*, PlayerSetup* out): assigns the player's
// setting entry to a 96-byte PlayerSetup (strings freed and replaced).
constexpr CodeSite kGetPlayerSetup{"GetPlayerSetup", 0x7B5940,
	{0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x83, 0x79, 0x28, 0x00, 0x48, 0x8B, 0xFA, 0x48}};

// The jump state's second step (rva 0x602C80): after the first one made the
// new mode's data object. For local versus it only moves on; the mod puts
// the players and stage into the data object here, before the third step
// (rva 0x600AF0) copies it into the battle setting.
constexpr CodeSite kJumpSecondStep{"JumpSecondStep", 0x602C80,
	{0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x55, 0x49, 0x8D, 0x6B, 0xA1, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00}};
// Copy constructors of the mode data's fighter (CharPkg, 64 bytes) and match
// (SpecPkg, 40 bytes) records; FString assignment; the game's free.
constexpr CodeSite kCopyCharPkg{"CopyCharPkg", 0x581CD0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48}};
constexpr CodeSite kCopySpecPkg{"CopySpecPkg", 0x581EC0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F}};
constexpr CodeSite kAssignString{"AssignString", 0x190FF90,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41}};
constexpr CodeSite kGameFree{"GameFree", 0x1130C40,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D, 0xA8, 0x67, 0x99, 0x02}};

// The game's content checks (no ownership involved), used to refuse a launch
// with picks the game does not have rather than load them. All read the game
// singleton (kGameData) themselves; the first argument of the spec checks is
// unused.
// bool FighterInstalled(CharaAsset*, const FString* code): its package exists.
constexpr CodeSite kFighterInstalled{"FighterInstalled", 0x549180,
	{0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x33, 0xED, 0x89, 0x6C, 0x24, 0x70, 0xE8, 0x9E, 0x5E}};
// bool CostumeValid(void*, const FString* code, int costume)
constexpr CodeSite kCostumeValid{"CostumeValid", 0x56B190,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xDA}};
// bool ColourValid(void*, const FString* code, int costume, int colour)
constexpr CodeSite kColourValid{"ColourValid", 0x56B0E0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x41}};

// VersusBattleState (MatchEnd).
// void ShowResult(VersusBattleState*): its MatchEnd handler (vtable +0x5F0,
// called from the battle flow's event dispatch on code 14): marks the
// results as up and opens the results menu.
constexpr CodeSite kShowResult{"ShowResult", 0x66A0D0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0x3C, 0x04, 0x00, 0x00}};
// void RestartBattle(VersusBattleState*): the rematch "Continue" makes:
// broadcasts BattleRestart (flow code 18) to the battle synchronously.
constexpr CodeSite kRestartBattle{"RestartBattle", 0x684760,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41}};

// Content lists (GameContent): the singleton's spec data ("SpecDataAsset"),
// and bool List(SpecData** spec, TArray<FString>* out, bool includeRandom)
// for the selectable, installed fighters and the spec's stages (spec flags,
// no ownership).
constexpr CodeSite kSpecData{"SpecData", 0x68E7E0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x33}};
constexpr CodeSite kSelectableFighters{"SelectableFighters", 0x566FC0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x7A, 0x0C, 0x00, 0x48, 0x8B, 0x1A, 0x48, 0x89, 0x6C}};
constexpr CodeSite kSpecStages{"SpecStages", 0x5672D0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x7A, 0x0C, 0x00, 0x48, 0x8B, 0x1A, 0x48, 0x89, 0x6C}};

// UClass* MainMenuState(package): the main menu's class (scene vtable
// kMainMenuVtable); the results menu's "Exit" changes scene to it (rva
// 0x66A690), as the game's own exits from a battle do (rva 0x591950).
constexpr CodeSite kMainMenuClass{"MainMenuStateClass", 0xC20E70,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xAD, 0x47, 0xE8, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};

constexpr CodeSite kSites[] = {kBattleFrameUpdate, kInputRecord, kRollbackUnitPre, kGetManager, kFlowMessage,
	kFindUnit, kFindPart, kProjectileReleasable, kGetBattleData, kGameAlloc, kShowErrorDialog, kShowNamedDialog, kChangeSceneState, kSceneManagerTick, kTitleLeaveStep,
	kJumpMessageClass, kJumpStateClass, kNewObject, kJumpCopyPlayers, kGetBattleFlow, kEnsureLoader, kGameData,
	kPlayerRef, kGetPlayerSetup, kJumpSecondStep, kCopyCharPkg, kCopySpecPkg, kAssignString, kGameFree,
	kFighterInstalled, kCostumeValid, kColourValid, kShowResult, kRestartBattle, kSpecData, kSelectableFighters,
	kSpecStages, kMainMenuClass};

namespace versus {

constexpr uint32_t kResultPhase = 0x438;   // int: 0 none, 1 replay, 2 results up
constexpr uint32_t kVersionSelect = 0x43C; // byte: "Continue" opens a version select instead
constexpr int32_t kNoResult = 0;
constexpr int32_t kResultsUp = 2;

} // namespace versus

// The game singleton's content tables (from the embedded DA_CharaEntry and
// stage data; parsed at rva 0x56D050).
namespace content {

constexpr uint32_t kCharaAsset = 96;   // singleton: fighters (getter rva 0x2ACA980)
constexpr uint32_t kStageAsset = 104;  // singleton: stages (getter rva 0x1210160)
constexpr uint32_t kStageEntries = 40; // StageAsset: TArray data, then int count at +48
constexpr uint32_t kStageCount = 48;
constexpr uint32_t kStageStride = 80;  // entry: +0 FString code
// Codes in the table that are not playable stages: random (rand()), no stage
// data (BO3, BO4, SH1, SH5, S11, S12), and an empty entry.
constexpr const wchar_t* kUnplayableStages[] = {L"RND", L"BO3", L"BO4", L"SH1", L"SH5", L"S11", L"S12"};
constexpr const wchar_t* kRandomFighter = L"RND";

} // namespace content

namespace jump {

// The init message: +40 the target (0 vs CPU, 1 local versus, 2 training,
// -1 back to the main menu; the last step goes to VersusBattleState for 0
// and 1, TrainingBattleState for 2), +44 (training only) copy the setting's
// fighters, +45 swap sides in that copy, +46 run the ownership check (rva
// 0x5E2D10: replaces unowned costumes/colours/stages, the stage by rand(),
// and leaves the jump for an unowned fighter).
constexpr uint32_t kMessageMode = 40;
constexpr uint32_t kMessageCopySetting = 44;
constexpr uint32_t kMessageSwapSides = 45;
constexpr uint32_t kMessageCheckOwnership = 46;
constexpr uint32_t kJumpStateMessage = 848; // the jump state's init message
constexpr int kModeVersusCpu = 0;
constexpr int kModeVersus = 1;
constexpr int kModeTraining = 2;
constexpr int kModeBackToMainMenu = -1;
constexpr uint32_t kFlowLoader = 16;     // Flow: the loader
constexpr uint32_t kLoaderProvider = 16; // Loader: the mode's provider
constexpr int kProviderType = 14;        // a provider type registered by the flow reset (rva 0x71A790)
constexpr uint32_t kProviderPlayers = 8; // Provider: PlayerSetup[]
constexpr uint32_t kPlayerSetupSize = 96;
constexpr int kPlayers = 2;
// The mode data object's type for each target (the first step's mapping:
// 0 -> 4, 2 -> 0, else 3).
constexpr int kVersusCpuType = 4;
constexpr int kVersusType = 3;
constexpr int kTrainingType = 0;

// The mode data object's virtual functions (byte offsets in its vtable).
constexpr uint32_t kDataType = 8;       // int ()
constexpr uint32_t kDataSetChara = 56;  // void (int player, const CharPkg*): the fighter's setup
constexpr uint32_t kDataSetCode = 40;   // void (int player, const CharPkg*): the selected fighter
constexpr uint32_t kDataGetChara = 72;  // const CharPkg* (int player)
constexpr uint32_t kDataSetSpec = 104;  // void (const SpecPkg*)
constexpr uint32_t kDataGetSpec = 112;  // const SpecPkg* ()

// CharPkg: +0 FString fighter code, +16 costume, +20 colour, +24 TArray of
// 16-byte items, then ints and two bytes. SpecPkg: +0 byte, +4 time,
// +8 rounds, +16 FString stage code, +32..+34 bytes.
constexpr uint32_t kCharPkgSize = 64;
constexpr uint32_t kCharCode = 0;
constexpr uint32_t kCharCostume = 16;
constexpr uint32_t kCharColour = 20;
constexpr uint32_t kCharItems = 24;
constexpr uint32_t kSpecPkgSize = 40;
constexpr uint32_t kSpecTime = 4;
constexpr uint32_t kSpecRounds = 8;
constexpr uint32_t kSpecStage = 16;

} // namespace jump

namespace scene {

constexpr uint32_t kManagerCurrentState = 904; // (unrelated to the title state's own +904)
constexpr uint32_t kIdleStep = 0x12AF480; // an empty function: a state waiting on something else
constexpr uint32_t kManagerNextState = 912;
constexpr uintptr_t kMainMenuVtable = 0x2D98660; // MainMenuState (class getter rva 0xC20E70)
constexpr uintptr_t kVersusBattleVtable = 0x2DA9918; // VersusBattleState (class getter rva 0xC222F0)
constexpr uintptr_t kJumpStateVtable = 0x2D85920; // BattleJumpSetupState (its tick, rva 0x5F9FE0, at +0x580)

} // namespace scene

// The title screen's state (vtable rva 0x2D8AC78; its tick, rva 0x5FA730,
// counts +904 down and then calls the step at +824). After boot it opens the
// "AdvertiseWindow", a Scaleform window that runs the online sign-in, and
// waits (idle step) for the window's command; offline the window sends
// "Login" only once the offline notice is closed.
namespace title {

constexpr uintptr_t kVtable = 0x2D8AC78;
constexpr uint32_t kStep = 824;
constexpr uint32_t kStepDelay = 904;       // frames before the step runs (a field of the title state)
constexpr int kLoginDelayFrames = 60;      // what the "Login" command sets

} // namespace title

inline const GameBuild* VerifiedBuild()
{
	static const GameBuild build{"SFV CE build 13173604", kExeTimestamp, kSites, sizeof(kSites) / sizeof(kSites[0])};
	return &build;
}

namespace dialogs {

// The online start-up's downloads, as the game's error codes (shown in hex):
// EULA 0x80003 (task completion rva 0xF912F0), privacy policy 0x80047
// (0xF91660), URL list 0x80048 (0xF91710), GDPR text 0x8004A (0xF91370). They
// fail only because NetGuard blocks the CDN.
constexpr uint32_t kBlockedDownloadCodes[] = {0x80003, 0x80047, 0x80048, 0x8004A};

// "Gameplay will continue in offline mode": posted by boot (rva 0x68B310)
// after it sets the game's offline flag, and by the search-failure branch
// (rva 0x69CC40). Not "Dialog_QA_Y_0026", a yes/no question.
constexpr const wchar_t* kOfflineNotice = L"Dialog_INFO_0011";

} // namespace dialogs

// The battle's random number generators: objects {vtable, uint32 state} whose
// generator is an LCG (1664525, 1013904223; rva 0x32B2B0) and whose
// constructors seed the state from _time64 (rva 0x3C7DE0 and others), so two
// processes start with different seeds. One of them is a global object.
namespace rng {
constexpr uint32_t kVtable = 0x2C4F078;   // data: the generator class's vtable
constexpr uint32_t kGlobal = 0x3966538;   // data: the global generator object
constexpr uint32_t kState = 0x8;          // uint32 state after the vtable
// A fighter's generator is followed by the value its tick drew from it
// (rva 0x37D64E: fighter+0xC50 = generator at fighter+0xC40, next()); the
// game snapshots the pair as one region of this size. Reseeding the
// generator leaves the draw from before the battle, which differs per process.
constexpr uint32_t kFighterDraw = 0x10;
constexpr uint32_t kFighterPairSize = 24;
} // namespace rng

namespace flow {
constexpr uint32_t kStateMessageType = 0x3A651C0; // data: what a state-change message's type call returns
constexpr uint32_t kStateIndex = 12;              // int in the message
constexpr int kFight = 6;                          // players are fighting
} // namespace flow

namespace battleframe {
constexpr uint32_t kState = 0x190;      // pause, frame counters, slow motion, extra ticks, tick counter, clock
constexpr uint32_t kStateSize = 0x30;   // through +0x1BF
constexpr uint32_t kMinTicks = 0x1A4;   // ticks per frame = 1 + max(this, extra)
constexpr uint32_t kExtraTicks = 0x1A8; // the game's own rollback replays through this
constexpr uint32_t kTickCount = 0x1B8;  // bumped once per battle tick
// Counts since the battle frame was reset (rva 0x4D8830): updates (+0x198),
// updates that ran ticks (+0x19C) and the clock those advance by +0x1B0
// (+0x1B4, float). They include the updates a process runs before its layout
// binds, one or two more in one copy than the other. Only the slow-motion
// schedule reads +0x198 (rva 0x4DC6A0, 0x4D2850), and slow motion (+0x1A0) is
// only ever zeroed or copied in this build (rva 0x4D8830, 0x4E9230).
constexpr uint32_t kUpdates = 0x198;
constexpr uint32_t kTickedUpdates = 0x19C;
constexpr uint32_t kClock = 0x1B4;
} // namespace battleframe

namespace inputunit {
constexpr uint32_t kPadCount = 0xF8;
constexpr uint32_t kLocalMask = 0x100;
constexpr uint32_t kPadValues = 0x118;  // uint32 per pad: this tick's input
constexpr uint32_t kRings = 0x178;      // per pad: 128 x {tag, frame, value, valid}
constexpr uint32_t kRingStride = 0x800;
// Frame tag, record/consume frames, base frame, delay, sync state: the
// decompiled code uses dwords 2143..2180 (+0x217C..+0x2213). Kept outside
// the game's snapshot (its netcode resets them after a restore).
constexpr uint32_t kCounters = 0x2170;
constexpr uint32_t kCountersSize = 0xA8;
// QueryPerformanceCounter's low part when the unit starts its frame sync
// (rva 0x423630); online play measures time from it (rva 0x420170).
constexpr uint32_t kSyncClock = 0x2214;
} // namespace inputunit

namespace rollbackunit {
constexpr uint32_t kManagerContext = 0xE8; // GetManager(unit + this)
} // namespace rollbackunit

namespace unit {
// A unit's context (unit + 0xE8) holds at +8 an object whose weak pointer to
// the battle (the unit container) sits at +96.
constexpr uint32_t kContext = 0xE8;
constexpr uint32_t kContextOwner = 0x8;
constexpr uint32_t kOwnerBattle = 96;
constexpr uint32_t kFlowUnitType = 0x3A65188; // data: FlowUnit's type id (the round flow)
// data: the versus rule unit's type id (a RuleUnit subclass, vtable rva
// 0x2C70D98), which judges KOs and time-outs (task rva 0x487CD0).
constexpr uint32_t kVersusRuleUnitType = 0x3A723E0;
constexpr uint32_t kReplayUnitType = 0x3A69C48; // data: ReplayUnit's type id (vtable rva 0x2C6FF70)
constexpr uint32_t kCharaUnitType = 0x3A647C0;  // data: CharaUnit's type id, a fighter
} // namespace unit

// The rule unit judges a round over in its task (rva 0x487CD0): state 2 is
// "fighting", 3 "judged"; on a KO or time-out it stops the battle frame
// (+0x190 = 0x20), copies its round record (+252, in the game's snapshot) into
// the battle data and counts the round. The state itself is outside the
// snapshot.
namespace ruleunit {
constexpr uint32_t kJudgeState = 248;
} // namespace ruleunit

// A projectile's ProjectileVisualPart: its UE particle actor and the state
// of that actor (+232): 1 to create it, 2 playing, 3 the projectile died
// (set by the battle tick, rva 0x4576B0), 4 fading out, 0 gone. The visual
// task (rva 0x485140) turns 4 into 0 only once the live actor (+376, not in
// the snapshot) has cleared itself through a delegate (rva 0x48FD50) that
// the actor's own Tick (rva 0x5335A0) fires on the engine's frame clock,
// not the battle's.
namespace projectilevisual {
constexpr uint32_t kType = 0x3A66D00;
constexpr uint32_t kState = 232;
constexpr int32_t kStateDied = 3;
constexpr int32_t kStateFading = 4;
} // namespace projectilevisual

// The battle's match data (GetBattleData). The game's snapshot holds only
// +6176..+6404 of it; the round judge writes the rest when a round ends.
namespace battledata {
// +6404 match clock, +6408 rounds judged, +6416 seven round records of 69576
// bytes, +493448 the last judgement, +493484 the current round's record
// (+12 in it: the round-over reason the Fight state polls).
constexpr uint32_t kResults = 6404;
constexpr uint32_t kResultsSize = 563060 - 6404;
// Flags the Fight and Finish states set and clear on entry (0x3200: the
// fighters may act).
constexpr uint32_t kFlowFlags = 566176;
constexpr uint32_t kFlowFlagsSize = 4;
// int: _time64() when the match data is reset (rva 0x304EC0, on its +80
// record). A fighter's random generator is reseeded at every round start
// with side + this + rounds judged * 8 (CharaUnit, rva 0x37B43E), so two
// processes' fighters draw different numbers unless it is the same.
constexpr uint32_t kStartTime = 108;

// The match's result (MatchResult), all inside kResults..: the versus rule
// unit's tick (rva 0x487CD0) commits each judged round (judge rva 0x485240)
// into a record and, once a side has the wins needed (rounds / 2 + 1, rva
// 0x2F9F20), marks the match decided with its winner (rva 0x2F7FA0).
constexpr uint32_t kRoundsSetting = 8;     // int: rounds (best of N), from the match record's +8
constexpr uint32_t kRoundCount = 6408;     // int: rounds judged (at most 7 kept)
constexpr uint32_t kRounds = 6416;         // the round records
constexpr uint32_t kRoundStride = 69576;
constexpr int kMaxRounds = 7;
constexpr uint32_t kDecided = 493448;      // int: 1 once the match is decided
constexpr uint32_t kMatchWinner = 493452;  // int: side 0 or 1, -1 none (a draw)
// A round record: +0 the cause (2 a KO of some kind, 1 time out by health,
// 4 another time-out rule, 56/64/128/256 other rules), +4 the winning side
// (-1: none; such a round counts for both sides), +12 how it ended:
// 0 undecided, 1 KO, 2 double KO, 3 Critical Art, 4 EX, 5 chip, 6 perfect,
// 7 time over (the order of the game's finish icons, rva 0x76B520).
constexpr uint32_t kRoundCause = 0;
constexpr uint32_t kRoundWinner = 4;
constexpr uint32_t kRoundFinish = 12;
constexpr int32_t kCauseKo = 2;
// At a round's end the rule unit (rva 0x480120) copies the battle statistics
// (StatsUnit+248, see manager::kRunRules) into the round record after
// its first 64 bytes, filling it: restored, not hashed.
constexpr uint32_t kRoundStats = 64;
constexpr uint32_t kRoundStatsSize = kRoundStride - kRoundStats;
static_assert(kRounds + kMaxRounds * kRoundStride == kDecided, "the round records end where the judgement starts");
} // namespace battledata

// FlowUnit (rva 0x3C8860 builds it): the round flow's state machine. Its
// PreTick (rva 0x3DD890) switches to the requested state: the old state's
// Exit, the shared pointer moved, the new state's Enter; then the current
// state ticks.
namespace flowunit {
constexpr uint32_t kStatesBegin = 248;     // std::vector<std::shared_ptr<FlowState>>
constexpr uint32_t kStatesEnd = 256;
constexpr uint32_t kCurrent = 288;         // std::shared_ptr<FlowState> {object, control}
constexpr uint32_t kCurrentIndex = 304;    // index into the states
constexpr uint32_t kRequestedIndex = 308;  // differs from the current: switch next PreTick
constexpr uint32_t kRequestArg = 312;      // handed to the next state (its +8)
constexpr uint32_t kRequestFlag = 316;     // uint8
} // namespace flowunit

// A flow state (Ready, Fight, Finish, ...): phases run by a sequencer. A
// phase's function is bound into a delegate whose closure is a GameAlloc
// block; setting a phase (rva 0x414670) replaces the delegate.
namespace flowstate {
// The delegate the phase binder (rva 0x477FE0) builds: these four functions
// and a 16-byte closure {phase function, state}.
constexpr uint32_t kPhaseDelegateVtable = 0x305E758; // data
constexpr uint32_t kPhaseDelegateDestroy = 0x1099220;
constexpr uint32_t kPhaseDelegateCopy = 0x4C19E0;
constexpr uint32_t kPhaseDelegateInvoke = 0x447A60;
constexpr uint32_t kArg = 8;           // int
constexpr uint32_t kPhase = 64;        // int
constexpr uint32_t kDelegate = 72;     // {vtable, destroy, copy, closure, invoke}
// The sequencer that runs the delegate (rva 0x3E3680): the phase function
// runs once `elapsed` reaches `wait`; `elapsed` grows by `rate` (16.16) a
// tick while `enabled`. +132 is padding (it holds whatever the heap left).
constexpr uint32_t kWait = 112;        // float
constexpr uint32_t kElapsed = 116;     // float
constexpr uint32_t kRate = 120;        // int, 16.16
constexpr uint32_t kStep = 124;        // int, the last tick's step
constexpr uint32_t kEnabled = 128;     // int
// Setting a phase equal to +136 zeroes +140 (rva 0x414670).
constexpr uint32_t kMarkedPhase = 136; // int
constexpr uint32_t kMarkCount = 140;   // int
constexpr uint32_t kBaseSize = 144;    // the base class; +144.. is the state's own
struct ClassSize {
	uint32_t vtable;
	uint32_t size;
	bool keepOwn = true; // false: its own fields hold pointers the record must not restore
	// Bit k: the 8-byte word at kBaseSize + 8k holds heap leftovers until the
	// state first runs (seen differing between two copies); kept, not hashed.
	uint8_t leftoverWords = 0;
};
// Allocation sizes of the flow's state classes, read from the `mov ecx, size`
// before each allocation in the flow builders (rva 0x4257F0, and 0x423DD0 for
// 0x2C6A3F8); a class's size is the same in every builder. A state of a class
// not listed keeps only the base: its own fields are never read or written.
constexpr ClassSize kClassSizes[] = {
	{0x2C6A390, 152}, {0x2C6A4C8, 152}, {0x2C6A530, 152}, {0x2C6A598, 160},
	{0x2C6A600, 152}, {0x2C6A6D0, 144}, {0x2C6A7A8, 144}, {0x2C6A810, 144},
	{0x2C6A878, 184, true, 0x18}, {0x2C6A8E0, 144}, {0x2C6A948, 144}, {0x2C6A460, 144}, {0x2C6A2C0, 152},
	{0x2C6A328, 160}, {0x2C6AB00, 176}, {0x2C6A258, 160},
	// Own fields holding heap pointers: the builder's closure and callback
	// (0x2C6AA18), addresses seen at +152 (0x2C6A3F8) and +160 (0x2C6A668, Finish).
	{0x2C6AA18, 184, false}, {0x2C6A3F8, 160, false}, {0x2C6A668, 168, false}};
constexpr uint32_t kMaxSize = 184;
} // namespace flowstate

namespace manager {
constexpr uint32_t kRegionsBegin = 0x10;     // {void* ptr; uint64 size}[]
constexpr uint32_t kRegionsEnd = 0x18;
constexpr uint32_t kPostRestoreList = 0x38;  // std::list sentinel pointer
// Post-restore list node: fn(ctx, const FrameId*) when token != 0.
constexpr uint32_t kNodeToken = 0x18;
constexpr uint32_t kNodeContext = 0x28;
constexpr uint32_t kNodeFunction = 0x30;
// How registered runs of the game's snapshot are hashed, by owner type and
// run offset (RegionNames; docs/design/SFV_REGION_MAP.md). Every run is saved
// and restored; these are left out of the hash, or have a word masked.
//
// Presentation, which drifts on replay without the fight diverging (learned
// with the in-game synctest, -sfve_learn=1, 2026-11-04: during fighting only
// these differed, in about 70% of 7-frame rollbacks, while the fighters'
// regions never did): the camera (CameraUnit+0x100, 528 bytes), the fighters'
// centre and player lists (CharaManagerUnit+0x1C0, 112), each fighter's
// vectors rebuilt in the CharaManager tasks (CharaUnit+0x764, 300; its first
// 7 dwords are control state, see the region map, section 3, tier C), and a
// projectile's visual part (ProjectileVisualPart+0xC0 and +0xF0: its UE
// actor, reconciled after a restore by rva 0x482D10, so a replay can differ
// in the actor pointer and a frame counter).
//
// The battle statistics (StatsUnit+0xF8, 77804 bytes, registered at rva
// 0x4B0570): its task (rva 0x4BA550, after the fighters' tasks) adds each
// frame's time to a per-player bucket chosen by fighter-state predicates,
// and a frame replayed after a restore can land in a neighbouring bucket
// (the synctest with both players mashing: two counters 1000 apart, their
// sum equal; no fighter region differed). Only UI text and a mode score read
// them (rva 0x3EDCE0, 0x494FE0, 0x6536A0); the fight does not.
//
// The replay unit's tick counters (ReplayUnit+0xF8, 16): it counts battle
// ticks (four copies, task rva 0x48DF30) while the replay recorder records;
// the recorder (outside the snapshot) stops at a round's end, so a replay
// across a round's end counts fewer.
//
// A masked word: each fighter's CharaStatusPart registers part+0x238..+0x303
// as one run (rva 0x354260); its 8 bytes at +0x88 are part+0x2C0, the
// combo's copied move count, and +0x2C4, the opponent's move starts in the
// current combo (incremented when the other fighter enters a state, rva
// 0x356290, and reset by this fighter's own, rva 0x35DD20). When both
// fighters start a move on the same frame the result depends on which writes
// last, and the two copies (or a replay) can differ; only the Combo event's
// statistics listener reads it (rva 0x4B27D0).
struct RunRule {
	const char* owner;   // RegionNames' owner type
	uint32_t offset;     // the run's start in its owner
	const char* kind;    // non-empty: not hashed; the region name's suffix
	uint32_t maskedWord; // kNoMask, or the run-relative 8-byte word not hashed
};
constexpr uint32_t kNoMask = ~0u;
constexpr RunRule kRunRules[] = {
	{"CameraUnit", 0x100, ".presentation", kNoMask},
	{"CharaManagerUnit", 0x1C0, ".presentation", kNoMask},
	{"CharaUnit", 0x764, ".presentation", kNoMask},
	{"ProjectileVisualPart", 0xC0, ".presentation", kNoMask},
	{"ProjectileVisualPart", 0xF0, ".presentation", kNoMask},
	{"StatsUnit", 0xF8, ".stats", kNoMask},
	{"ReplayUnit", 0xF8, ".replaycount", kNoMask},
	{"CharaStatusPart", 0x238, "", 0x88},
};
} // namespace manager

} // namespace sfve::mod::sfv
