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

## The results screens (MatchResultDemo's phase 6), after the first laptop runs

**What the runs showed** (`D:\sfv-re\b8-runs\R2\`, `-skipAfter 90 -skipCopy 1`; R3 with
`-skipCopy 0` the same). Battle kind 1, so the demo's own gate was open. Decided at 2334, the
copy asked at 2424, Finish ended at 2430, the demo went 1 → 3 at 2445 and was skipped 3 → 4
(now in phase 6) at 2460. The result call still came only at 3813, about 1350 frames later, all
of it in phase 6. Both copies matched exactly (0 differing).

Before MatchOver the tallies showed: `the battle asked the pads for ffffbff0 1202 time(s) (1202
from the session's inputs), 0 yes`. Something in phase 6 asked for any button on every tick, and
the session's skip frames carry only 0x4000, which that mask leaves out.

**What phase 6 waits for (IDA).** Phase 5 (rva `0x40E6A0`) starts the ResultUnit's sequence
(rva `0x48D7D0`: the +1184 object of what rva `0x2F78A0` returns gets its vtable +1552 with 1,
then ResultUnit+248's vtable +8). Phase 6 waits until
that object's vtable +16 says it is done (rva `0x1B27750`: its +120). The object is made by rva
`0x480D70` from the battle data's kind:

- kind 7: vtable `0x2C70728`
- kind 10: rva `0x478430`
- kinds 0 and 11: rva `0x4782A0`
- team battles: rva `0x479BB0` / `0x479B70`
- **everything else, kind 1 included: rva `0x4793E0`, vtable `0x2C700D8`**

It is a phase sequencer like a flow state: map at +24, current at +40, delegate at +48, elapsed
at +92, a press cooldown at +124, done at +120. Start (vtable +8, rva `0x48D5A0`) binds page 0
and one of several page lists. For a versus battle that is rva `0x490D10`, then rva `0x490EB0`.
Their pages drive the battle HUD's Scaleform movie (strings from the UI calls they make):

1. **The win quote**:
   - `GenerateRoot`, `ResetChart` (rva `0x493F40`)
   - `Set_Message` and `StartFadein_Message`: the winner's quote (rva `0x493970`)
   - `Set_ResultType`, `Set_PlayerIndex` (rva `0x49B430`) and `Set_PlayerId` (rva `0x499480`)
   - `StartFadein_Result` (rva `0x493620`) and `StartAnimation_ResultType` (rva `0x49ED00`)
   - then **rva `0x49FB00`: any button or 600 ticks**
   - then `StartFadeout_Result` (rva `0x493850`) and 30 ticks (rva `0x49F040`)

   This page set is skipped when nobody won.
2. **The results card**:
   - `BattleFlow::PlayInformationVoice` (rva `0x49B990`), `GenerateRoot`/`ResetChart` (rva
     `0x49A250`)
   - the card: `SetCharacterName`, `SetFaceIconPath`, `SetWinCount`, `SetWinRate`,
     `SetStraightWinCount`, `SetRoundCount` (rva `0x49A2E0`)
   - `StartFadein_Result`, `StartAnimation_ResultType` after 30 ticks (rva `0x49EBE0`)
   - the round icons (rva `0x49EAC0`, `0x49F2F0`, `0x492870`: `SetRoundResultIcon`,
     `Set_SingleResultRow`)
   - then **rva `0x49FB00` again: any button or 600 ticks**
   - then vtable +0x28 (rva `0x491FE0`), which sets done (+120 = 1) once the game instance allows
     (rva `0x690630`).

Two pages of up to 600 ticks plus their animations make the 1350 frames. After them the flow
plays MatchOver and MatchEnd, and the results call (where Ember's prompt opens) comes as it does
unskipped.

**How the game's own skip ends them.** Offline, a press does. Page rva `0x49FB00` counts its
cooldown +124 down each tick. Once it is at or below 0 it asks the battle's pad query rva
`0x2F2D40` with mask **0xFFFFBFF0** (every attack and more, not the skip bit 0x4000). After the
decision that query asks the pads themselves (rva `0x2F30D0`, which the mod hooks). On a yes it
plays sound 200 (rva `0x777F40`), sets the cooldown to 30 and turns the page. So it is a pad read
in the battle tick, not a timer, a UMG callback or a Scaleform event. The other modes' page lists
ask the same mask: rva `0x4920A0`, `0x492900`, `0x492EF0`, `0x49F970`, `0x4A1230` and
`0x4B7D60`. The one other pad caller, rva `0x4AA110`, asks mask 0x40 (LK).

**The fix.** `intro_skip::PadsHear`: while a session's inputs play a tick
(`SfvAdapter::InjectedAny`, answering the hooked rva `0x2F30D0`), a question hears both players'
given bits of its mask, as before. In addition, **once the match is decided, the skip bit
answers a question for any button**: one whose mask holds every attack, as 0xFFFFBFF0 does. The
request then turns each page the way a player's press does offline. Because the request stays on
every skip frame (one in 15) until the scenes end, and each page waits out its 30-tick cooldown,
each page turns within about 30 frames: two pages and their fades, instead of 1200 ticks.

**Why it is safe.**

- The answer is a function of the tick's given inputs and of `kDecided` only. Both are the same
  on every copy on that frame:
  - the inputs are the confirmed skip frame's, which every session path (fighter, watcher,
    replay, synctest) gives alike through `GivenInputs`;
  - `kDecided` is rollback state.
- The question is asked inside the battle tick while the session's inputs play it (the tallies
  show all 1202 calls "from the session's inputs"). So the page turns on the same tick on every
  copy, and no copy reads its own pad.
- Nothing is written from outside the game: the game's own page code advances its own sequence.
  It does that in the unskipped path too, deterministically: the runs' result frames matched.
- No rollback reaches the pages:
  - frames after the decision read no input, so no misprediction can start a rollback there;
  - a rollback from an earlier fight frame cannot cross a confirmed skip frame.
- The answer is narrow. Directions (masks 1, 2), single buttons (0x40) and the game's own skip
  question (0x4000, which hears the bit itself) are answered as before. The intro (not decided)
  is unchanged.
- The page code that hears the press is the code the game runs on an offline press: sound 200,
  then the next page.

## Between rounds, and SFV's own pause (2026-11-12)

**What the owner saw.** "In between rounds I can't figure out how to skip the after scene, and
sometimes that will open the pause menu and instantly desync the game."

### SFV's pause opens on one copy only

From IDA:

- **The detector.** The battle's `PauseUnit` registers a PreUpdate task, rva `0x46E110`.
  Registration is at rva `0x46B8F0`: name "PauseUnit", order 0x1020000, scheduler phase 1, the
  battle's thread. Phase 1 runs in every battle update (`BattleFrameUpdate`, rva `0x4DC6A0`),
  paused or not.
- **What it reads.** For each local player record of the match data (+540 == 0, pad at +544),
  then pad 0, it reads the pad manager directly: rva `0x4CF800` (this frame) and rva `0x4CEF40`
  (held), bit 0x4000 in the pad manager's bits (Start). These are the PC's own pads. The
  session's inputs never come into it.
- **When it may pause.** Only while the match data's +6352 has bit 4 (the flow allows a pause;
  the owner hit it between rounds) and the battle frame is not stopped by a KO (+0x190 bit 0x20).
- **What it does.**
  - It sends the battle a `PauseControlEvent` (vtable rva `0x2C6DF18`, type registered at rva
    `0x13BD00`).
  - The receivers, rva `0x4639B0` and `0x463E40`, set the battle frame's pause bits through rva
    `0x4D5DE0` (+0x190 |= 1, 2 << pad).
  - `BattleFrameUpdate` then skips the ticks on that copy, and the pause menu units open.
  - The other copy plays on. +0x190 is in the hashed `battleframe` region, so the next checkpoint
    differs: the reported instant desync.
  - The same task also sends a pause the game's menus asked for (+248..+256, set by rva
    `0x4709C0`).
- **Other senders of the event** (rva `0x461850` and `0x461B10`..`0x462F80`, the pause menus'
  commands, `0x481620`, `0x4F92F0`, `0x501650`) act from within an open pause menu or from UI.
  None reads a pad. The test mode's `TestModePauseUnit` (rva `0x46F0E0`) is not in a versus
  battle.
- **Who sets the pause bits.** The bits are +0x190's 1, 2 and 4, and only the event's receivers
  set them (rva `0x4D5DE0`'s other callers pass 0x10, the KO's 0x20 and the replay control's
  0x80).
- **Ember's own Start.** In a room fight Ember's shell is kept away (the library's
  `FightKeepsMenuAway`), so Start went to the game. In a Practice match or a watched game, Start
  opens Ember's menu first.

**The fix.** `GameHooks` hooks rva `0x46E110` (`sfv::kPauseUnitPreUpdate`, added to `kSites`).
The task runs only when `BattleListener::GamePauseAllowed()` says so:

- `BattleDriver`: not while a linked or replayed battle loads (`LaunchedBattleLoading`).
  Otherwise the mode decides.
- `NetplayMode`: never while a room is active (fighters, watchers, a replay's playback) or a
  session is live (linked tests).
- Every other mode, and offline battles, Training and offline versus: allowed. They keep SFV's
  pause.

Start therefore does nothing in a room battle, and no copy can set the pause bits, the only pause
state in the rollback layout. Ember's shell is untouched: its Start is read in the XInput hook
before the game and behaves as before. The hooks log `SFV's own pause is off: a session plays
this battle` on each change. After every session update the adapter logs `the game's pause bits
are N` whenever those bits change, so a pause from any other path would show.

### The scenes between two rounds

The flow after a KO or time-over that does not decide the match (IDA):

1. **Finish (7)**: the same class as at the match's end (`0x2C6A668`).
2. **RoundOver (8)**. With the match data's `kDecided` 0 (rva `0x40AA70`) it asks for one of:
   - **RoundResultDemo (9)**: class `0x2C6A7A8`, phases bound at rva `0x3EFD50`;
   - **RoundDoubleKODemo (10)**: class `0x2C6A810`, phases bound at rva `0x3EFBD0`.
3. The demo's phase 5 (rva `0x40A9F0`) asks for **RoundStart (4)**, then comes **Ready (5)**:
   the next round's "ROUND 2", "FIGHT".

How each round demo runs:

- Phase 0 (rva `0x409720` / `0x4096F0`) marks phase 3 as the state's skip target (rva
  `0x3EC380`).
- Phase 1 runs for up to 300 ticks or until the demo ends (rva `0x409C60`, `0x2FB570`). In the
  double-KO demo it is up to 120 ticks (rva `0x409BA0`).
- Phase 1 asks the battle's input for 0x4000 (rva `0x2F2D40`). Before the decision that is the
  input the battle reads: the session's given inputs.
- On the bit it goes to the marked phase (vtable +56, rva `0x445040`), then phase 4 (15 ticks)
  and the request for RoundStart.

So the round demos end on the skip bit by themselves. Until now the bit never reached them,
because frames between rounds were not skip frames.

**The fix.** `intro_skip::RoundScenes` (states 7, 8, 9, 10) is now part of `Skippable` and
`Scenes`:

- Their skip frames read the bit confirmed, as the intro's and the match's do.
- `Request` asks from a fresh press after the same 60-frame guard. With the room's rule on, it
  asks by itself after the guard (`SetAutoScenes`).
- The request clears at RoundStart.
- `SfvAdapter::Advance` ends Finish on the confirmed skip frame exactly as at the match's end
  (Request(RoundOver), `ScenesStepFor`). RoundOver leaves on its first tick, and the round demo
  takes the next skip frame's bit itself.
- RoundStart and Ready (the round call, "FIGHT") are never skippable.

It is safe for the same reasons as the match's end: confirmed frames, a move that depends on the
state at the frame's start and its given inputs, flow state in the rollback record. The demo's
own read is of the battle's input record, which is rollback state.

The guard also absorbs the button that landed the KO, as at the match's end. The any-button
answer (`PadsHear`) stays a decided match's: between rounds nothing asks for any button.

**Logs.**

- `netplay: a round is over at frame D ...; its scenes play to the next round's call`
- `netplay: the round's scenes took N frame(s), from the round's end at frame D to the next round
  at frame R, flow state S (K skip(s) applied)`
- `netplay: the round's end seen at frame D was taken back by a rollback at frame F`, for a guessed
  KO that a rollback took back
- the `scenes skipped at frame` lines, as at the match's end.

**Tests** (`IntroSkipTests`):

- the round states are skippable and are scenes, and the round call, RoundJump and the fight are
  not;
- a press inside the guard asks nothing, and a fresh press after it asks until RoundStart;
- the rule asks by itself after the guard and never into the round call;
- Finish between rounds takes the same step, and the round demos are left to the game;
- the cases whose meaning changed (Finish undecided is now a skip frame).

**runroom.**

- `-rounds 2` (or more) prints and checks every round's scenes with `CheckRounds`: the same
  next-round frame and the same skips on both copies, skipped with `-skipAfter N` (N >= 60) or
  `-autoSkip`, and no skip otherwise.
- `-pressStart` makes copy `-skipCopy` press Start half a second into every round's end and every
  match's decision.
- At the end of every run, any copy whose battle frame was paused fails it. With `-pressStart`,
  both fighters must have logged the game's pause off.

**What the laptop run must check.**

1. `runroom.ps1 -games 1 -rounds 3 -lag 30 -pressStart`: Start pressed between rounds and in the
   match's scenes on copy 1. No pause on any copy, 0 differing checkpoints, the rounds' scenes
   whole (no skip), the game's pause logged off on both.
2. `-games 2 -rounds 3 -lag 30 -skipAfter 90`: every round's scenes skipped on the same frames on
   both copies. On screen, the next round's "ROUND n / FIGHT" call plays in full. 0 differing.
3. `-games 2 -rounds 3 -lag 30 -autoSkip -watchers 1`: the same without a press. The watcher's
   checkpoints match.
4. Offline (not a room): SFV's own pause still opens with Start in Training and in a local versus
   battle (`hooks: SFV's own pause is on`), and Ember's Start over a Practice match still opens
   Ember.

### R2 and R3: not the skip

Build 3's runs R2 (`-games 2 -rounds 3 -lag 30 -skipAfter 90`, a fighter at 3000) and R3
(`-autoSkip -watchers 1`, the watcher at 3480) "diverged" a few hundred frames after a
between-round skip. Both copies' states at 3000 were byte-for-byte equal (only the sync clock,
left out of the hash, differed). The hash had zeroed different words of an unowned 632-byte
table of small counters on each copy, because its address tie-break asked each process's own
memory (`VirtualQuery`) and a counter (0x173, 0x1EE) matched one process's heap. The skip only
moved the counters to those values at the moment the next round began. Fixed in the hash
(`StateLayout::AddressAt` decides by value; `desync.md`, "Superseded the same day"). The skip
itself is unchanged.
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
4. `-skipAfter 20`: the press falls in the guard. No copy applies a skip, and both copies take
   the result on the same frame. (Run R4 found this right: 0 skips over 1865 frames. The old
   frame-count check was wrong; the unskipped length varies, 1545 and 1865 frames have been
   seen.)
   A shot of three copies can take several seconds. In run S6 (`-rematches 1 -padAnswers -shots
   -watchers 1`), two blocking shots between the prompt opening and copy 1's B answer pushed the
   B past the prompt's 10 s countdown. Copy 1's shell closed at +10.01 s ("Back on Ember's Home"),
   P2 never declined, and its asking lapsed at 11 s. The unanswered prompt gave up the seat, so
   the room's results prompt never came. The shots over the prompt, and every shot taken during
   the scenes, now run without blocking (`ShotAsync`). The run waits for them only at its end.
   With `-shots`, every game also gets `game<N>-after-skip-<copy>.png`, about 1 s after copy 0
   first applies a skip, and `game<N>-before-result-<copy>.png`, from about 1 s before copy 0
   logs its result. That one is picked from rolling shots taken from the decision on. Games
   where the run answers the prompt at once (`-quickAnswer`, or the last WIN with `-padAnswers`)
   take only the after-skip shot.
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

Runs R2 and R3 matched on both copies through phase 6 and failed only on its length. The
results screens' pages (above) now hear the request. The re-run of 2 and 3 must show the
`hooks:` tally for `ffffbff0` with yes answers before flow state 13. It must also show the
scenes well under 700 frames, a "sound 200" page turn on screen in the after-skip shot, and the
results card (or its fade) in the before-result shot.
