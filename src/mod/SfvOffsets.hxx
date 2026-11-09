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

// BattleFrame* BattleFrameCtor(BattleFrame*): the battle frame's constructor
// (sets its vtable, rva 0x2C742D8, whose +0x40 is BattleFrameUpdate). Its one
// caller (rva 0x4F4795) is a virtual of the frame's owner (slot at rva
// 0x2C7B638), which, when the owner's +0x300 holds no frame, allocates 0x1D0
// bytes with FMemory::Malloc (rva 0x4F4788 calls 0x11370F0), constructs the
// frame in them and installs the battle through the frame's +0x08 (rva
// 0x4DA710). Another virtual of the same vtable (slot at rva 0x2C7B458, rva
// 0x4EFBE0) resets the frame (+0x18, rva 0x4D8830), clears +0x300 and drops
// the frame. So the constructor runs once per battle, while the reset runs at
// that end, in the destructor (rva 0x4C7180) and at the install. Hooked so a
// new battle at the old one's address is still new (BattleIdentity).
constexpr CodeSite kBattleFrameCtor{"BattleFrameCtor", 0x4C5C40,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41}};

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

// UObject* KiwiGameInstance(): the game's instance object (a KiwiGame class,
// checked), or null. The InputUnit mirrors its input frame into it
// (inputunit::kInstanceInputFrame) every tick.
constexpr CodeSite kGameInstance{"KiwiGameInstance", 0x521690,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x2B, 0x62, 0x86, 0x03, 0x48, 0x8B, 0x98}};

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
// for the selectable, installed fighters, and for the spec's stages less
// those whose package this copy lacks (no ownership). kSpecStages keeps the
// order of kSpecStageIds' list and only the stages also in a second walk of
// the spec's stage map (rva 0x559540), which keeps an entry whose flag at
// +11 says it needs no package, the code RND, or one whose package the
// StageAsset entry's +16 path names exists on this install (rva 0x549500,
// then the package test at rva 0x11EE840). So two installs of the same
// build can list different stages: what this copy has installed, never what
// every copy shares (EMBER_LIBRARY.md section 38).
constexpr CodeSite kSpecData{"SpecData", 0x68E7E0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x33}};
constexpr CodeSite kSelectableFighters{"SelectableFighters", 0x566FC0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x7A, 0x0C, 0x00, 0x48, 0x8B, 0x1A, 0x48, 0x89, 0x6C}};
constexpr CodeSite kSpecStages{"SpecStages", 0x5672D0,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x7A, 0x0C, 0x00, 0x48, 0x8B, 0x1A, 0x48, 0x89, 0x6C}};
// bool SpecStageIds(SpecData** spec, TArray<int>* out): the stages the spec
// lets players select, with no package check: it walks the spec's stage map
// (spec+408, 48-byte elements, the StageAsset index at +0) in its order and
// keeps each entry whose flag at +10 is set, once; then, only while the
// online identity is logged in (rva 0x57F1A0, which asks kLoggedIn first), it
// appends index 50. Each value is an index into the StageAsset table
// (content::kStageEntries), whose entry's code kSpecStages turns it into
// (rva 0x547510). The same on every install of the build: Ember's stage
// universe, which the build id fingerprints.
constexpr CodeSite kSpecStageIds{"SpecStageIds", 0x567B20,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x48}};

// UClass* MainMenuState(package): the main menu's class (scene vtable
// kMainMenuVtable); the results menu's "Exit" changes scene to it (rva
// 0x66A690), as the game's own exits from a battle do (rva 0x591950).
constexpr CodeSite kMainMenuClass{"MainMenuStateClass", 0xC20E70,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xAD, 0x47, 0xE8, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};

// The engine's exit (GameExit). void RequestExit(bool force): with force,
// TerminateProcess; without, PostQuitMessage and the engine's "requesting
// exit" flag, so the main loop ends after this frame. The game's EXIT and
// QUIT console commands ("Closing by request", rva 0x1ACAB20), which a
// window close reaches, call it without force on the game thread; so do
// some 80 other callers.
constexpr CodeSite kRequestExit{"RequestExit", 0x11D0580,
	{0x48, 0x83, 0xEC, 0x28, 0x84, 0xC9, 0x74, 0x54, 0x48, 0x89, 0x5C, 0x24, 0x20, 0xE8, 0x8E, 0x14}};

// A stage's name as the game's menus show it (GameContent's StageName): the
// native behind KWStageEntryDataAsset's GetStageName (rva 0xD18B70).
// FString* StageName(StageAsset*, FString* out, const FString* code): finds
// the entry by code (case-insensitive), finds or loads the stage's own data
// asset (the entry's +32 path, rva 0x55E550) and copies its FText (+40) as
// shown; empty for a code not in the table. The game's favourite-stage
// screen calls it so (rva 0x812F70), with the stage asset (content::
// kStageAsset) and the out string freed by kGameFree.
constexpr CodeSite kStageName{"StageName", 0x564550,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x49, 0x8B, 0xD0, 0xE8, 0xEF, 0x9F, 0xFF}};

// The spec's pick space (GameContent's fighter, costume and colour lists).
// DA_Spec (/Game/Spec/DA_Spec, loaded at boot, rva 0x691790) holds a TMap
// keyed by a fighter's CharaUniqueID (spec+328, 136-byte elements, value at
// +8); a fighter's value holds a TMap of costumes (value+24, 120-byte
// elements, the costume's own value at +8), and each costume's value a TMap of
// colours (value+24, 40-byte elements). Each lister below walks one of those
// maps in its own order and appends the keys whose spec flags allow them:
// bool List(SpecData** spec, ..., TArray<int>* out).
//
// The fighters the spec lets players select (the record's flag at value+2):
// no package check; the game's kSelectableFighters keeps those of them whose
// package is installed, or whose flag at value+4 says they need none (rva
// 0x5591E0, FighterInstalled).
constexpr CodeSite kSpecFighters{"SpecFighters", 0x566AF0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55}};
// List(spec, uniqueId, out): the fighter's costumes the spec enables (the
// costume's flag at value+0), with no package check: the costume settings
// menu's spin box (rva 0x8CC6E0, case 3) lists these, less the unowned.
constexpr CodeSite kSpecCostumes{"SpecCostumes", 0x54F1F0,
	{0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x70, 0x41, 0x8B}};
// List(spec, uniqueId, out): kSpecCostumes less those this copy cannot load:
// a costume whose flag at value+3 is clear needs its CostumeDatas package
// (rva 0x558DC0, 0x548E40). kCostumeValid is membership in this list.
constexpr CodeSite kPlayableCostumes{"PlayableCostumes", 0x54FAB0,
	{0x48, 0x8B, 0xC4, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89}};
// List(spec, uniqueId, costume, out): the costume's colours the spec enables
// (flag at value+0) that are either flagged at value+1 or numbered below 128
// (rva 0x54EE30, 0x558920); no package check. kColourValid is membership in
// this list. Numbers 0-99 are the colours "01".."99", 100 and up the EX
// colours "EX01".. (the menu's labels, rva 0x553B80).
constexpr CodeSite kSpecColours{"SpecColours", 0x54F870,
	{0x48, 0x8B, 0xC4, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89}};
// FString* CostumeName(void*, FString* out, const FString* code, int costume):
// the costume's FText (its spec value+8 object, +64) as shown, or
// "Costume<n>" where the spec has none (rva 0x55C5B0); the first argument is
// unused. The costume settings menu names its spin box's entries so.
constexpr CodeSite kCostumeName{"CostumeName", 0x557870,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x41}};
// const FString* TextToString(const FText*): the text as shown, rebuilt in
// the current language first if it has to be (FText::ToString). The versus
// screen names a fighter so (rva 0x6DD620: PersonalData+80, "charaName").
constexpr CodeSite kTextToString{"TextToString", 0x116E270,
	{0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x83, 0x79, 0x10, 0x00, 0x48, 0x8B, 0x01, 0x48, 0x8B}};
// FText* CreateText(FText* out, const TCHAR* source, const TCHAR* textNamespace,
// const TCHAR* key): a text literal (NSLOCTEXT), looked up in the game's own
// localization data by namespace and key, the source string where it has
// none. The league names are built so (rva 0x6BDE20: "KW",
// "ID_CMN_LEAGUE_0000" as both source and key, then TextToString), and so are
// SFV Ember's (EMBER_LIBRARY.md section 28). The temporary FStrings it builds
// are its own.
constexpr CodeSite kCreateText{"CreateText", 0x2DBB50,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55}};

// The game's pictures (GameArt).
// UClass* Texture2DClass(const TCHAR* package): UTexture2D's class, cached at
// rva 0x3D91E68 (L"/Script/Engine").
constexpr CodeSite kTexture2DClass{"Texture2DClass", 0x1F3CF10,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x4D, 0x4F, 0xE5, 0x01, 0x48, 0x85, 0xC0, 0x75, 0x21}};
// UObject* StaticLoadObject(UClass*, UObject* outer, const TCHAR* name,
// const TCHAR* filename, uint32 flags, UPackageMap*, bool reconcile): finds
// the object or loads it, synchronously. The game loads its data assets so
// (rva 0x53D1D0, with flags 0 and reconcile true).
constexpr CodeSite kStaticLoadObject{"StaticLoadObject", 0x1242910,
	{0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x49, 0x8D, 0xAB, 0x18, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD8}};
// UClass* VersionDataClass(const TCHAR* package): KWVersionDataAsset's class,
// cached at rva 0x3AA6370 (L"/Script/KiwiGame"). The game loads its
// V-Trigger and V-Skill name assets as such (rva 0x76AFD0, 0x76AB40); see
// namespace choices.
constexpr CodeSite kVersionDataClass{"VersionDataClass", 0xC27870,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xF5, 0xEA, 0xE7, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};
// bool ColourSwatch(CharaAsset*, const FString* code, int costume, int
// colour, uint32 out[2]): the two colours the costume settings menu shows for
// a colour (rva 0x8CC6E0, case 4), as 0xRRGGBB. It finds, or loads, the
// costume's DA_<x>_CostumeColorPreview_<nn> (named by the fighter's
// CostumeColorPreviewDatas entry) and reads its palette: +40 the colours
// 0-99, +56 the EX colours, 8 bytes each (rva 0x553C70). False, with both
// white, for a costume with no preview data.
constexpr CodeSite kColourSwatch{"ColourSwatch", 0x554750,
	{0x44, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x53, 0x57, 0x41, 0x55, 0x41}};

// The battle HUD's own text (HudText).
// void HudInvoke(HudMovie*, const FString* method, float number, const
// FString* text): calls the HUD's Scaleform movie's ActionScript `method`
// with (number, text) as its arguments, through the movie's player (+0x318,
// then +0x28; rva 0x73DDA0); nothing for a movie without one. The player
// copies the text into its own string table (rva 0xEEBE40), so the caller's
// FString need only live for the call.
// The battle HUD's setup (rva 0x3F9260, a HudUnit virtual) names each side,
// the side (0 or 1) as the number:
//   "SetPilotName" (rva 0x3FD223): the plate on the life bar, the label
//     HudPilotLabel (rva 0x416510) picks: ID_SYS_HUD_0015 or _0016 by side
//     ("PLAYER 1", "PLAYER 2" in the log), _0011 or _0017, or a name;
//   "SetPlayerName" (rva 0x3FD150 online, 0x3FD428 offline): the fighter's
//     name under the bar.
// The HUD's update (rva 0x403220) names a plate again, at rva 0x4037B8, when
// HudPilotLabel reports its label changed (rva 0x403709). The marker over
// each fighter (rva 0x4041D0) takes the same labels by side (_0015, _0016,
// _0017 or a name) through "SetCursorText", at rva 0x4047C3, with the side
// as the number. Nothing else calls these methods.
constexpr CodeSite kHudInvoke{"HudInvoke", 0x73D600,
	{0x48, 0x85, 0xC9, 0x74, 0x15, 0x48, 0x8B, 0x89, 0x18, 0x03, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74}};

// What the game lets this copy's player pick (GameContent). The menus and
// the battle jump's check (rva 0x5E2D10) ask the battle setup data asset
// (UKWBattleSetupDataAsset, 344 bytes,
// /Game/GameFlow/CommonSceneStates/BattleSetup/BattleSetupDataAsset; the
// check finds it so, rva 0x551520 and 0x477A70). Its +96 byte restricts the
// picks to the asset's own list at +120 (TArray of 24-byte
// KWBattleSetupCostumeColor: +0 FString fighter code, +16 costume, +20
// colour), less a set at +104, each entry also passing the filter object at
// +280 when one is installed (its vtable +8; a BattleSetupState's constructor
// installs one, rva 0x5A4D50 and 0x574510, from a list it copies from the
// object rva 0x141E6A740 returns, its +40). With +96 clear the costume and colour lists are the
// spec's and the install's alone, and the fighters those of the asset's
// select-grid list at +48 that are installed. None of these paths calls
// Steam; the only Steam licence query in the exe (rva 0xE112C0: ISteamApps
// vtable +0x38 for app 475230, BIsDlcInstalled by the layout of the shipped
// Steamworks, Steamv130) is reached from three menu screens (rva
// 0xAAC150, 0xAADDE0, 0xAC6310), not from these.
//
// UClass* BattleSetupClass(const TCHAR* package): the asset's class, cached at
// rva 0x3AA5C20 (L"/Script/KiwiGame"; the check's IsA test, rva 0x477A70).
constexpr CodeSite kBattleSetupClass{"BattleSetupClass", 0xC26230,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xE5, 0xF9, 0xE7, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};
// void OwnedFighters(Setup*, TArray<FString>* out, bool random, bool eleven):
// kSelectableFighters (installed) that the asset allows (rva 0x55A9B0: its
// list at +48 with +96 clear, else the restriction list); Random and ELEVEN
// appended on request. The check takes a fighter in this list (rva
// 0x56AF20), or one of a login-gated list's (rva 0x56B3A0, 0x565F20: lists
// held by the game instance's +144 object, read only with kLoggedIn).
constexpr CodeSite kOwnedFighters{"OwnedFighters", 0x54E8E0,
	{0x44, 0x88, 0x4C, 0x24, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48}};
// bool OwnedCostumes(Setup*, const FString* code, TArray<int>* out):
// kPlayableCostumes that the asset allows (rva 0x55AEF0); the costume
// settings menu's spin box lists these (rva 0x8CC6E0, case 3).
constexpr CodeSite kOwnedCostumes{"OwnedCostumes", 0x5545B0,
	{0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x41, 0x8B, 0x40, 0x0C}};
// void OwnedColours(Setup*, const FString* code, int costume, TArray<int>*
// out): kSpecColours with +96 clear, else the restriction list's colours of
// that costume.
constexpr CodeSite kOwnedColours{"OwnedColours", 0x55ABA0,
	{0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x53}};
// void OwnedStages(Setup*, TArray<FString>* out, bool random): kSpecStages
// that the asset lists at +184 and does not exclude at +168; the check's
// stage test (rva 0x56B920).
constexpr CodeSite kOwnedStages{"OwnedStages", 0x550DA0,
	{0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC}};
// bool LoggedIn(): the default online subsystem (rva 0x73BEC0, from the
// "OnlineSubsystem" module), its interface from vtable +80, reports status 2
// from vtable +288 for the local user (UE4's ELoginStatus::LoggedIn is 2).
// Offline it does not (GameContent: "logged in 0"). The login-gated lists
// and the shop's fighter list are read only then (rva 0x565F20, 0x551E50).
constexpr CodeSite kLoggedIn{"LoggedIn", 0x742A20,
	{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0xE8, 0x91, 0x94, 0xFF, 0xFF, 0x48}};

// bool SetPlayerParam(ParamTable*, int param, int value): one of a player's
// 0x2A battle parameters ({value, default, min, max}, 16 bytes each); refuses
// a parameter past 0x29 or a value outside {min, max}, and -2 restores the
// default. The game's own writers: KWBattlePlayerSetting's SetGaugeVitalType
// (rva 0x7C7B50: table = setting + 560 + 1424 * player, param 22) and
// Training's menu (rva 0x4A86F0), which writes params 22, 24, 26 and 27 of
// both players into GetBattleData's tables (+560, +1984) and into each
// fighter's own copy (CharaUnit + 864). See namespace practice.
constexpr CodeSite kSetPlayerParam{"SetPlayerParam", 0x309330,
	{0x4C, 0x8B, 0xC9, 0x83, 0xFA, 0x29, 0x77, 0x49, 0x48, 0x63, 0xCA, 0x41, 0x83, 0xF8, 0xFE, 0x74}};

constexpr CodeSite kSites[] = {kBattleFrameUpdate, kBattleFrameCtor, kInputRecord, kRollbackUnitPre, kGetManager, kFlowMessage,
	kFindUnit, kFindPart, kProjectileReleasable, kGameInstance, kGetBattleData, kGameAlloc, kShowErrorDialog, kShowNamedDialog, kChangeSceneState, kSceneManagerTick, kTitleLeaveStep,
	kJumpMessageClass, kJumpStateClass, kNewObject, kJumpCopyPlayers, kGetBattleFlow, kEnsureLoader, kGameData,
	kPlayerRef, kGetPlayerSetup, kJumpSecondStep, kCopyCharPkg, kCopySpecPkg, kAssignString, kGameFree,
	kFighterInstalled, kCostumeValid, kColourValid, kShowResult, kRestartBattle, kSpecData, kSelectableFighters,
	kSpecStages, kMainMenuClass, kSetPlayerParam, kRequestExit, kStageName, kSpecFighters, kSpecCostumes, kPlayableCostumes,
	kSpecColours, kCostumeName, kTextToString, kCreateText, kTexture2DClass, kStaticLoadObject, kColourSwatch, kHudInvoke,
	kBattleSetupClass, kOwnedFighters, kOwnedCostumes, kOwnedColours, kOwnedStages, kLoggedIn, kSpecStageIds,
	kVersionDataClass};

} // namespace sfve::mod::sfv

// The game objects' layouts, apart from the code sites above: the main
// menu's, the content tables' and the battle jump's, then the battle's and
// the rollback's. Every includer of this header sees them all.
#include "mod/SfvBattleLayouts.hxx"
#include "mod/SfvMenuLayouts.hxx"

namespace sfve::mod::sfv {

inline const GameBuild* VerifiedBuild()
{
	static const GameBuild build{"SFV CE build 13173604", kExeTimestamp, kSites, sizeof(kSites) / sizeof(kSites[0])};
	return &build;
}

} // namespace sfve::mod::sfv
