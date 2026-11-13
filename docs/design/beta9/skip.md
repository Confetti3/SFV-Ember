# beta.9: the scene skip is a press and nothing else (2026-11-13)

The beta.8 notes are `docs/design/beta8/skip.md` (how a skip ends each scene) and
`docs/design/beta8/autoskip.md` (the room rule this change removes).

## 1. The skip fires only on a press

**Owner's decision (2026-11-13).** The skip works when a player presses Start or an attack button
and never fires by itself. The KO and results screens stay as they are.

**What changed.**

- The Skip winner scenes rule is gone on the SFV side. `SfvCapabilities` sets
  `caps.skipScenes = false`, and the library builds the "Skip winner scenes" row only when a game
  offers it (`ShellHost.cxx:75` sets `offersSkipScenes` from `caps.skipScenes`, and
  `RoomPanel.cxx:719` adds the row only when it is set). `SetSkipScenes` is only reachable from
  that row (`RoomActions.cxx:347-351`), so no SFV room can change the rule.
- Public rooms still carry `skipScenes = true` in the snapshot (the library default). Nothing
  reads it: `RoomMatchNow`, `RoomMatch`, `MatchPicks`, `TakeLinkedBattle`, `RoomBattle` and
  `NetplayMode` lost the flag, and `intro_skip::Request` lost `SetAutoScenes`. No fighter copy can
  ask on its own whatever the snapshot says.
- What stays: `Request::Next` asks from a new press (Start's bit 0x4000 or any of the six
  attacks) made at least `kScenesGuardFrames` (60) frames after this copy saw the scenes start,
  either player's press skips for both, the skip bit is read one session frame in 15
  (`kEvery`).
- The pipe's `room` command lost its "skip winner scenes" argument
  (`room [rounds] [seconds] [instant] [first to] [tiny] [banned stages]`), so `runreplay.py`
  (`room 1 30 {instant} 0`) and every other tool open rooms that cannot skip by themselves.
  `runroom.ps1 -autoSkip` is removed. `-skipAfter`, `-skipCopy`, `-skipPad`, `-pressStart` are
  the scripted presses. A run with none of them must apply no skip (`CheckScenes`/`CheckRounds`
  fail on one).

**Old replays still play.** A replay (and a watcher) does not run `Request::Next`: a playback goes
`NetplayMode::Step` -> `session_->Watching()` -> `WatchFrames` (`NetplayMode.cxx:160-163`), and
`WatchFrame` steps `session_->Step(0)` (`NetplayMode.cxx:357`), never reaching the request at
`NetplayMode.cxx:206-207`. The skip frames' bits come from the recorded confirmed inputs:
`GivenInputs` keeps the skip bit on the skip frames of a flow state that is a scene
(`GameAdapter.hxx:99-106`, `SfvAdapter.hxx:116-119`), and `SfvAdapter::Advance` ends the scene on
a given bit alone (`SfvAdapter.cxx:537`). Neither consults a rule. A replay recorded under beta.8's
auto-skip therefore plays the skips it recorded, on the frames it recorded them.
(The match version in the room build id still refuses beta.7 replays; that is unchanged.)

**Tests.** `IntroSkipTests`: the four auto-skip cases are gone; "with no press the request is
never set, however long the decided match's scenes run" (5000 frames, every scene state, a held
direction, the round scenes, the intro) and a press-only case for the scenes between rounds were
added. `SfvCatalogTests`: the launch-carries-the-rule case is gone. `EmberAdapterTests`:
`caps.skipScenes` is false.

## 2. Proving the skip on rounds that end in a KO

**The gap.** No run has skipped a round that ended in a KO: all 56 saved round results are
time-overs (30-second rounds, seeded mashing). Time-overs have no KO presentation, so what a cut
`Finish` leaves behind in a KO is unproven.

**What the code does, for any ending** (read from `IntroSkip.hxx`, `SfvAdapter.cxx:372-437`,
`beta8/skip.md`). A press at least 60 frames after the scenes started sets the request. The next
confirmed skip frame (one in 15, both copies the same) runs `EndScenes` before the frame's
update. `ScenesStepFor(Finish, any phase)` is `RequestNext`: the flow is asked for RoundOver
(`FlowRequest`, `SfvAdapter.cxx:400-408`) whatever phase Finish is in, so Finish is cut about
60-75 frames after the KO. The flow's own switch then runs (`FlowUnit::PreTick`: "the old
state's Exit, the pointer moved, the new state's Enter", `beta8/skip.md`). RoundOver asks for the
round's demo on its first tick; the demos read the confirmed bit themselves; the next round's
call is never cut.

**What is unknown.** `beta8/skip.md` names rva `0x40BC00` twice in a way that does not agree: as
Finish's phase 12 ("waits up to 1200 ticks for the KO's presentation, rva `0x2FB5D0`") and, in the
closing checklist (item 8), as an "exit-only step that sets each fighter's +2376 to -1 before it
leaves". Both cannot be right, and which of them runs on a cut decides the risk:

- if the +2376 reset is in Finish's `Exit` (a vtable method), the switch runs it on a cut as on a
  normal leave, and nothing is skipped over;
- if it is the body of a phase (12 or a later one), a cut before that phase leaves each fighter's
  +2376 at what the fight set, and the next round starts with it.

The KO slow-motion flag (battle frame +0x190 bit 0x20) has the same two readings (set by the KO's
hit event, cleared by a phase or by `Exit`). If it stayed set, `BattleFrameUpdate` runs the ticks
only while the frame is not paused (+0x190 clear or +0x194 set, `beta8/skip.md`), and the flow, a
phase-2 task, would not tick: the next round would not start. The skip would then freeze the
battle on both copies alike (a hang, not a desync). The existing check `NotePause` logs only bits
0x7, so it would not show 0x20.

None of this can be settled from what is in the repository, and the IDA database was not
reachable from this PC (no session open; `ida-pro-mcp` refused the connection). It needs IDA on
the laptop (Finish's constructor `0x41C7A0`, vtable `0x2C6A668`: its Exit slot, and what `0x40BC00`
is) or a game run (below).

### Per ending

| ending (finish name) | what the code does on a press | what is at risk |
|---|---|---|
| KO by a normal (`ko`) | Finish is cut 60-75 frames in, during the KO presentation; RoundOver; the round's demo (RoundResultDemo 9) ends on the bit; next round | the two unknowns above: +2376 and the KO bit 0x20 |
| KO by Critical Art (`critical art`) | the same. Whether the flow is already in Finish while the finishing cinematic plays is not known; if Finish starts at the hit, a cut at 60 frames lands inside the cinematic | the above, plus a cinematic cut short (camera, hidden fighters, the matinee's own state) |
| KO by a projectile (`ko`) | the same | the above; projectile units still live at the cut |
| Perfect, chip, EX finish (`perfect`, `chip`, `ex`) | the same; only the finish icon the HUD draws differs | the above. The icon may be cut; nothing in the rollback state depends on it |
| double KO, round not deciding (`double ko`) | Finish cut; RoundOver asks for RoundDoubleKODemo (10), which ends on the bit itself (`ScenesStepFor` leaves it to the game) | the above |
| double KO or draw deciding the match | Finish cut; RoundOver asks for MatchDrawDemo (12); the skip moves phase 2 to 3 and phase 4 to 5 (`ScenesStepFor`); phases 1 (120 ticks) and 6-7 play out; the results' pages then hear the request through `PadsHear` | the above; a draw's demo has never been run skipped |
| time-over, one side ahead (`time over`) | proven: 56 saved results, 0 differing, the next round starts | none known |
| time-over draw (`time over`, no winner) | as the double KO (demo 10 between rounds, 12 at the end) | not run; no KO presentation, so the risk is the demo's only |
| V-Trigger or EX at the finish | the same as the KO it ends in; the V-Trigger's timer and gauge are rollback state the scene moves do not touch | `desync.md`: Z33's and Z29's items read model poses; a cut during them is not run |

### Making it safe without a run: the Finish gate

The only guard the code can have today without guessing at the game is not to cut Finish while the
KO's slow-motion bit is set, so a KO's presentation plays until the game itself clears the bit.
`intro_skip::ScenesStepFor` takes a `koStop` argument (default false: the step as it was) and
returns no step for Finish while it is true; `intro_skip::kKoStopBit` is 0x20. It does nothing
until `SfvAdapter::EndScenes` passes the bit (patch below). If the game keeps the bit through all
of Finish, the gate never opens and a KO's scenes just play out, which is safe and shows in the
logs; if it clears the bit when the presentation ends, the skip waits for exactly that. It does
not settle the +2376 question; only IDA or a run can.

**Patch for `src/mod/SfvAdapter.cxx` (not made here, the file is not this change's).** In
`SfvAdapter::EndScenes`, replace

```cpp
const intro_skip::ScenesStep step = intro_skip::ScenesStepFor(id, vtable, phase);
```

with

```cpp
// The KO's slow motion (+0x190 bit 0x20) holds Finish: a KO is cut once the game cleared it.
const bool koStop =
	battleFrame_ && (Field<uint32_t>(battleFrame_, sfv::battleframe::kState) & intro_skip::kKoStopBit) != 0;
const intro_skip::ScenesStep step = intro_skip::ScenesStepFor(id, vtable, phase, koStop);
```

and add the word to the two log lines so the run shows it: in the "moves no phase here" log
(`SfvAdapter.cxx:388-393`) and in the `RequestNext` case's `snprintf` (`:403`) append
`, battle frame state word %08x` with the same field. `NotePause` (`:556-566`) masks 0x7;
widening the mask to 0x37 (`sfv::pause::kBits | 0x30`) logs a change of the KO bit and bit 0x10
with the flow state, which shows when the game sets and clears them. (A second patch, running
Finish's exit step explicitly after the cut, is not proposed: its address and effect are the
unknown above.)

### Tool support

`runroom.ps1` and a new pipe command can now produce and check these rounds:

- **`vital <P1 percent> <P2 percent>`** (pipe, `NetplayMode`; runroom `-vital "P1,P2"`): at the
  first frame of every round's fight each fighter's vital (CharaStatusPart +0xC0, 16.16) is set to
  that share of its maximum (+0xC4); 0 or 100 leaves a side. `-vital 100,5` with P2 idle: the
  next hit KOs. `-vital 5,5` with both fighters hitting: a trade is a double KO. It is applied on
  fighters' and watchers' copies alike. It is not rollback-proof (the write is made before the
  frame's step, and whether the session's save for that frame already holds it is not
  established), so it needs `-lag 0` (runroom refuses otherwise) and no `-practice` (whose gauges
  refill the vital). It is for the run's own use; no player option reads it.
- **Round-start check, on every run**: each copy logs `netplay: a round's fight starts at frame F;
  battle frame state word W (pause bits P, KO bit K), slow motion S` at each round's first fight
  frame. runroom fails the run if any round starts with a pause bit, the KO bit or slow motion
  set, on different frames on the two copies, or if the copies count different rounds. This is the
  "next round starts clean" check. +2376 is not read: the mod has no accessor and the object that
  holds it is not named in the repository; once IDA names it, a `peek` of it after each round
  start settles it.
- **`-expectFinish NAME`**: reads the finish names the game recorded in each match result
  (`netplay: match result {...}`) and fails if no round ended so.
- **No-press full length**: a run with no `-skipAfter`/`-skipPad` already failed on any applied
  skip; it now also requires each decided match's scenes to take at least `-fullMin` frames
  (default 1000; 1545 and 1865 seen after a time-over).
- Already there and usable: `-idle 0|1`, `-moveset targets|supers|projectiles` (a normal's chain,
  a Critical Art, a projectile), `-rounds`, `-seconds` (a time-over draw: `-mashFrom 99999` presses
  nothing, so both sides finish with equal vital), `-practice` (gauges so Critical Arts and
  V-Triggers can be played, but its vital refill fights `-vital`: tune the percent), `peek` and
  `perturb`.

## 3. Scripted moves the harness could not play (target combos and V-Skill)

`kVSkillPunches` and the other sets press at 20-frame gaps, so no set could play a target combo.
Three frame-timed sets are added to `input::MoveSet` (`src/mod/GameInput.*`): each is a list of
`Step`s counted in frames, a function of the seed like the others, grounded (down at most), facing
from the fighter.

| set | blocks (press to press, a press is 4 frames) |
|---|---|
| `luke` (Z42) | LP, MP at +10, HP at +31 (the field's gaps); LP, MP at +8, HP at +18 (tight); each alone and followed by QCB+LP 30 frames after the HP (the field replay's follow-up); QCB+LP alone |
| `akira` (Z41) | V-Skill (MP+MK) taps at gaps 6/9 and 21/38; a 13-frame hold; a hold then a tap, LP, MP, HP, LK, MK or HK 8 frames after it; a tap then LP, HP |
| `targets` (any fighter) | LP>MP (gaps 6, 12), MP>HP (8, 11), LK>MK (9, 6), LP>MP>HP (6,10 and 12,8), MK>HK (7, 12), crouching LP>MP and LK>MK (gap 8) |

Akira's V-Skill I or II is the pick's loadout (`Z41:c:col:1:x` or `:2:`), not the script's.

**Not done, and why.** Luke's other target combos and Akira's partner-calling moves: nothing in
this repository (docs, data, pick space) holds their inputs, and the brief says not to invent
them. The follow-up presses after Akira's V-Skill are a probe (every attack button once, 8
frames after the hold), not a known launcher input. When the inputs are known they are one
`Step` list each in `GameInput.cxx`.

**Wiring.** `tools/runroom.ps1` and `tools/synctest.ps1` pass `-moveset luke|akira|targets` as
`-sfve_moveset=` and now refuse an unknown name (the mod reads an unknown name as the system
set and plays single buttons, silently). The mod's own option parser, `src/mod/ModConfig.cxx`,
is not in this change's scope: it needs three lines (see the report's patch), without which the
mod ignores the new names. Tests: `GameInputTests` runs the new sets through every generic
case (blocks follow each other, grounded, facing) and checks the Luke, Akira and chain timings
frame by frame.
