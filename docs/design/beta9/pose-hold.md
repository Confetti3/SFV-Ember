# beta.9: holding a tick for a drawn frame (pose holds, 2026-11-13)

The socket record (`src/mod/SocketReads.hxx`, header comment lines 1-55; beta.8 notes in
`docs/design/beta8/desync.md`) serves a fight socket read on a stale pose from earlier fresh
reads. A pose never read fresh on this copy kept the live value, which is another pose's: the
last desync class the beta.9 candidate showed (three two-copy rooms at 80 ms, checkpoints 9060,
2300 and 1600; misses 26-39 in the first 1,800 updates, 73-84 by 14,400). Evaluating the pose
on demand is not possible (same header, lines 45-50). A pose hold makes the engine evaluate it
instead.

## 1. Mechanism

A read only a drawn frame of the update's start state gives right is fresh on the first update
after a drawn frame with nothing restored before it. When an update of frame F missed (any
`MissReason`, replayed frame or the new frame after a rollback):

1. The update reports it: `GameAdapter::WantsDrawnFrame` (SfvAdapter: the update's reads had a
   miss, `TakeUpdateReads`).
2. The session books it (`pose_hold::Book::Wanted`): the earliest such frame of the tick
   decides; a later frame of the same tick is covered by the hold (it is replayed too), and
   after a refusal at F the frames after F keep their live values for this tick. A rollback in
   the same tick that loads F's state or an earlier one voids the pending hold (`Loaded`): that
   replay decides again.
3. Checks (`RollbackSession::ConsiderPoseHold`): F - 1 not a held frame, the state F - 1 starts
   from kept here (`SavedState`, a copy, not junk) and in GGPO (`ggpo_can_hold_at`: the ring and
   every player's input for F - 1). Then the hold is pending and every save of a later frame is
   junk.
4. At the end of the call that rolled back (`Step`, `StepHeld`, `Poll`: `FinishPoseHold`), once
   every GGPO call of the tick is done: `ggpo_hold_at(F)` loads F - 1's save (the load callback,
   `AfterLoad` with the game's post-restore callbacks) and runs one advance callback, which plays
   F - 1 again. Its last phase, the visual task (rva 0x3A06C0), places every actor for F's start
   state. The call returns with the game at F's start. The engine draws: it evaluates exactly
   that pose.
5. The next call's GGPO plays F up to the frame the tick stood at before anything else
   (`Sync::ResumeHold` at the top of `DoPoll`, `AddLocalInput` and `DisconnectPlayer`, before
   anything is received), as advance callbacks of a rollback, saving each frame again. F's
   update is the first since the drawn frame: its read is fresh, recorded, and what the copy that
   guessed right read. Then the call goes on as usual (receive, roll back, the new frame).

If the hold's own replay of F - 1 wants a drawn frame too (a read the first frame after a load
cannot rebuild: `placed` is false then), the state F starts from kept a live value, so the
session holds at F - 1 instead (`ggpo_hold_at` again before the replay; the frames left reach
the same frame), at most `kChainLimit` (4) frames back.

No simulated frame is lost: the tick that holds has already played its new frame (it is
replayed with the rest), and the next tick replays and then plays its own.

### The GGPO patch (`vcpkg-overlays/ports/ggpo/pose-hold.patch`, port version 22)

- `Sync::Hold(frame)`: refuses inside a callback, when `CanHold` fails, or when a queue still
  holds a misprediction; loads `frame - 1` (`LoadFrame`, `ResetPrediction`), one
  `advance_frame` callback, and records `_held_to` (the frame count before, or the one already
  recorded when called again before the replay). Returns the frames left.
- `Sync::ResumeHold()`: when `_held_to` is set and not in a rollback, advance callbacks until
  `_framecount` reaches it, rolling back (so `DoPoll` inside them does nothing). Each one saves
  its frame again, so GGPO's ring holds the corrected states before any later rollback can load
  one.
- `Sync::SetLastConfirmedFrame` keeps every input whose frame's save is still in the ring
  (`_framecount - 9` on): a hold may replay from any saved frame.
- `InputQueue::CanGetInput`, `ggpo_can_hold_at`, `ggpo_hold_at`.

### Bookkeeping in the session

- **Junk saves** (`Slot::junk`): the saves of frames after a pending hold's frame, and every save
  made after the miss in that tick. Always copied, never hashed, never `SavedState` (dumps), and
  dropped unhashed when the replay frees them. If no hold comes (GGPO refuses at the end of the
  tick, never expected), `Unjunk` hashes them as usual.
- **Copies**: with holds on, a rollback's and a hold's replay saves keep their copy (a later hold
  may load any of them), and the cheaper-rollbacks rule keeps a save's copy until the inputs
  `kChainLimit` frames further past it are confirmed (a chain loads up to that much earlier than
  a rollback could). With the input delays rooms use this copies nearly every save (about 1 MB
  each); the price of holds in the no-lag case.
- **Frame() and LastConfirmedFrame()** while a hold's frames wait: `Frame()` is still the frame
  the tick stood at (the host's scripts, scenes and pacing see no jump back); `LastConfirmedFrame()`
  stops at F - 1, so no checkpoint after F is taken by the verifier, no frame after F is
  confirmed to the replay recorder, and no match end is decided on the held state.
- **Fresh after the hold** (`socket_reads::PoseFreshness`, SfvAdapter): `fresh` is set by the
  battle frame's hook (once per engine frame, after the engine drew) and cleared by any update or
  restore; `placed` set by an update, cleared by a restore. After a hold the first update says
  once whether it was the held frame's and fresh (`AfterHold`). Not fresh (no drawn frame came,
  another update ran first, or something restored the state) counts as a failed hold, and the
  book refuses a second hold for that update's miss (`NotFreshAfterHold`): never a loop.

### Guards

- No hold outside rollback modes: only a session with `Config::poseHolds` (the netplay mode's
  fighters and lagged replays, `ChoosePoseHold`) acts on `WantsDrawnFrame`; watchers, the
  synctest and Training never do.
- No hold when F - 1's state is not kept (`StateNotKept`), F is frame 0 (`FirstFrame`), F - 1 or
  F is a held frame (`HeldFrame`), after the chain limit (`ChainLimit`), or for the first update
  after a hold at the same frame (`NotFreshAfterHold`). Each keeps the live value and is counted.
- One hold a call; the session kept for watchers after the fight never holds.

## 2. Nothing observable runs twice harmfully (code reading)

- **Sounds and effects.** A hold is a rollback split over two engine frames: a load of F - 1
  (the game's post-restore callbacks: the sound callbacks drop what was played after it, rva
  0x4D5820, 0x4B6230), F - 1 replayed, then F up to the tick's frame replayed. The game sees the
  same calls as a rollback to F - 1, with one drawn frame between F - 1 and F. The tick's new
  frame was played forward once and is replayed once, as after any misprediction.
- **The trace ring** (`DesyncTrace.hxx`): `NoteFrame` keeps the last report of each frame; the
  replay reports frames again. `NoteStep` counts the hold's load as a rollback of its step; the
  next step's replayed frames count as re-simulated without a rollback.
- **Replay recording**: `OnFrame` keeps the last report; `Confirm` uses
  `min(Frame(), LastConfirmedFrame() + 1)`, which stops before the held frame, so no frame from
  it is final until replayed. Checkpoints reach the recorder only through `RecordHash`.
- **Checkpoint hashing**: junk saves are never hashed; each frame is hashed once final from the
  save the replay made (`RecordHash` and `onCheckpoint` again only for a final frame saved again,
  as before). The verifier (`CheckpointExchange::TakeCheckpoints`) takes a checkpoint only once
  `LastConfirmedFrame()` passes it and `HashAt` has it.
- **Pace counters**: one forward update a tick still (the held tick's new frame); its replay
  counts as replayed. The session's `rollbacks` (the pace line's) count each hold once;
  `Stats::poseHolds` and `poseHoldsRefused` count them apart.
- **Watcher streams**: P1 streams confirmed inputs from GGPO's queues; a hold changes no input and
  no confirmation. Inputs stay queued a few frames longer, which `GetConfirmedInputs` does not see.

## 3. Lagged replays and watchers

- `LaggedReplay` (the replay check of field matches) holds the same way with its own book: the
  rendered frame ends at F's start after F - 1 is played again from its kept state, the next
  `Step` (or `Finish`, which now returns false until no hold waits) first plays F up to the frame
  reached, and `ConfirmedThrough()` stops at F meanwhile, so no checkpoint after F is hashed.
- A watcher plays confirmed frames only and keeps no saves, so it cannot hold. Its catch-up
  (`NetplayMode::WatchFrames`, several frames in one engine frame when behind) makes stale reads
  on the extra frames: served from the record, else live. A watcher can therefore still diverge
  while catching up (its report says it only watches); fixing that needs a watcher to keep two
  states, not built.

## 4. Logs

- One line per hold: `socket: hold at frame <input frame> (session frame F), unit u (Pn), socket
  S, from rva 0x...: the tick ends at this frame's start, its pose drawn; N frame(s) played again
  (session frame F-1 now, F to E next)`, then on the next tick `socket: hold at frame <input
  frame>: played again fresh` (or `... found the models not fresh ...`), and with
  `-sfve_socket_log=1` the read itself: `socket: frame <input frame> replayed, ... (fresh;
  recorded; ...)`.
- A refusal: `socket: hold at frame ..., ...: failed (<reason>); the read keeps the live value`;
  a dropped one: `...: dropped, a rollback in the same tick plays it again`.
- The `sockets:` lines and the battle's end add `H pose hold(s), F gave a fresh read; K failed
  (state not kept a, held frame b, not fresh after the hold c, chain limit d, refused by GGPO e,
  first frame f)`; the session's `session costs:` line and the lagged replay's end line count
  holds and refusals; the desync report's details have a `pose holds:` line.
- `-sfve_pose_hold=0|1` (development; default off since the socket anchors, `socket-anchors.md`) and the `pose hold on|off` token in
  `LayoutRulesId`: copies that hold and copies that do not never meet. The layout id changed
  (`ee964ab93eb142f6` with holds on, as `RoomBuildTests` computes it), so `data/pick-space.json`
  must be exported again from the game (the pipe's `catalog-export`, as for f25fa0e); until then
  `RoomBuildTests`' pick space check fails, and its build line would read
  `sfv-ember/13173604/0.7/55c1d32e7a18ea97/layout-ee964ab93eb142f6`.

## 5. Verified by code reading and tests only

- The GGPO patch, the session's junk bookkeeping and LaggedReplay: `tests/PoseHoldTests.cxx`
  plays two sessions over a lossy loopback link and lagged replays with the mock fighter's pose
  reads (`MockFighter::PoseReadsEvery`: exact readings of a pose and place, and, in the chained
  case, rebuilds of a stance read elsewhere that a frame right after a restore cannot make), and
  checks every final state and every reported checkpoint against a straight run that draws every
  frame, that every hold's next update was fresh, that chains happened, and that without holds
  the states differ. The book (`pose_hold::Book`), `socket_reads::PoseFreshness`, `HoldsText` and
  `ChoosePoseHold` have their own tests.
- Termination: each hold's next update reads its frame fresh and records a reading that was not
  there (it missed), so a run of holds ends once the window's poses are recorded; a chain is at
  most `kChainLimit` deep, and a hold whose next update is not fresh is never made again for that
  update. (Only the record's own eviction, 8192 poses and 4 places a pose, could undo a reading.)
- That the engine evaluates the poses between two calls of the battle frame from what the last
  update's visual task set (the record's own premise), and that the hold's replay of F - 1 places
  the models exactly as the straight play's F - 1 does.

## 6. What a game run must show

- Rerun rooms c, d, e of `D:\sfv-re\b9-pass\s3` and the four player-0 lag variants of field
  match 3: no desync; `socket: hold at frame ...` lines each followed by `played again fresh`;
  in the socket log the held frame's read logged `replayed ... (fresh; recorded; ...)` with the
  value the other copy read; `sockets:` lines with holds and `0 failed` or few; `0 conflicting`.
- No doubled or cut sounds or effects around a hold, and the one-frame picture lag not noticeable.
- The pace line: updates per second unchanged with holds.
