#pragma once

// The layouts of the game objects outside a battle, in StreetFighterV.exe
// (Steam build 13173604; SfvOffsets.hxx, which includes this, has the
// build and its code sites): the versus result screen, the content tables,
// the spec's pick-space map and the V-Skill and V-Trigger name assets, textures, the battle jump and
// its records, the scene states, the title screen, and the start-up dialogs.
// Offsets in bytes; addresses are RVAs.

#include <stdint.h>

namespace sfve::mod::sfv {

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

// CharaAsset: the DA_CharaEntry records (parsed at rva 0x56D050), a TArray of
// 232-byte entries, which the game searches by a spec id as GameContent does
// (rva 0x547160). An entry: +0 FString CharaCode, +16 CharaProgID,
// +20 CharaUniqueID (the spec's key), +24 the loaded PersonalData object
// (KWCharaPersonalDataAsset), +48 SetupData (FString, the fighter's battle
// setup), +64 PreviewSetupData, +80 CostumeDatas (TArray<FString>, indexed by
// costume number), +112 CostumeColorPreviewDatas, +128 the loaded
// BattleResourceList. ELEVEN's entry has no SetupData and no
// BattleResourceList: the select screen decides which fighter Eleven plays
// as (bIsDecidedCharaEleven_Left/Right), and a battle cannot load ELV itself.
constexpr uint32_t kCharaEntries = 40;
constexpr uint32_t kCharaCount = 48;
constexpr uint32_t kCharaStride = 232;
constexpr uint32_t kCharaUniqueId = 20;
constexpr uint32_t kCharaPersonal = 24;
constexpr uint32_t kCharaSetupData = 48;
// CostumeDatas: a TArray of FStrings (16 bytes each), its count at +88; a
// costume number is a place in it.
constexpr uint32_t kCharaCostumeDatas = 80;
constexpr uint32_t kCharaCostumeCount = 88;
constexpr uint32_t kGameStringStride = 16;
// PersonalData: the fighter's name (FText), as the versus screen's
// "charaName" (rva 0x6DD620).
constexpr uint32_t kPersonalName = 80;

} // namespace content

// DA_Spec's pick-space map (EMBER_LIBRARY.md section 41). The spec's
// post-init (rva 0x574150) parses the CharaSpec table embedded in the exe
// (one row per fighter, costume and colour: Name, CharaId, RefAvailableCode,
// RefVisibleCode, CostumeId, RefCostumeAvailableCode, CostumeText, ColorId,
// RefColorAvailableCode). Then a queued step list (rva 0x7A2FC0, one step a
// tick) builds the map. Step 7 (rva 0x56C2B0) adds an element for every
// fighter table entry with a CharaUniqueID of 0 or more, in the table's
// order; under it one for every costume number of its CostumeDatas, and
// under each costume one for every colour 0-127; and links each CharaSpec
// row to its element (value +8). Step 8's refresh (rva 0x57CE50) sets each
// element's flag at value+0 from the account's entitlement cache: the row's
// RefAvailableCode (a fighter), RefCostumeAvailableCode (a costume) or
// RefColorAvailableCode (a colour) granted. An element without a row is one
// the game has no data for; every element with one is in the universe.
//
// Each set is a UE4 TSet (+0 its elements' data) found by key with the
// game's own lookups (sfv::kFindSpecFighter and the others); an element is
// its int key, then its value at +8.
namespace spec {
constexpr uint32_t kFighters = 328; // SpecData: the fighters' set, keyed by CharaUniqueID
constexpr uint32_t kSetElements = 0;
constexpr uint32_t kValue = 8;
constexpr uint32_t kFighterStride = 136;
constexpr uint32_t kCostumeStride = 120;
constexpr uint32_t kColourStride = 40;
// A value's fields, the same for the three kinds.
constexpr uint32_t kEntitled = 0; // byte: the row's code is in the entitlement cache
constexpr uint32_t kRow = 8;      // the linked FKWCharaSpec row; null for none
// A fighter's value: its costumes' set, keyed by costume number. (Its +2 is
// kSpecFighters' selectable flag, +4 its cached package test.)
constexpr uint32_t kFighterCostumes = 32;
// A costume's value: its colours' set, keyed by colour number. (Its +3 is
// its cached package test, set only for the costumes the refresh asks.)
constexpr uint32_t kCostumeColours = 24;
// The builder adds colours 0 to 127 under every costume.
constexpr int kColourKeys = 128;
} // namespace spec

// A fighter's V-Trigger and V-Skill names (/Game/Chara/DA_VTriggerNameAsset
// and DA_VSkillNameAsset, KWVersionDataAsset). The game's lookup (rva
// 0x567EA0) walks the asset's array, compares each entry's code with the
// fighter's (ignoring case) and takes the matching entry's names; their count
// is the fighter's number of choices, and the select and VS screens index
// them by CharPkg +48 and +56. Whether the lookup stops at the first match or
// gathers the names of every match has not been checked against the game:
// GameContent gathers every match's, which is the same while each fighter
// has one entry. The layout below is known only from reading the exe, so
// GameContent walks no more than kMaxEntries entries and kMaxNames names of
// an entry; an asset past either reads as not there.
namespace choices {
constexpr uint32_t kEntries = 40; // asset: TArray data, then int count at +48
constexpr uint32_t kCount = 48;
constexpr uint32_t kEntryStride = 32; // entry: +0 FString fighter code
constexpr uint32_t kNames = 16;       // entry: TArray data, then int count at +24
constexpr uint32_t kNameCount = 24;
constexpr uint32_t kNameStride = 40; // an FText
constexpr int kMaxEntries = 1024;
constexpr int kMaxNames = 8;
} // namespace choices

// A loaded UTexture2D (UE 4.7.6) and its render resource.
namespace texture {
// UTexture's FTextureResource*, and the resource's RHI texture
// (FTexture::TextureRHI, after FRenderResource's 48 bytes: its InitResource,
// rva 0x155A4C0, links +16..+39 and sets +40). A material's texture
// parameter is set so (rva 0x17397F0: texture+64, then +48).
constexpr uint32_t kResource = 0x40;
constexpr uint32_t kTextureRhi = 0x30;
// FTexture2DResource's owner, the texture itself.
constexpr uint32_t kResourceOwner = 0x68;
// UTexture2D's FTexturePlatformData*: +0 SizeX, +4 SizeY, +8 slices, +12 the
// pixel format (GetSizeX, rva 0x1CB9410).
constexpr uint32_t kPlatformData = 0xC0;
// The D3D11 RHI texture's ID3D11Texture2D (FD3D11TextureBase's resource). Not
// read from code: found at run time on a loaded texture, and checked on
// every texture before use (GameArt: its vtable is a D3D11 texture's, and
// its size is the platform data's).
constexpr uint32_t kD3DTexture = 0x68;
} // namespace texture

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
// The ownership check's result, once it has run (rva 0x602790, the step
// after the second): 0 when it changed nothing; 0x210 a fighter it does not
// allow (that player's costume and colour set to 0; the jump then leaves
// with the error notice Notice_ERROR_0030, rva 0x602830), 0x102 a costume set
// to 0, 0x101 a colour set to 0, 0x108 a stage it does not allow replaced
// (by TRN or S25, in one branch chosen by rand(): rva 0x5E3082-0x5E360A),
// 0x400 the setup asset missing.
constexpr uint32_t kJumpCheckResult = 856;
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

// CharPkg (CopyCharPkg copies every field): +0 FString fighter code, +16
// costume, +20 colour, +24 TArray of 16-byte items, +40 int (default 1),
// +44 int (default -1), +48 V-Trigger, +52 int, +56 V-Skill (0 is I, 1 is
// II), +60 and +61 bytes. The select screen's ChangeCostume (rva 0x592C50)
// stores the two choices at +48 and +56 and applies the record through the
// same two setters WriteFighter calls (rva 0x6C8540); the battle setting
// copies them to its player record's +220 and +228 (rva 0x7CAA40). A fresh
// mode data object's default record holds 0 in both (rva 0x70C180).
// SpecPkg: +0 byte, +4 time, +8 rounds, +16 FString stage code, +32..+34 bytes.
constexpr uint32_t kCharPkgSize = 64;
constexpr uint32_t kCharCode = 0;
constexpr uint32_t kCharCostume = 16;
constexpr uint32_t kCharColour = 20;
constexpr uint32_t kCharItems = 24;
constexpr uint32_t kCharVTrigger = 48;
constexpr uint32_t kCharVSkill = 56;
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
// TrainingBattleState: the jump's target 2 (class getter rva 0xC22130, "TrainingBattleState" registered by rva
// 0xBF3B20 under FightRequestBattleStateBase, instance size 0x458; its constructor, rva 0x6606D0, sets this
// vtable). Its pause menu's commands go to the vtable's +0x5E0 (rva 0x66A310, the only vtable that holds it):
// "EXIT_BATTLE" asks the state's scene manager (state+768) for MainMenuState with no message (rva 0x584D20, which
// calls kChangeSceneState), the change MatchEnd's LeaveBattle asks for; "BACK_TO_CHARASELECT" asks for the
// class at rva 0xC220F0 instead.
constexpr uintptr_t kTrainingBattleVtable = 0x2DA8B70;

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

} // namespace sfve::mod::sfv
