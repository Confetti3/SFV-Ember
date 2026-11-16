# beta.10: seeks in a replay's playback (2026-11-15)

Step back, jump 5 seconds either way, restart and an A-B loop, over the playback of a recorded
battle (EMBER_LIBRARY.md section 26). Written against the code, not yet built or run: the "To verify"
list below is the final round's.

## 1. What the player gets

| Control | Keyboard | Pad |
|---|---|---|
| Step back one frame (pauses first; held, it repeats) | `,` | X (Square) |
| Jump back / forward 5 seconds (keeps playing, or stays paused) | Left / Right | D-pad Left / Right |
| Restart from the first fight frame | Home | D-pad Down |
| Loop: set A here, then set B here and loop A..B, then off | L | D-pad Up |

The strip (`DrawReplayStrip`) has a thin timeline (position / total, the earliest reachable point,
the loop's marks) and a chip per control. The pipe: `replay stepback`, `replay seek <frame>`,
`replay jump <+/-seconds>`, `replay restart`, `replay loop`; the `replays` status reports `frame`,
`total`, `floor`, `last`, `loop`, `snapshots`, `seeks`, `seeking`, `furthest`.

## 2. Design

1. **Model** (library, `common/ReplayTransport.hxx`): five more `Command`s, `Seek` (the target of a
   seek on its way, the loop's marks, step back's pause) and `SeekView` in `View`. Game-neutral; a
   game offers it by setting `SeekView::offered` and `PadControls::Offer(true)`; SF4 offers none
   and ignores the commands.
2. **A position is the frames played** (the session's `Frame()`): "frame F" is the state at the
   start of frame F. A landing is a frame that reads inputs (`RecordedFrame::Fight`) of the game
   the playback is in: the frames between the held WIN frames before and after it
   (`core/PlaybackSpan.hxx`). A seek never leaves its game (a rematch in place starts another).
3. **The ring** (`core/PlaybackSnapshots.hxx`): `StateLayout::SaveTo` at the first fight frame of
   a game, then every 120 frames; at most 48; when full every other one goes and the spacing
   doubles; the first of a game stays. A new game clears it. Owned by `ReplaySeeker`
   (`core/ReplaySeeker.hxx`), which the netplay mode calls before each frame it plays.
4. **Restore**: `RollbackSession::RestoreReplay` (declared in the session's header, defined with the
   seeker so `RollbackSession.cxx` does not grow): `LoadFrom`, `GameAdapter::AfterLoad`, `frame_` set.
   The nearest state *strictly before* the target is used, so at least one update plays after the
   restore and puts the models where the state says (`SfvAdapter::AfterLoad`, `SocketReads`); the
   earliest landing is therefore the first fight frame after the first state.
5. **Flow**: `NetplayMode::PlayReplay` (a replay's engine frame) builds a `SeekGround` (frame, span,
   earliest state, whether a restore is possible), asks `adapter::PlaybackTurnFor` (`ember/
   SfvPlayback.cxx`: the commands since the last frame, the loop, the seek's progress; the transport's
   `Hold()` is not called while a seek runs) and gets a `PlaybackTurn`: frames to play (0 or 1), or a
   goal with a state to put back first. A seek plays up to 30 updates an engine frame, each through
   `WatchFrame` (the watcher's decisions, as the catch-up path plays several), so it presents what
   the engine already presents. A forward seek is the same without the restore.
6. **Bookkeeping** (`ember/PlaybackBook.hxx`): the strip's round and clock are made again from the
   recording's fight frames after a seek (`NoteSeek`), not counted; a checkpoint is compared by its
   frame id, a checkpoint reached again is compared again and counted once, one that ever differed
   stays a difference (logged the first time, up to eight, with "after N seek(s)"); a playback is
   whole when it was to the last recorded frame (`Furthest`), whatever number of frames it played.
   `EndPlayback`'s line keeps `N of M frame(s) played, K checkpoint(s) compared with the recording,
   D differing` for the tools, and adds the frames played in all and the seeks.
7. **Refused** (counted in `View::refused`): a seek while the WIN is held, while the next frame is
   held, before the first state of the game is held, in a lagged or rollback-checked playback (a test's
   diagnostics), and a loop mark off a fight frame. A seek or a loop that loses its ground (the
   game's end) is dropped.
8. **Files**: `core/PlaybackSpan.hxx`, `PlaybackSnapshots`, `ReplaySeeker` (core, tested on
   MockFighter), `ember/PlaybackBook`, `SfvPlayback` (the playback moved out of `SfvReplays.cxx`,
   which went from 829 to about 650 lines), `NetplayMode` (941 lines, +23).

## 3. What a seek does not restore

A snapshot is the rollback layout, which leaves out what the engine draws and plays (presentation
state is restored but not hashed, `pose-hold.md`). After a restore the first update places the models
(hence "strictly before"), and a socket read on a pose never drawn can miss (`socket-anchors.md`,
the four reads that feed fight state): a watcher's catch-up plays several frames an engine frame the
same way, and a seek adds no hook. The sounds and effects a seek replays play (or not) as a
rollback's replay does (`AfterLoad`'s sound callbacks drop what was played after the state).

## 4. To verify (the final round)

`python tools/runreplay.py --seeks [--rematches 1]` is the proof: in one playback it steps back,
jumps back and forward, restarts once, loops A..B twice, plays to the end, and requires every
checkpoint to equal the recording's and the playback to be whole. Also by eye: the strip's chips and
timeline at 720p, 1080p and 3440x1440; the first frame after a restart while paused (the models);
the D-pad and X on an Xbox, a PlayStation and a Switch pad; a seek during the last round's scenes.
