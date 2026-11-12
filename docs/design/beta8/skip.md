# beta.8: skipping the scenes after the deciding KO (2026-11-11)

**What players saw.** After the last KO of a room match both players wait about 26 s (some 1545
frames) before Ember's rematch prompt opens over SFV's results. Pressing a button there does
nothing. A synced skip has been in the code since section 52 (`mod/IntroSkip.hxx`, `h_PadsAny`)
but has never ended a scene.

**Decided by the owner.** Either player's press skips the scenes for both. The prompt stays on
SFV's results screen and never opens earlier. The skip must be rollback-safe and play the same on
both copies, for watchers and in replays.

## Evidence

### The owner's 2026-11-11 log (beta.7, the maintainer as P1)

`D:\sfv-re\b8-desync\sfv_ember-owner-20261111.log` holds 20 decided matches. Every one has a run
of `session: frame N gives the game confirmed bits ...` lines every 15 frames from the first
press after the decision to the result. Examples:

- 8115..9525: P2 asked from 8115, P1 from 8595. Result at frame 9528.
- 7620..9330: P1 (the local player) asked first. Result at 9333.

So the skip bit was sent, confirmed and given to the game on about 95 skip frames per match, by
both players, and nothing was skipped. The local player's presses reached the session, so the
pad read worked after the decision. The bits were given, so the announced flow state was not
Fight.

### IDA (laptop database, build 13173604)

**Which flow.** The versus battle's setup (rva `0x461090`) builds the flow with rva `0x471AB0`
unless rva `0x2FC230` is set. In that case it uses rva `0x472F80`, which adds states 38 and 39.
`0x471AB0` makes 19 states, which matches the log's room battles ("flow: bound ... with 19
states"). The state ids index the name table at rva `0x39665E0`, filled at rva `0x142840`:

| id | name | constructor | class (vtable rva) | next |
|---|---|---|---|---|
| 1 | BattleStart | `0x41C4F0` | `0x2C6A390` | 3 |
| 3 | IntroDemo | `0x41CE40` | `0x2C6A3F8` | 2 |
| 2 | MatchStart | `0x41D380` | `0x2C6A4C8` | 4 |
| 4 | RoundStart | `0x41DC50` | `0x2C6A530` | 5 |
| 5 | Ready | `0x41D5B0` | `0x2C6A598` | 6 |
| 6 | Fight | `0x41C6F0` | `0x2C6A600` | 7 |
| 7 | Finish | `0x41C7A0` | `0x2C6A668` | 8 |
| 8 | RoundOver | `0x41D9D0` | `0x2C6A6D0` | decides |
| 9 | RoundResultDemo | `0x41DBB0` | `0x2C6A7A8` | |
| 10 | RoundDoubleKODemo | `0x41D930` | `0x2C6A810` | |
| 11 | MatchResultDemo | `0x41D160` | `0x2C6A878` | 13 |
| 12 | MatchDrawDemo | `0x41D020` | `0x2C6A8E0` | 13 |
| 13 | MatchOver | `0x41D0C0` | `0x2C6A948` | decides |
| 14 | MatchEnd | `0x41D460` | `0x2C6A460` | none |
| 17 | RoundJump | `0x41D6E0` | `0x2C6A2C0` | 4 |
| 18, 19, 15 | BattleRestart, BattleStartReplay, BattleOver | generic `0x2C6A328` | | 2, 1, 16 |
| 16 | BattleEnd | `0x41C0C0` | `0x2C6AA18` | |

There is no Skip (22) and no OutroDemo (21) state.

**How a state change works.** `FlowUnit::PreTick` (rva `0x3DD890`) is a phase-2 battle task. It
is registered at rva `0x3D9660` with the input unit's phase-2 tasks (rva `0x439A60`). The switch
runs the old state's Exit, moves the current pointer, broadcasts the state message
`{old id, new id}` (the one `h_FlowMessage` hears), then runs the new state's Enter (vtable +8,
which binds the phases) and Begin (vtable +16, phase 0). States ask for the next one through
`FlowUnit::Request` (vtable +16, rva `0x3E40C0`). Request looks the id up in the map at +272 and
sets +308/+312, or returns 0 for a state the flow does not have. A phase moves on with rva
`0x414670`. That function rebinds the delegate to the phase's function, restarts its wait
(rva `0x3E3130`) and, when the wait is 0, runs the phase at once.

**After the deciding KO.** The flow goes Fight, Finish, RoundOver, MatchResultDemo (or
MatchDrawDemo when nobody won), MatchOver, MatchEnd. The versus battle answers the switch to
MatchEnd with its results call (rva `0x66A0D0`, `h_ShowResult`).

- **Finish (7)**: phases bound at rva `0x3EEE00`. Their waits come from a data object (defaults
  180, 36, 18, 180) plus fixed waits of 120, 18, 60 and 120 ticks. Phase 12 (rva `0x40BC00`)
  waits up to 1200 ticks for the KO's presentation (rva `0x2FB5D0`). Phase 17 (rva `0x40DA20`)
  waits up to 1200 ticks on rva `0x2F2DA0`. The last phase, 14 (rva `0x40EBE0`), asks the flow
  for the state at +144 (8), with flags +148 and the state's argument +8. **No phase reads a
  pad.**
- **RoundOver (8)**: one phase (rva `0x40AA70`) that runs on Begin. With the match decided
  (battle data +493448 = `kDecided`) it asks for 11, or 12 when the winner is -1. Kinds 5, 6,
  10 and online kinds go elsewhere.
- **MatchResultDemo (11)**: phases bound at rva `0x3EF3B0`.
  - 0 (rva `0x40DBC0`) goes to 2, or to 1 when that check fails.
  - 1 and 2 (rva `0x409100`, after 90 ticks or at once) set the skip gate +176 from rva
    `0x3F0020` and go to 3.
  - **3 (rva `0x409930`, after 5 ticks) is the only post-KO phase that reads the skip bit**
    (`0x2F2D40` with 0x4000, through `0x2F30D0` once decided), and only while +176 is set. On a
    hit it calls rva `0x2F3030` (the ResultUnit-side mark +308 = 1/2/3 by winner), sets +180 = 1
    and goes to 4. Without a hit it goes to 4 once the winner's demo has played (rva
    `0x2FB500`) or after 600 ticks.
  - 4 (rva `0x3DAFC0`) goes to 5. Phase 5 (rva `0x40E6A0`) sends the battle's "StartResult"
    event, starts the ResultUnit's sequence (rva `0x48D7D0`) and, unless +180, clears battle
    data +6196.
  - 6 (rva `0x40F2E0`) waits for that sequence (rva `0x47FD80`, ResultUnit+248's vtable +16).
    7 (rva `0x3DADB0`) asks for MatchOver.
- **The gate, rva `0x3F0020`**: open only when the battle data's kind (+0) is 1, the object rva
  `0x2F72A0` returns has +268 clear, there is no team battle (rva `0x2FC230`) and no online kind
  (rva `0x2FC2A0`). The default kind is 3 (rva `0x2EDCB0`). The log shows the gate shut in room
  battles: the bits were given in phase 3 (it runs for up to 600 ticks, and skip frames come
  every 15) and nothing happened. The adapter now logs the kind at bind.
- **MatchDrawDemo (12)**: phases bound at rva `0x3EF2A0`: 1 (`0x409000`, 120 ticks), 2
  (`0x409870`, waits for the demo or 600 ticks), 3 (`0x409E50`), 4 (`0x3DAFC0`, 300 ticks), 5
  (`0x40E460`, "StartResult"), 6 and 7 as above. No phase reads a pad.
- **MatchOver (13)**: one phase (rva `0x40A090`) that runs on Begin. It asks for MatchEnd (or
  OutroDemo 21 / MatchLose 23, which this flow does not have).

**Who reads 0x4000 at all.** The callers of rva `0x2F2D40` are flow phases `0x409930`,
`0x409BA0` (RoundDoubleKODemo, 120 ticks), `0x409C60` (RoundResultDemo, 300), `0x40EE00` (a
state class of another flow, vtable near rva `0x2C6DC90`), `0x40EEE0` (the generic intro),
`0x40F000` (the versus intro, class `0x2C6A3F8`) and units (`0x48E770`, `0x4920A0`,
`0x492900`, `0x492EF0`, `0x49F970`, `0x49FB00`, `0x4A1230`, `0x4AA110`, `0x4B7D60`). Rounds 9
and 10 come only before the decision. Of the post-KO states only MatchResultDemo's phase 3
asks.

**The input unit after the decision.** `BattleFrameUpdate` (rva `0x4DC6A0`) runs phase 2 (the
ticks) only while the frame is not paused (+0x190 clear or +0x194 set). FlowUnit's PreTick and
the input unit's phase-2 tasks are phase-2 tasks, so the flow cannot change state on an update
whose input unit did not tick. The log agrees: the local player's presses after the decision
reached the session (`Sampled(0)` kept changing).

**The pause menu's Skip and ReceiveSkipMatinee.** Pause command 2 (rva `0x461850`) posts a
`TransitionFlowEvent` for state 22 (Skip). The flow receives it at rva `0x3D4500` and calls
Request. The versus flow has no 22, so Request returns 0 and nothing happens. Command 1 posts
state 2 with argument 1 and command 6 posts 15. Flows that do have Skip (rva `0x4274B0`) use a
generic state that waits 30 ticks and goes to OutroDemo. Either way it is an event on one copy.
`ReceiveSkipMatinee` (rva `0x5A06B0`) is a scene-state machine outside the battle. In its state
3 it reads the local pad manager directly (rva `0x4CF800`, bit 0x4000) and stops a matinee (rva
`0x6C0170`). It is not part of the battle tick and would move one copy only. Neither is the
lever.

### The three candidate causes

1. *The local press never reaches the session (input record frozen).* **Not real** in the
   field log: both players' presses reached the session. It is guarded anyway (below).
2. *The flow keeps announcing Fight, so the bit is stripped.* **Not real**: the bits were given
   on the skip frames, which happens only when the announced state is not Fight.
3. *The scenes do not poll the pad.* **Real.** Finish, RoundOver, MatchDrawDemo, MatchOver and
   MatchEnd never ask. MatchResultDemo asks only behind a gate that room battles keep shut.

## The fix

**Ending the scenes ourselves, on the confirmed skip frame.** `SfvAdapter::Advance`: when the
frame's given inputs carry the skip bit and the match is decided, `EndScenes` runs before the
frame's update. It moves the current flow state on the way the game's own phases do
(`intro_skip::ScenesStepFor`, pure, tested). Each move checks the state's id **and** its class:

| current state | phase | move |
|---|---|---|
| Finish (7, `0x2C6A668`) | any | Request(+144, +148, +8), exactly what its last phase does. Only when +144 is RoundOver (8); otherwise it plays on (logged). |
| MatchResultDemo (11, `0x2C6A878`) | 1 (the 90-tick wait) | SetPhase(2): the same setup without the wait |
| MatchResultDemo | 3 | the game's own skip: `0x2F3030`(flow context), +180 = 1, SetPhase(4). This runs on at once to the "StartResult" event and the wait for the results. |
| MatchDrawDemo (12, `0x2C6A8E0`) | 2 (waits for its demo) | SetPhase(3), which goes to 4 at once |
| MatchDrawDemo | 4 (300 ticks) | SetPhase(5), the results' event |
| anything else | | nothing (logged once per state and phase) |

So a skip from Finish takes two skip frames (Finish to RoundOver to MatchResultDemo, then the
demo's skip). The results' event, the ResultUnit's sequence, MatchOver, MatchEnd and the results
call then play as unskipped. The prompt still opens on SFV's results (`h_ShowResult`,
`RoomBattle::TakeEnd`), as section 58 wants.

**Why it is rollback-safe and the same everywhere.**

- The skip bit reaches the game only on skip frames of a decided match's scenes (or the intro).
  `RollbackSession::Step` steps those only once confirmed (`ReadsConfirmed`), and every path
  (the fighters' step, a watcher's frame, a replay's frame, the synctest) gives the same
  `GivenInputs`.
- The move depends only on the state at the frame's start and on those inputs. Everything it
  writes is the flow's: requested index and argument, phase, delegate and the state's own
  fields. That is all in the `SfvFlow` record, which is captured after the update.
- No rollback reaches back past a confirmed frame. `0x2F3030`'s write (+308 of the unit rva
  `0x2E7900` finds) is the one write outside the flow record. Whether it is in the game's
  snapshot is not established. It is written on the same confirmed frame on every copy.
- Replays record the given bits with the frame and play them back unchanged, so a playback
  makes the same moves.
- The pad source is the session's while the moves run, so any pad question they cause hears the
  session's inputs.

**Who asks.** `intro_skip::Request` (each copy's sender) asks from the first **new** press made
at least `kScenesGuardFrames` (60) session frames after this copy saw the match decided. A
button down from the fight is no press until released. Presses inside that first second are
ignored and not taken up later. Either player's request skips for both. In the scenes the local
press is `Sampled(0)` (the pad as the input record sees it) plus the game's own unhooked pad
query (`GameLocalPadsAny`, rva `0x2F30D0` through the trampoline, reported as the skip bit).
That covers candidate 1 should the record ever stand still. A test's script presses nothing in
the scenes except where the pipe's `skipafter <n>` says. Plain scripted runs therefore still
play the scenes whole. After a rematch in place the request starts over, so the answer's button
skips nothing.

**Diagnostics** (one game run settles the rest):

- `adapter: battle kind K (the winner's demo opens its own skip for kind 1 only)` at bind.
- `netplay: the match is decided at frame D (flow state S, T input tick(s) in the last update)`
  and `netplay: the match is no longer decided at frame F`.
- `flow: state A -> B with the match decided (T input tick(s) so far in this update[, a frame
  replayed after a rollback])` for every flow state change after the decision.
- `adapter: T input tick(s) in this update with the match decided (flow state S; before, P)`
  whenever the tick count changes.
- `hooks: the battle asked the pads for M and heard yes from the session's inputs (call N)` for
  every injected yes, and before each flow state change
  `hooks: before flow state S the battle asked the pads for M n time(s) (i from the session's
  inputs), y yes`.
- `netplay: this copy asks to skip the scenes from frame F, k frame(s) into them`.
- `scene skip: ...` (adapter) and `netplay: scenes skipped at frame F: ...` for each move applied,
  or `scene skip: the skip bit in flow state S (class rva C) phase P ends nothing yet`.
- `netplay: the scenes took N frame(s), from the decision at frame D to the result at frame R
  (K skip(s) applied)`.

**Code sites** (`SfvOffsets.hxx`, in `kSites` for BuildCheck): `FlowRequest` `0x3E40C0`,
`SetFlowPhase` `0x414670`, `MarkResultSkipped` `0x2F3030`. The 16 signature bytes of each were
read from the laptop database. Layouts are in `src/mod/SfvResultFlow.hxx`: state ids, class
vtables, +32 id, +144/+148 Finish's next, +176/+180 the demo's gate and skipped, phase numbers,
battle data +0 kind.

## Compatibility (must go with this change)

The skip bit now changes the simulation where beta.7 ignored it. beta.7 already sends it after
any press in the scenes (the log is full of such frames), so a beta.7 copy and a beta.8 copy
would split at the first press. A beta.7 replay would also leave its recording in beta.8,
because its recorded skip frames now end scenes. The match version in the room build id
(`kMatchBuild`, `src/ember/SfvPickSpace.hxx`) must be bumped for beta.8. The replay check
(`SfvReplays.cxx`, header build) then refuses beta.7 replays, and rooms never seat the two
together. Stream F's layout change bumps it too; one bump covers both. The pick space
(`data/pick-space.json`) and the room host then need re-exporting and rebuilding.

## Tests

`tests/IntroSkipTests.cxx` (in `sfve_tests`):

- the request in the scenes with frames, including the guard: a mash inside the first 60
  frames is ignored and not taken up, and a button held through the guard counts only once
  pressed again;
- the scenes' clock: start, and start over after a rollback brings the fight back;
- the script's scheduled press, and one scheduled inside the guard asking nothing;
- `ScenesStepFor` for every state, phase and class;
- the state ids.

Not built or run here: no builds on this PC.

## What the game run must check (laptop, with the owner's OK)

1. `tools\runroom.ps1 -games 1 -lag 30` (no skip). Each copy logs the decided scenes, about
   1545 frames, and **no** skip applied. Read the `flow:` lines, the `hooks:` tallies (does
   MatchResultDemo ask 0x4000? how often?), the tick lines and `battle kind`. This is the
   baseline.
2. `tools\runroom.ps1 -games 2 -lag 30 -skipAfter 90 -skipCopy 1` (P2's press). Both copies log
   the same `scenes skipped at frame` lines and the same result frame, and the scenes take fewer
   than 700 frames. Expect about 60 + 30 + up to 95 + the results' own wait. 0 differing
   checkpoints. The prompt still opens on SFV's results: take `-shots`.
3. The same with `-skipCopy 0` (P1's press).
4. `-skipAfter 20`: the press falls in the guard. No skip, and the scenes take 700 frames or
   more.
5. `-skipPad -skipAfter 90`: copy 1's virtual pad (X) through the real pad path.
6. `-rematches 1 -padAnswers -skipAfter 90 -lag 30`: the skip, then the rematch in place and a
   second WIN. The restart's few decided frames must not ask (no `asks to skip` line before the
   second game's decision).
7. With `-watchers 1`: the watcher logs the fighters' skip frames and result frames (runroom
   compares them).
8. Look at the screens after a skip (`-shots`): the winner's demo cut, the results screen as
   unskipped (the "StartResult" event and the ResultUnit's sequence still run), Ember's prompt
   over it, nothing left in slow motion or with a wrong pose. Finish's exit-only step (rva
   `0x40BC00` sets each fighter's +2376 to -1 before it leaves) does not run when Finish is cut
   short. Check the winner's demo looks right.
9. A draw (double KO or time-over draw at the deciding round) if one comes: MatchDrawDemo's two
   moves.

If phase 6's wait for the ResultUnit's sequence turns out to differ between copies (the result
frames would differ, and runroom fails on that), the skip from MatchResultDemo has to move
further on its own. The unskipped path waits there too and was in sync, so this is not expected.
