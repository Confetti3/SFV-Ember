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
// The flow runs outside the battle's rollback snapshot.
constexpr CodeSite kFlowMessage{"ReceiveBattleFlowMessage", 0x3D3ED0,
	{0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24}};

constexpr CodeSite kSites[] = {kBattleFrameUpdate, kInputRecord, kRollbackUnitPre, kGetManager, kFlowMessage};

inline const GameBuild* VerifiedBuild()
{
	static const GameBuild build{"SFV CE build 13173604", kExeTimestamp, kSites, sizeof(kSites) / sizeof(kSites[0])};
	return &build;
}

// The battle's random number generators: objects {vtable, uint32 state} whose
// generator is an LCG (1664525, 1013904223; rva 0x32B2B0) and whose
// constructors seed the state from _time64 (rva 0x3C7DE0 and others), so two
// processes start with different seeds. One of them is a global object.
namespace rng {
constexpr uint32_t kVtable = 0x2C4F078;   // data: the generator class's vtable
constexpr uint32_t kGlobal = 0x3966538;   // data: the global generator object
constexpr uint32_t kState = 0x8;          // uint32 state after the vtable
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
} // namespace inputunit

namespace rollbackunit {
constexpr uint32_t kManagerContext = 0xE8; // GetManager(unit + this)
} // namespace rollbackunit

namespace manager {
constexpr uint32_t kRegionsBegin = 0x10;     // {void* ptr; uint64 size}[]
constexpr uint32_t kRegionsEnd = 0x18;
constexpr uint32_t kPostRestoreList = 0x38;  // std::list sentinel pointer
// Post-restore list node: fn(ctx, const FrameId*) when token != 0.
constexpr uint32_t kNodeToken = 0x18;
constexpr uint32_t kNodeContext = 0x28;
constexpr uint32_t kNodeFunction = 0x30;
// Regions of the game's snapshot that drift on replay without the fight
// diverging: written from per-rendered-frame state (camera and presentation),
// so they differ whenever a replay runs a different number of rendered
// frames. Still saved and restored; left out of the hash. Learned with the
// in-game synctest (-sfve_learn=1, 2026-11-04): during fighting only these
// differed, in about 70% of 7-frame rollbacks, while the fighters' regions
// never did. Identified by size, each unique in the table (one 112, two 300,
// one 528 bytes); Bind checks the counts.
struct PresentationSize {
	uint64_t size;
	int count;
};
constexpr PresentationSize kPresentationSizes[] = {{112, 1}, {300, 2}, {528, 1}};
} // namespace manager

} // namespace sfve::mod::sfv
