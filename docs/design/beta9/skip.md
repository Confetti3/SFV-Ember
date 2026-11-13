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
