#pragma once

// Addresses in StreetFighterV\Binaries\Win64\StreetFighterV.exe (Steam build
// 13173604, PE timestamp 0x648AD3A4), derived from our own IDA database
// (D:\sfv-re\StreetFighterV.exe.i64). RVAs; the image is relocated.
//
// Every function the mod hooks or calls carries a 16-byte signature read from
// that exe, and the three spec finds a second one at their element step. BuildCheck verifies the timestamp and every signature before the
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

// This copy's install (no ownership involved): what GameContent reports as
// installed, and what a launch checks so it never loads content this copy
// lacks. Both take the fighter table (content::kCharaAsset) and find the
// fighter's entry by code (rva 0x57F030, the entry's +80 CostumeDatas).
// bool FighterInstalled(CharaAsset*, const FString* code): its package exists.
constexpr CodeSite kFighterInstalled{"FighterInstalled", 0x549180,
	{0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x33, 0xED, 0x89, 0x6C, 0x24, 0x70, 0xE8, 0x9E, 0x5E}};
// bool CostumePackage(CharaAsset*, const FString* code, int costume): the
// costume's CostumeDatas entry names a package (more than the empty
// FString's terminator) and that package exists (rva 0x1411EE840, after
// rva 0x141243030 makes the path). False for a costume number outside the
// entry's CostumeDatas. The battle's own load asks exactly this: for each
// player setting (1424-byte records, the costume at +176) rva 0x7B33B0,
// reached from the resource load at rva 0x69BC20, sets the costume to 0
// when it fails, and checks nothing else about the costume.
constexpr CodeSite kCostumePackage{"CostumePackage", 0x548E40,
	{0x40, 0x53, 0x55, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x33, 0xED, 0x49, 0x63, 0xD8, 0x48}};

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

// Content lists (GameContent): the singleton's spec data ("SpecDataAsset";
// its pick-space map is namespace spec, in SfvMenuLayouts.hxx).
//
// The game's stage listers are not used: neither is the same on every copy.
// SpecStageIds (rva 0x567B20) walks the spec's stage set and keeps each
// element whose flag at value+2 is set; the stages' refresh (rva 0x57E380,
// step 8) sets that flag to "package found" (value+3) and both of the row's
// codes granted by the account's entitlement cache (value+0, value+1: the
// type-3 set of rva 0x54F680). Only while the online identity is logged in
// does it append index 50 (rva 0x57F1A0). SpecStages (rva 0x5672D0) narrows
// that list to the installed stages again (rva 0x559540). So both depend on
// the install and on the account; GameContent reads the stage universe from
// the set's elements instead (EMBER_LIBRARY.md section 41).
constexpr CodeSite kSpecData{"SpecData", 0x68E7E0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xDB, 0x48, 0x8B, 0xF9, 0x33}};

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

// The spec's pick space (GameContent's fighter, costume and colour lists;
// the map's layout is namespace spec, in SfvMenuLayouts.hxx). The universe
// is every fighter, costume and colour with a CharaSpec row, found by key
// with the game's own lookups below, never through the game's listers: those
// keep only what the account's entitlement cache grants (EMBER_LIBRARY.md
// section 41).
//
// bool SpecReady(SpecData* spec): the spec's map is built and refreshed: its
// +232, which step 7 of the queue at rva 0x7A2FC0 sets once it has built the
// map (rva 0x56C260), and its +233, which step 8 sets once its three
// refreshes have run (rva 0x57CE10: fighters and costumes, rva 0x57CE50, then
// stages, rva 0x57E380). Step 8 sets it whatever the entitlement cache
// grants, so it says the same on every copy; the game asks it the same way
// (rva 0x7878F0, with the spec from kSpecData).
constexpr CodeSite kSpecReady{"SpecReady", 0x546710,
	{0x80, 0xB9, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0C, 0x80, 0xB9, 0xE9, 0x00, 0x00, 0x00, 0x00}};
// int* Find(TSet* set, int* outIndex, int key): the index of the set's
// element whose int key (element +0) is `key`, written to *outIndex and
// returned, or -1 for none. The set is UE4's TSet: +0 the elements' data,
// +56 the hash's inline bucket, +64 its allocation, +72 its size; a bucket is
// the key masked by size - 1, and each element holds the next index of its
// chain. The three share their first 80 bytes, so each also has a step site:
// the 16 bytes at its rva + 0x53, where it scales an index by its element's
// size, compares the key and starts to load the next index (whose offset the
// costumes' and colours' 16 bytes include). BuildCheck checks those too, so two swapped rvas, or a stride that is
// not the one the game's own find uses, fail the build check
// (sfv::kSpecFighterSet and the others bundle each with its stride).
// The fighters' set (spec::kFighters): 136-byte elements, next at +128.
constexpr CodeSite kFindSpecFighter{"FindSpecFighter", 0x54B7D0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0xDA, 0x48, 0x8B, 0xD1, 0x8B, 0x49, 0x48, 0x49, 0x63}};
// imul rdx, rcx, 88h; cmp [rdx+r8], ebx; jz; mov eax, [rdx+r8+80h].
constexpr CodeSite kFindSpecFighterStep{"FindSpecFighterStep", 0x54B823,
	{0x48, 0x69, 0xD1, 0x88, 0x00, 0x00, 0x00, 0x42, 0x39, 0x1C, 0x02, 0x74, 0x1D, 0x42, 0x8B, 0x84}};
// A fighter's costumes (spec::kFighterCostumes): 120-byte elements, next at +112.
constexpr CodeSite kFindSpecCostume{"FindSpecCostume", 0x54B8F0,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0xDA, 0x48, 0x8B, 0xD1, 0x8B, 0x49, 0x48, 0x49, 0x63}};
// imul rdx, rcx, 78h; cmp [rdx+r8], ebx; jz; mov eax, [rdx+r8+70h]; cmp.
constexpr CodeSite kFindSpecCostumeStep{"FindSpecCostumeStep", 0x54B943,
	{0x48, 0x6B, 0xD1, 0x78, 0x42, 0x39, 0x1C, 0x02, 0x74, 0x1A, 0x42, 0x8B, 0x44, 0x02, 0x70, 0x83}};
// A costume's colours (spec::kCostumeColours): 40-byte elements, next at +32.
constexpr CodeSite kFindSpecColour{"FindSpecColour", 0x54B860,
	{0x48, 0x89, 0x5C, 0x24, 0x08, 0x4C, 0x8B, 0xDA, 0x48, 0x8B, 0xD1, 0x8B, 0x49, 0x48, 0x49, 0x63}};
// lea rdx, [rcx+rcx*4]; cmp [r8+rdx*8], ebx (5 * 8 = 40); jz;
// mov eax, [r8+rdx*8+20h]; cmp.
constexpr CodeSite kFindSpecColourStep{"FindSpecColourStep", 0x54B8B3,
	{0x48, 0x8D, 0x14, 0x89, 0x41, 0x39, 0x1C, 0xD0, 0x74, 0x1A, 0x41, 0x8B, 0x44, 0xD0, 0x20, 0x83}};
// FString* CostumeName(void*, FString* out, const FString* code, int costume):
// the costume's FText (its spec value+8 object, +64) as shown, or
// "Costume<n>" where the spec has none (rva 0x55C5B0); the first argument is
// unused. It finds the costume by key and reads no flag, so every costume
// with a row has its name, entitled or not. The costume settings menu names
// its spin box's entries so.
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
// object rva 0x141E6A740 returns, its +40). With +96 clear the lists are the
// game's spec listers' (rva 0x566FC0 fighters, 0x54FAB0 costumes, 0x54F870
// colours), which keep only what the account's entitlement cache grants (the
// spec flags at value+0, set by the refresh at rva 0x57CE50 from the cache,
// UKWEntitlementsData) and, for fighters and costumes, what this copy has
// installed; the fighters are also those of the asset's select-grid list at
// +48. None of these paths calls Steam; the only Steam licence query in the
// exe (rva 0xE112C0: ISteamApps vtable +0x38 for app 475230, BIsDlcInstalled
// by the layout of the shipped Steamworks, Steamv130) is reached from three
// menu screens (rva 0xAAC150, 0xAADDE0, 0xAC6310), not from these. The cache
// is the account's: what Capcom's servers authorised at its last online
// session, and what it unlocked locally since (EMBER_LIBRARY.md section 41).
//
// UClass* BattleSetupClass(const TCHAR* package): the asset's class, cached at
// rva 0x3AA5C20 (L"/Script/KiwiGame"; the check's IsA test, rva 0x477A70).
constexpr CodeSite kBattleSetupClass{"BattleSetupClass", 0xC26230,
	{0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xE5, 0xF9, 0xE7, 0x02, 0x48, 0x85, 0xC0, 0x75, 0x21}};
// void OwnedFighters(Setup*, TArray<FString>* out, bool random, bool eleven):
// the selectable fighters whose package is installed (rva 0x566FC0, from
// the spec's selectable flag at a fighter's value+2, which needs its package
// and both of its codes in the entitlement cache) that the asset allows (rva
// 0x55A9B0: its
// list at +48 with +96 clear, else the restriction list); Random and ELEVEN
// appended on request. The check takes a fighter in this list (rva
// 0x56AF20), or one of a login-gated list's (rva 0x56B3A0, 0x565F20: lists
// held by the game instance's +144 object, read only with kLoggedIn).
constexpr CodeSite kOwnedFighters{"OwnedFighters", 0x54E8E0,
	{0x44, 0x88, 0x4C, 0x24, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48}};
// bool OwnedCostumes(Setup*, const FString* code, TArray<int>* out): the
// costumes the entitlement cache grants (spec flag value+0, rva 0x54F1F0)
// whose package is installed (the cached flag value+3, or kCostumePackage:
// rva 0x54FAB0, 0x558DC0), that the asset allows (rva 0x55AEF0); the costume
// settings menu's spin box lists these (rva 0x8CC6E0, case 3).
constexpr CodeSite kOwnedCostumes{"OwnedCostumes", 0x5545B0,
	{0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x41, 0x8B, 0x40, 0x0C}};
// void OwnedColours(Setup*, const FString* code, int costume, TArray<int>*
// out): with +96 clear, the costume's colours the entitlement cache grants
// (spec flag value+0) that are flagged at value+1 or numbered below 128 (rva
// 0x54F870, 0x54EE30, 0x558920); else the restriction list's colours of that
// costume. Numbers 0-99 are the colours "01".."99", 100 and up the EX colours
// "EX01".. (the menu's labels, rva 0x553B80).
constexpr CodeSite kOwnedColours{"OwnedColours", 0x55ABA0,
	{0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x53}};
// The stages' counterpart, OwnedStages (rva 0x550DA0, void (Setup*,
// TArray<FString>* out, bool random)), gives the game's installed stages
// (SpecStages, rva 0x5672D0, which also needs both of a stage's codes in the
// entitlement cache) that the asset lists at +184 and does not exclude at
// +168; the check's stage test (rva 0x56B920). Ember does not call it: a
// stage's `owned` is GameContent's kContentOwnership rule over the stage
// set's own flags (EMBER_LIBRARY.md section 41), so it is not a site here,
// where a verified site nothing calls could only fail the build check.
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
	kFighterInstalled, kCostumePackage, kShowResult, kRestartBattle, kSpecData, kMainMenuClass,
	kSetPlayerParam, kRequestExit, kStageName, kSpecReady, kFindSpecFighter, kFindSpecFighterStep, kFindSpecCostume,
	kFindSpecCostumeStep, kFindSpecColour, kFindSpecColourStep, kCostumeName, kTextToString, kCreateText,
	kTexture2DClass, kStaticLoadObject, kColourSwatch, kHudInvoke, kBattleSetupClass, kOwnedFighters, kOwnedCostumes,
	kOwnedColours, kLoggedIn, kVersionDataClass};

} // namespace sfve::mod::sfv

// The game objects' layouts, apart from the code sites above: the main
// menu's, the content tables' and the battle jump's, then the battle's and
// the rollback's. Every includer of this header sees them all.
#include "mod/SfvBattleLayouts.hxx"
#include "mod/SfvMenuLayouts.hxx"

namespace sfve::mod::sfv {

// One of the spec's three sets of the pick space, as GameContent finds an
// element of it: the game's own find, the find's step site (which BuildCheck
// checks, so the stride below is the one the game's find scales by), the
// element's size, and where the set is in what holds it (the spec, a
// fighter's value or a costume's value).
struct SpecSet {
	CodeSite find;
	CodeSite step;
	uint32_t stride;
	uint32_t set;
};
constexpr SpecSet kSpecFighterSet{kFindSpecFighter, kFindSpecFighterStep, spec::kFighterStride, spec::kFighters};
constexpr SpecSet kSpecCostumeSet{kFindSpecCostume, kFindSpecCostumeStep, spec::kCostumeStride, spec::kFighterCostumes};
constexpr SpecSet kSpecColourSet{kFindSpecColour, kFindSpecColourStep, spec::kColourStride, spec::kCostumeColours};
// The step sites encode the strides: imul's 32-bit and 8-bit immediates for
// the fighters and costumes, lea's *5 with a scale of 8 for the colours.
static_assert(kSpecFighterSet.step.bytes[3] == kSpecFighterSet.stride && kSpecFighterSet.step.bytes[4] == 0,
	"the fighters' stride is the one their find scales by");
static_assert(kSpecCostumeSet.step.bytes[3] == kSpecCostumeSet.stride, "the costumes' stride is the one their find scales by");
static_assert(kSpecColourSet.step.bytes[3] == 0x89 && kSpecColourSet.step.bytes[7] == 0xD0 && kSpecColourSet.stride == 5 * 8,
	"the colours' stride is the one their find scales by");

inline const GameBuild* VerifiedBuild()
{
	static const GameBuild build{"SFV CE build 13173604", kExeTimestamp, kSites, sizeof(kSites) / sizeof(kSites[0])};
	return &build;
}

} // namespace sfve::mod::sfv
