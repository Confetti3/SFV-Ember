# Beta.8: cheaper rollbacks

The owner asked for the comparison review's recommendation 4.4 (the review:
skip the state copies no load can reach, hash on confirm, check a sample of the loads) built
in Ember's own design. Nothing here comes from the other project's code, tables or text: the
rules below are derived from our GGPO port's own source (`adanducci/ggpo@c88b667` with
`vcpkg-overlays/ports/ggpo`'s patches) and our `RollbackSession`. Section 2.6's "do not" stands:
no lean replays, and every load still runs the restore's callbacks.

Branch `b8/perf` (from `b8/integrate` 25b0ddc). Not built, not run: the laptop runs at the end
say what must be checked.

## What a rollback cost before

`RollbackSession::Save` copied the whole layout (about 1 MB, `StateLayout::SaveTo`) on every
GGPO save: every frame played forward and every frame a rollback replays. A checkpoint frame
(every 60) was also hashed on every save of it, the replays' included, and with `verifyLoads`
every save was hashed twice. Saves dominate: a 4-frame rollback is 4 copies on top of the
frame's own (SFV_PORT.md, "A save copies and hashes about 1 MB").

## 1. A save no load can reach keeps no copy

**The rule.** GGPO labels a save with the frame it starts: save F is the state before frame F's
inputs. Our port loads save F in two places only (every `LoadFrame`/`AdjustSimulation` caller
in the patched tree):

1. `Sync::CheckSimulation`: a rollback to the first frame whose input was guessed wrong. That
   frame's input was not here when the frame was played, so it was not here when its save was
   made: at save time the confirmed frame C was below F.
2. `Peer2PeerBackend::DisconnectPlayerQueue`: a dropped fighter's resimulation, from
   `syncto` = that fighter's last frame received (or, in `Poll2Players`, the least last frame
   of the players up to it). Each player's last frame only grows, so `syncto` is never below
   any confirmed frame there was before: C at save time <= `syncto`. The load is of save
   `syncto` itself, a frame whose inputs may all be known.

So a save made when C >= F + 1 is reached by neither: (1) needs C < F, (2) needs C <= F. The
margin of one frame is for (2) only. (GGPO's own synctest backend also loads, but Ember never
starts it.)

**The confirmed frame "now".** `ggpo_get_last_confirmed_frame` is the number GGPO's last poll
set; a step's save comes after the step's own local input was added, which that number does
not count yet. With it, the save at the end of step F - 1 (save F) needs the local input for
F + 1 to be confirmed, which only a delay of 3 or more gives at that point: with the default
delay of 2 no save would ever be skipped. So the GGPO port gets a query of its own,
`ggpo_get_confirmed_input_frame` (`confirmed-input-frame.patch`, port version 21): the least
`_local_connect_status[i].last_frame` of the connected players, which is what `Poll2Players`
would confirm now. It counts the local inputs as added (never guessed) and the remote ones as
received by the last poll, which ran `CheckSimulation` on them before returning, so any guess
among them was already rolled back. It never goes back (last frames only grow; a dropped
player leaves the minimum). With more than two players it gives the last poll's number. It
does not poll and changes nothing in GGPO.

**What a skipped save keeps.** GGPO still gets a buffer (the pool slot, as before; GGPO never
reads it), so `Free` and the ring work as before. The slot is marked `copied = false`:

- `SavedState` answers nothing for it; a dump's frame keeps its copy (`KeepSave`, which the
  pipe's `dump` command calls).
- If the frame is hashed at all (a checkpoint, `hashEveryFrames`, `verifyLoads`), it is hashed
  at once from the live state, which is its state (`StateLayout::HashLive` and `HashSaved`
  hash the same bytes alike), and it is final (below). A checkpoint copies its state once,
  into the kept checkpoint state (`CheckpointState`, the desync report's bytes), and hashes
  that.
- `Load` of such a slot never happens; if it did, it counts `skippedLoads`, logs, and fails the
  session ("load of a save that kept no copy") without touching the game, as a restore the game
  cannot complete does.

## 2. Hash once final

A save of frame F is final once every input before F is confirmed (F - 1 <= C): a rollback
starts after C, and a drop's resimulation from `syncto` >= C re-saves only frames after
`syncto`, so F is never saved again with other bytes. (The one re-save of a final frame, F =
C + 1 after a drop at `syncto` = C, plays frame C on the same confirmed inputs: the same bytes.)
This is the desync check's own rule (`sf4e::statehash::IsConfirmedCheckpoint`).

With `hashOnConfirm`, a hashed frame whose save is not final yet is marked `pending`. After
every GGPO call that may confirm frames (`ggpo_idle` and `ggpo_advance_frame`, through
`Step`, `StepHeld` and `Poll`, whichever way they return), at each save, and before GGPO closes,
`HashConfirmed` reads C and hashes the pending saves that are final, oldest first, from their
saved bytes. A pending save that GGPO frees while not final was replaced by a rollback's save of
the same frame (GGPO's ring keeps one save a frame, and a rollback writes over the frames after
the one it loads); its hash is never made (`hashesDropped`). A pending save cannot reach the
ring's end before it is final: GGPO refuses a local input more than 8 frames past the confirmed
one, and its ring holds 10 saves. `Free` still hashes one that is final, for certain.

**During a rollback.** The confirmed frame "now" already counts the inputs that started the
rollback, while the saves after the frame it loaded still hold the guessed timeline until GGPO
saves each again. So from `Load` to the save of the frame the rollback returns to (or the end
of the GGPO call), a pending save of a frame after the loaded one is never hashed: it is about
to be replaced, and is dropped when GGPO frees it. The frames the rollback saves again are
judged as any save: final ones are hashed (or skipped) at once, from the corrected state.
After a failure inside a GGPO call (a load that could not restore) no further save is kept.

**Why the hashes are today's.** The same function (`HashSaved`, peers' scope) of the same bytes
(the save GGPO keeps for the frame, the one in force when the frame is final), with the same
masks and address zeroing (the layout's rules are fixed from bind to the end of the battle, and
the address test is a pure function of the value). What changes is only which provisional
hashes are made: the saves a rollback later replaces are not hashed, and `onCheckpoint` and the
"checkpoint:" line come once per checkpoint, when it is final, instead of once per save.
`HashAt` has no hash of a frame before it is final (no caller reads one: the verifier and the
recorder read confirmed frames, `ReplayRecorder::Confirm` takes min(Frame, LastConfirmed + 1),
which is final by then).

## 3. A sample of the loads checked

`verifyLoadsEvery = N`: every Nth restore, the restored state's full (local scope) hash is
compared with its save's, the slot's bytes, which nothing writes while the slot is in use, so
their hash then is their hash at save time. A mismatch is logged ("restored with a different
hash") and counted (`loadMismatches`). `verifyLoads` still checks every restore and hashes every
frame (the loopback tests' setting); it no longer hashes every save twice. A match plays with N
= 16 (`-sfve_verify_loads`, 0 for none): about two hashes per 16 rollbacks.

## 4. The numbers

`StateLayout::Costs` gains three counts the session keeps: saves that kept no copy, frames
hashed once final after their save, and saves replaced before final. The `costs:` line (every
600 updates, `SfvAdapter::LogCosts`) now reads

`costs: the last 600 updates: tick T us; replayed tick R us (n); save S us (n, k skipped), load
L us (n), hash H us (n, c once final, d not needed), the restore's callbacks C us (n)`

where save's n counts the copies made and k the saves that made none. Each session's end logs
`session costs: F frame(s) played, R replayed in N rollback(s); K save(s) kept no copy; C
frame(s) hashed once final, D save(s) replaced before final; V restore(s) re-hashed, M differed`.
Both match `costs: `, so `runroom.ps1` and `synctest.ps1` print them.

## Settings

| `RollbackSession::Config` | mod option (development builds) | default |
|---|---|---|
| `skipUnloadableSaves` | `-sfve_skip_saves=0` | on |
| `hashOnConfirm` | `-sfve_hash_on_confirm=0` | on |
| `verifyLoadsEvery` | `-sfve_verify_loads=N` | 16 (the mod), 0 (the session) |

A player build reads none of them (BuildKindTests) and plays with the defaults. `runroom.ps1
-plainSaves` passes both `=0` for the before run; `-verifyLoads N` sets N.

## What it does not touch

- **The synctest** (`SyncTester`, the mod's synctest mode, a replay's `-sfve_replay_check`) and
  **the lagged replay** keep their own saves and load them on purpose; they never go through
  `RollbackSession::Save`.
- **Watchers and replays** play confirmed frames with no GGPO saves (`HashWatched`,
  `HashLagged` hash at once, as before).
- **The start agreement** compares the frozen start state before the session exists.
- **The socket record-and-serve** (`desync.md`) keys on per-fighter hashes taken at the start of
  every update by the adapter, and its freshness is cleared by `AfterLoad`, which every real
  load still runs. Saves and their hashes are not involved.
- **Recording and the desync check**: the same checkpoint values, reported once final. The
  replay file and the match protocol are unchanged, and so are the layout id and the match
  build.
- **Held frames and the room's decisions on confirmed frames** change the game before the next
  save; a skipped save is one no load reaches, so nothing they wrote is lost.

## How many saves are skipped

Save F is made at the end of step F - 1 and is skipped when the other side's input for frame F
+ 1 is already here. That input was sent in the other side's step F + 1 - delay, delay - 2
steps before this side's step F - 1 on a shared clock. So with the default delay of 2 a save is
skipped only on the side that runs behind, by more than the link's one-way time; with delay 3,
on a link under about a frame (16 ms) one way, plus the other side's lead. Played
forward, the frames skipped are the ones whose inputs came in time: the saving is the copy each
of them costs, about 0.3 to 0.7 ms a frame (SFV_PORT.md's figure). In a rollback the inputs that
started it have just been received, and the confirmed frame "now" counts them: every replayed
frame they confirm past is re-saved with no copy, so a rollback that its inputs settle copies
only its last frames, still open to a guess. Hash on confirm saves less in a match (only
checkpoints are hashed there) but removes every replayed checkpoint's hash. The laptop runs
give the real counts.

## Tests (`sfve_tests`)

`tests/RollbackCostTests.cxx`, over `tests/LossyLink.hxx` (a relay between a `LoopbackPair`'s
two sessions: 2 to 16 ms on the way in order, 5% lost once both sessions run, both peers
stepping a frame every 4 ms with a delay of 3). Nothing is lost during GGPO's handshake: it
sends a lost handshake datagram again only after 0.5 s (first round) or 2 s (later rounds), past
the pair's one-second disconnect timeout. The laptop's first run lost one there (seed 31): one
peer ran, the other never synchronized, and the running one failed for the drop after a second
with nothing restored; the savings were not involved.

- a save no load can reach keeps no copy, and no load ever reaches one (the frames each
  session skipped, from `onSaveSkipped`, against every rollback's target, `RollbackTargets`;
  `skippedLoads` 0);
- hashes and checkpoints are the same with and without the cheaper rollbacks (the same match
  twice, every frame's hash on all four sessions equal to an offline run's; every checkpoint's
  regions equal; with the savings each checkpoint reported once, with the value the session
  without them reported last);
- with a match's settings (checkpoints only, one restore in four re-hashed): the re-hash count,
  no mismatch, every checkpoint reported once and equal to the offline run's, the kept
  checkpoint state hashing as its checkpoint;
- a fighter who leaves mid-match: GGPO's reload for the drop reaches a save that kept its copy,
  kept for watchers (plays on) and not (fails for the drop, not for a load);
- a save kept for a dump keeps its copy;
- the synctest restores every save it makes.

`LoopbackPair` gains a settings hook and a pump. Every existing loopback, watcher, replay and
start agreement test now runs with the savings on (the defaults). `BuildKindTests` covers the
three options.

## What the laptop run must check

Build the development build of this branch (GGPO's port version changes, so vcpkg rebuilds
GGPO), then:

1. Unit tests: `sfve_tests` (RollbackCostTests, RollbackSessionTests, WatcherTests,
   ReplayTests, StartAgreementTests, RematchDecisionTests, SyncTestTests) and
   `sfve_ember_tests` (BuildKindTests, SfvVerificationTests, DesyncReportTests).
2. Before and after, two copies, the same seeds; both must PASS with 0 differing checkpoints,
   and the `costs:` and `session costs:` lines compared (save count and time per update, the
   skipped and "once final" counts):
   ```
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 2 -plainSaves
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 2
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -delay 3 -games 2
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 0 -games 2
   powershell -File tools\runroom.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -lag 30 -games 2 -watchers 1
   ```
   No copy may log `whose save kept no copy` or `restored with a different hash`.
3. The synctest, unchanged (its `costs:` lines show `0 skipped`):
   ```
   powershell -File tools\synctest.ps1 -p1 RYU:0:1 -p2 KEN:0:2 -moves -practice -frames 7000 -check
   ```
4. A recorded battle plays back with its checkpoints:
   `python tools\runreplay.py --lag 30` (PASS, 0 differing).

## Not verified

- Nothing is built or run: the new GGPO patch was checked to apply on the port's patched tree
  (from the archive, all 18 patches, then this one) and nothing else.
- How many saves a real match skips at -lag 30 with delay 2 (see "How many"); the doc's
  estimate is from the rule, not a measurement.
- The loopback tests' timing on a loaded machine: the lossy tests check that some saves were
  skipped and some rollbacks made; a very slow runner could make either rare.
