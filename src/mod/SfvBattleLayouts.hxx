#pragma once

// The layouts of the game objects in a battle and its rollback, in
// StreetFighterV.exe (Steam build 13173604; SfvOffsets.hxx, which includes
// this, has the build and its code sites): the random number generators,
// a Practice room's gauges, the battle flow, frame and units, the battle
// data, and the rollback manager. Offsets in bytes; addresses are RVAs.

#include <stdint.h>

namespace sfve::mod::sfv {

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

// A Practice room's game (EMBER_LIBRARY.md section 27): the gauges Training
// mode refills, set for both players before the session's first frame, as
// Training's own menu sets them (rva 0x4A86F0).
//   - Each player's parameter table in the battle data (GetBattleData) at
//     kPlayerParams + kPlayerParamsStride * side, and a copy in the fighter's
//     CharaUnit at kCharaParams; the fighter's side is the CharaUnit's int at
//     kCharaSide (rva 0x3512A0 reads the battle data's table by it).
//   - Vital (22): 6-10 are INFINITY_MIN..INFINITY_MAX (EKWGaugeVital). Damage
//     (rva 0x361670) never marks the fighter down with them (the KO's flag,
//     CharaStatusPart's dword 192) and stops the vital at 0; each tick (rva
//     0x3635C0), once the fighter has been left alone for 60% of the
//     recovery timer (its dword 50 against dword_143966360), vital and
//     recoverable vital grow back by 1% of the maximum a frame, up to the
//     cap (10: the whole gauge). 11 holds them at 0, 12 at full.
//   - EX (24, EKWGaugeExtra): 3 INFINITY_MAX refills by 1% of the gauge a
//     frame once its timer (dword 57) is full (rva 0x362AC0); 4 holds 0, 5
//     full. At a round's start (rva 0x357540) 2, 3 and 5 start it full.
//   - V-gauge (26): 4 refills to half, 5 to full, the same way (rva
//     0x3632E0); 6 holds 0, 7 full.
//   - Stun (27) is left as in a match, as SF4 Ember's Practice leaves stun.
// Training's menu maps its rows to the same values (rva 0x4A86F0: vital
// {0, 12, 10, 9, 8, 4, 3}, EX {0, 2, 5, 3}, V {0, 3, 7, 5}). The tables are
// outside the game's snapshot (the battle data's snapshotted part is +6176..
// +6404, the CharaUnit's +1892..+2191) and nothing writes them during a
// battle but Training's menu; the gauges they drive are in it.
namespace practice {
constexpr uint32_t kPlayerParams = 560;
constexpr uint32_t kPlayerParamsStride = 1424;
constexpr uint32_t kCharaParams = 864;
constexpr uint32_t kCharaSide = 1880;
constexpr int kVitalParam = 22, kVitalRefill = 10;
constexpr int kExtraParam = 24, kExtraRefill = 3;
constexpr int kVGaugeParam = 26, kVGaugeRefill = 5;
} // namespace practice

namespace flow {
constexpr uint32_t kStateMessageType = 0x3A651C0; // data: what a state-change message's type call returns
constexpr uint32_t kStateIndex = 12;              // int in the message
constexpr int kFight = 6;                          // players are fighting
} // namespace flow

// The battle object (0x1D0 bytes, constructor rva 0x4C5C40, reset 0x4D8830).
// It is allocated by FMemory::Malloc directly (rva 0x4F4788 calls 0x11370F0),
// not through GameAlloc, so the heap's leftovers stay in what it leaves unset.
// One per battle (kBattleFrameCtor); the next battle's may reuse its block.
namespace battleframe {
constexpr uint32_t kState = 0x190;      // pause, frame counters, slow motion, extra ticks, tick counter, clock
// Through +0x1BB, the tick counter. +0x1BC..+0x1BF is alignment padding before
// the TArray at +0x1C0: the constructor and the reset write +0x1B8 as a dword
// and +0x1C0 as a qword, nothing reads or writes the gap, and it holds what
// the heap left there (1.0f in one copy after pad presses at the main menu).
constexpr uint32_t kStateSize = 0x2C;
constexpr uint32_t kMinTicks = 0x1A4;   // ticks per frame = 1 + max(this, extra)
constexpr uint32_t kExtraTicks = 0x1A8; // the game's own rollback replays through this
constexpr uint32_t kTickCount = 0x1B8;  // zeroed by each update (rva 0x4DC2D0), bumped once per battle tick
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
// In the counters: the frame tag (set to 7 at init) and the battle's input
// frame I, which the per-tick task rva 0x43A840 increments after the
// rollback unit's tasks; the battle reads the input recorded one tick
// earlier (frame I = R - 1, rva 0x4440D0). A save between two updates holds
// the state the game's own snapshot calls {tag, I - 1} (RollbackUnitPre
// builds its frame id from these two, rva 0x486CB3, before I advances).
constexpr uint32_t kTag = 0x217C;
constexpr uint32_t kInputFrame = 0x2180;
// The same rva 0x43A840 mirrors I into the game instance (kGameInstance) at
// this offset; round-start thresholds read it (rva 0x402E10, 0x40BA40).
constexpr uint32_t kInstanceInputFrame = 0x150;
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

// Fighter state the game's snapshot leaves out, by the part that holds it:
// each fighter's part of that type, if it has one, gets the run saved,
// restored and hashed as state outside the snapshot (SfvLayoutRules).
//
// CharaStatusPart registers +0x32C, +0x330 and +0x334 (rva 0x354260) and
// stops there. +0x338 is set to 1 by a move script's command (rva 0x328D60,
// in the command table rva 0x317580) and cleared with +0x334 by the status
// reset (rva 0x362850); the fight reads it in "is the V-Trigger running"
// (rva 0x372940) and in Z39's per-frame V-Trigger update (rva 0x3C0FF0),
// which sets or clears bit 0 of +0x110, the hold that stops the V-Trigger
// timer draining (rva 0x3632E0, 0x361590). A rollback to before the command
// kept its 1, so the replayed frames held the timer where the played ones
// had not (2026-11-09 sweep: Z38 against Z39, CharaStatusPart+0xC0's +0x50,
// that is +0x110, 0 played and 1 replayed, three times). +0x33C is set by
// rva 0x34F330, cleared by rva 0x351760 and gates the fighter's pending
// status effects (rva 0x344F40).
//
// Z39's CharaUniquePart (type 0x3A682D0) registers +0xF0..+0x464 only. Its
// tick (rva 0x3BF4B0, then 0x3C1300) counts +0x61C down each frame, a delay
// its moves set (rva 0x3AD7E0, 0x3A8670, 0x3C73F0); rva 0x3C1990 applies a
// mode change (+0x444 into +0x448, and to a state object in the snapshot
// through rva 0x43D440) only once it is 0. A rollback kept the later, lower
// count, so a replay applied the change sooner (2026-11-09 sweep: Z39
// against Z40, the part's +0x448 0 played and 4 replayed, and that object's
// 8-byte run 6 and 0x2B, one replay).
struct PartRun {
	uint32_t partType; // the part's type id (GameFindPart)
	uint32_t offset;
	uint32_t size;
	const char* name; // the region's name after "CharaUnit[i]."
};
constexpr PartRun kPartRunsOutsideSnapshot[] = {
	{fighter::kStatusPartType, 0x338, 8, "CharaStatusPart.holds"},
	{0x3A682D0, 0x61C, 4, "CharaUniquePart@0x3A682D0.delay"},
};

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
// with the in-game synctest, -sfve_learn=1): the camera (CameraUnit+0x100,
// 528 bytes: eye, target, field of view, read by nothing in the fight; the
// screen edge comes from the fighters' centre below, not the camera), and
// its later run at +0x480 (128 bytes: a 4x4 matrix at +0x480 and the
// floats after it to +0x500, registered by the camera's snapshot at rva
// 0x32EBD0): two PCs keep different values there while every fight run
// matches (EMBER_LIBRARY.md section 47: four desyncs between two players'
// PCs, each this run alone, the 521 others equal, through whole fights
// once it is left out), so it is the camera's presentation as above. Its
// other runs (+0x318, +0x340, +0x3A0, +0x3D0, +0x450, +0x550, or +0x170 in its other layout) are the same
// camera's: a V-Trigger's or Critical Art's camera moves them, and a
// replayed stretch leaves them a few ulps apart while every fight run
// matches (2026-11-09: the synctest with system moves, 1,162 rollbacks, only
// +0x450 differing, in four replays, the first 37 frames after a V-Trigger;
// a players' match ended "diverged" at the checkpoint 13 frames after one),
// so the whole camera is restored and left out of the hash; a
// projectile's visual part (ProjectileVisualPart+0xC0 and +0xF0: its UE
// actor, reconciled after a restore by rva 0x482D10, so a replay can differ
// in the actor pointer and a frame counter), and a fighter's material part
// (CharaMaterialPart+0xD8 and +0x3B0: shader and material instances
// recreated by its post-restore callback rva 0x3587D0), and its visual part
// (CharaVisualPart+0x6D8, 392 bytes, registered at rva 0x395320): +0x6D8
// drives the fighter's UE actor (rva 0x3A06C0: 1 start, 3 and 4 stop, 5
// re-sync, each then 0 or 2), and its post-restore callback (rva 0x3999B0)
// turns a playing 2 into 5, so a restored fighter re-syncs its actor where
// one that played straight on keeps 2; the floats after it drift on replay
// too (2026-11-09 sweep: one fighter's V-Trigger II, 313 replays differing
// there alone).
//
// Bone positions inside fight state, masked: a fighter's bone 28 in world
// space (CharaUnit+1984, read in Chara PreInput rva 0x37D250 from its visual
// part, the UE pose) and the point the fighters' centre task derives from
// it (CharaManagerUnit+464). After a restore the fighter's post-restore
// callback (rva 0x37B670) sets a flag that skips the bone read until a
// visual pass clears it, so replayed ticks keep the restored bone; only the
// camera reads these. Everything around them is fight state and hashed: the
// centre at CharaManagerUnit+448 (screen-edge pushback, push order, facing:
// rva 0x34C680, 0x34BCD0, 0x323830) and a fighter's positions, offsets and
// position relative to the centre in CharaUnit+0x764 (pushback, landing,
// box transforms: rva 0x34C680, 0x388660, 0x3826D0). Both masks are whole
// 8-byte words of their run: CharaUnit+0x764's [88, 112) also covers
// +1980..+1983 and +2000..+2003 (traced in IDA, 2026-11-04).
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
// A masked range: each fighter's CharaStatusPart registers part+0x238..+0x303
// as one run (rva 0x354260); its 8 bytes at +0x88 are part+0x2C0, the
// combo's copied move count, and +0x2C4, the opponent's move starts in the
// current combo (incremented when the other fighter enters a state, rva
// 0x356290, and reset by this fighter's own, rva 0x35DD20). When both
// fighters start a move on the same frame the result depends on which writes
// last, and the two copies (or a replay) can differ; only the Combo event's
// statistics listener reads it (rva 0x4B27D0).
//
// Masked in every slot: a projectile group's run at +0x500 (1408 bytes,
// registered at rva 0x480670) is 8 spawn requests of 176 bytes (walked at
// rva 0x47FDA0, 0x47C4A0 and 0x482480: +160 taken, +164 delay, +168 frame).
// A projectile that spawns another fills one from a stack request (rva
// 0x469390) that its constructor (rva 0x44C730) sets only through +144, and
// rva 0x48D000 copies all 160 bytes, so +148..+159 of the slot are whatever
// that stack held: a counter, a heap or image pointer, or zero, different on
// a replay and between two PCs (2026-11-09 sweep: three pairs, slot 0's
// +0x94..+0x9B alone, after Critical Arts and V-Triggers). No writer sets
// them (the slot resets rva 0x4709E0, 0x482850, 0x4781F0 and the direct
// fills rva 0x37F550, 0x453270 stop at +144) and nothing reads them: the
// spawn (rva 0x470FB0) reads up to +140. The word at +144 is masked with
// them; it holds the -1 every request starts with.
struct RunRule {
	const char* owner; // RegionNames' owner type
	uint32_t offset;   // the run's start in its owner, or kAnyRun: every run it has
	const char* kind;  // non-empty: not hashed at all; the region name's suffix
	// Otherwise the run-relative bytes [maskFrom, maskTo) are not hashed
	// (8-byte words; maskTo past the run's end means to its end), and again
	// every maskEvery bytes after them to the run's end when it is not 0.
	uint32_t maskFrom = 0;
	uint32_t maskTo = 0;
	uint32_t maskEvery = 0;
};
constexpr uint32_t kToTheEnd = ~0u;
constexpr uint32_t kAnyRun = ~0u;
constexpr RunRule kRunRules[] = {
	// The whole camera: every run its snapshot (rva 0x32EBD0) registers, in
	// either of its layouts (SFV_REGION_MAP.md: one 528-byte run at +0x100,
	// or 112 + 376 bytes).
	{"CameraUnit", kAnyRun, ".presentation"},
	{"CharaManagerUnit", 0x1C0, "", 16, 32},
	{"CharaMaterialPart", 0xD8, ".presentation"},
	{"CharaMaterialPart", 0x3B0, ".presentation"},
	{"CharaVisualPart", 0x6D8, ".presentation"},
	{"ProjectileVisualPart", 0xC0, ".presentation"},
	{"ProjectileVisualPart", 0xF0, ".presentation"},
	{"StatsUnit", 0xF8, ".stats"},
	{"ReplayUnit", 0xF8, ".replaycount"},
	{"CharaUnit", 0x764, "", 88, 112},
	{"CharaStatusPart", 0x238, "", 0x88, 0x90},
	{"ProjectileGroupUnit", 0x500, "", 0x90, 0xA0, 176},
};
} // namespace manager

} // namespace sfve::mod::sfv
