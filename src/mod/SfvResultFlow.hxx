#pragma once

// The versus battle's flow after its deciding round: the states between the
// last KO and SFV's results call (sfv::kShowResult), in StreetFighterV.exe
// (Steam build 13173604; SfvOffsets.hxx has the build and the code sites the
// skip calls). Read from our IDA database, 2026-11-11; the findings, with
// every rva, are in docs/design/beta8/skip.md.
//
// The versus flow (built by rva 0x471AB0, which the versus battle's setup,
// rva 0x461090, picks unless rva 0x2FC230 says otherwise) holds 19 states:
// BattleStart 1 (to 3), IntroDemo 3 (to 2), MatchStart 2 (to 4), RoundStart
// 4 (to 5), Ready 5 (to 6), Fight 6 (to 7), Finish 7 (to 8), RoundOver 8,
// RoundResultDemo 9, RoundDoubleKODemo 10, MatchResultDemo 11, MatchDrawDemo
// 12, MatchOver 13, MatchEnd 14, RoundJump 17, BattleRestart 18 (to 2),
// BattleStartReplay 19 (to 1), BattleOver 15 (to 16) and BattleEnd 16. No
// Skip (22): the pause menu's Skip (rva 0x461850) asks the flow for a state
// this flow does not have, and the flow's request (rva 0x3E40C0) refuses it.
//
// After the deciding KO the flow plays Finish, RoundOver, then
// MatchResultDemo (a winner) or MatchDrawDemo (none), MatchOver and MatchEnd,
// whose state change the versus battle answers with its results call. Of
// these only MatchResultDemo's phase 3 (rva 0x409930) asks for the skip bit,
// and only when its own gate (kResultSkipGate, set by rva 0x3F0020) is open:
// the battle data's kind (kBattleKind) is 1, and neither a team battle (rva
// 0x2FC230) nor an online kind (rva 0x2FC2A0) is playing. Finish waits on the
// KO's presentation (rva 0x40BC00 and 0x40DA20, each up to 1200 ticks) and
// on fixed delays; MatchDrawDemo waits on its demo (rva 0x409870, up to 600
// ticks) and 300 ticks more. None of them reads a pad.

#include <stdint.h>

namespace sfve::mod::sfv::resultflow {

// State ids: the index of a state's name in the flow's state table (rva
// 0x39665E0, filled at rva 0x142840), which every state keeps at kStateId
// (its constructor's second argument) and a state change announces
// (flow::kStateIndex).
constexpr int kFinish = 7;
constexpr int kRoundOver = 8;
constexpr int kMatchResultDemo = 11;
constexpr int kMatchDrawDemo = 12;
constexpr int kMatchOver = 13;
constexpr int kMatchEnd = 14;

// The classes of the versus flow's states the skip ends (vtable rvas): each
// constructor writes its vtable after the base's (rva 0x2C6A740).
// Finish (constructor rva 0x41C7A0, 168 bytes; phases bound at rva 0x3EEE00).
constexpr uint32_t kFinishVtable = 0x2C6A668;
// MatchResultDemo (constructor rva 0x41D160, 184 bytes; phases bound at rva
// 0x3EF3B0): 0 rva 0x40DBC0 (to 2 when the battle's unit of type rva
// 0x3A649B8 is in its state 6 or 7, rva 0x3D0D10; else to 1), 1 rva
// 0x409100 after 90 ticks and 2 the same function at
// once (sets the gate and goes to 3), 3 rva 0x409930 after 5 ticks (the skip
// bit, behind the gate: to 4 marked skipped; else to 4 once the winner's demo
// has played, rva 0x2FB500, or after 600 ticks), 4 rva 0x3DAFC0 (to 5), 5 rva
// 0x40E6A0 (sends the battle's "StartResult" event; to 6), 6 rva 0x40F2E0
// (waits for the results' unit, rva 0x47FD80; to 7), 7 rva 0x3DADB0 (asks the
// flow for MatchOver).
constexpr uint32_t kMatchResultDemoVtable = 0x2C6A878;
// MatchDrawDemo (constructor rva 0x41D020, 144 bytes; phases bound at rva
// 0x3EF2A0, its start goes to 1, rva 0x40DDC0): 1 rva 0x409000 after 120
// ticks (to 2), 2 rva 0x409870 after 5 (waits for the demo, rva 0x2FB500, or
// 600 ticks; to 3), 3 rva 0x409E50 (to 4), 4 rva 0x3DAFC0 after 300 ticks
// (to 5), 5 rva 0x40E460 (sends "StartResult"; to 6), 6 and 7 as above.
constexpr uint32_t kMatchDrawDemoVtable = 0x2C6A8E0;

// A flow state's fields (beyond flowstate's base).
constexpr uint32_t kStateId = 32; // int
// Finish's next state, the flags and the argument its last phase (rva
// 0x40EBE0) hands the flow's request: +144 (kRoundOver, from the
// constructor), +148 (0) and the state's own argument (flowstate::kArg).
constexpr uint32_t kNextState = 144;
constexpr uint32_t kNextFlags = 148;
// MatchResultDemo's own fields: +176 its skip gate (rva 0x409100 writes rva
// 0x3F0020's answer), +180 skipped (rva 0x409930 sets it with the skip; rva
// 0x40E6A0 then keeps the battle data's int at +6196, which it clears
// otherwise).
constexpr uint32_t kResultSkipGate = 176;
constexpr uint32_t kResultSkipped = 180;
// The phases the skip moves MatchResultDemo and MatchDrawDemo to.
constexpr int kResultSetupWaitPhase = 1; // the setup after 90 ticks...
constexpr int kResultSetupPhase = 2;     // ...and the same setup at once
constexpr int kResultSkipPhase = 3;      // reads the skip bit behind the gate
constexpr int kResultSkippedPhase = 4;   // where the game's own skip goes
constexpr int kDrawDemoPhase = 2;        // waits for the draw's demo
constexpr int kDrawAfterDemoPhase = 3;
constexpr int kDrawWaitPhase = 4;        // 300 ticks before the results
constexpr int kDrawResultPhase = 5;

// The battle data's kind (int at +0; 3 in its defaults, rva 0x2EDCB0). Read
// for the log: the win demo's own gate (rva 0x3F0020) opens only for 1.
constexpr uint32_t kBattleKind = 0;
constexpr int kKindSkippable = 1;

// Between two rounds (a KO or time-over that does not decide the match): the
// flow plays Finish (7), RoundOver (8), then RoundResultDemo (9, a winner) or
// RoundDoubleKODemo (10, none), which ask the flow for RoundStart (4, rva
// 0x40A9F0); RoundStart and Ready (5) are the next round's call ("ROUND 2",
// "FIGHT"), which the skip never cuts. RoundOver takes the round demos only
// while the match data's kDecided is 0 (rva 0x40AA70).
constexpr int kRoundResultDemo = 9;
constexpr int kRoundDoubleKODemo = 10;
constexpr int kRoundStart = 4;
// RoundResultDemo (constructor rva 0x41DBB0, phases bound at rva 0x3EFD50)
// and RoundDoubleKODemo (constructor rva 0x41D930, rva 0x3EFBD0): phase 0
// (rva 0x409720 / 0x4096F0) marks phase 3 as the state's skip target (rva
// 0x3EC380, with the match data's kIntroSkippable); phase 1 (rva 0x409C60:
// up to 300 ticks or the round demo's end, rva 0x2FB570; the double KO's rva
// 0x409BA0: 120 ticks) asks the battle's input for the skip bit (rva 0x2F2D40: before the
// decision that is the input the battle reads, the session's) and on it goes
// to the marked phase (vtable +56, rva 0x445040); 3 (rva 0x40B720) and 4
// (15 ticks) lead to 5, which asks for RoundStart. So these two end on the
// confirmed skip bit by themselves.
constexpr uint32_t kRoundResultDemoVtable = 0x2C6A7A8;
constexpr uint32_t kRoundDoubleKODemoVtable = 0x2C6A810;

} // namespace sfve::mod::sfv::resultflow

// SFV's own pause in a battle (docs/design/beta8/skip.md). The PauseUnit's
// PreUpdate task (rva 0x46E110, registered by rva 0x46B8F0 as "PauseUnit",
// scheduler phase 1, so it runs in every battle update, ticking or not)
// reads each local pad's Start from the pad manager itself (rva 0x4CF800
// and 0x4CEF40, bit 0x4000 there), never from the session's inputs, while
// the match data's +6352 has bit 4 (the flow allows a pause) and the battle
// frame is not stopped by a KO (+0x190 bit 0x20). It then sends the battle a
// PauseControlEvent (vtable rva 0x2C6DF18, type at rva 0x3A652B8), whose
// receivers (rva 0x4639B0, 0x463E40) set the battle frame's pause bits
// (rva 0x4D5DE0: +0x190 |= 2 << pad, and 1): that copy's battle stops ticking
// and opens the pause menu, the other's goes on, and the battle frame's
// state is in the rollback layout, so the next checkpoint differs. The same
// task also sends a pause asked for by the game's menus (+248..+256, rva
// 0x4709C0). The mod keeps the task from running in a linked battle.
namespace sfve::mod::sfv::pause {
// The battle frame's pause bits (battleframe::kState): the pause menu's (1)
// and each player's (2 << pad, pads 0 and 1).
constexpr uint32_t kBits = 0x7;

} // namespace sfve::mod::sfv::pause
