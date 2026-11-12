# beta.8: the winner scenes skip without a press (2026-11-12)

**What players saw.** The press-to-skip of `skip.md` ends the scenes after the deciding KO, but
only when someone presses. A player who does not know about it waits the whole presentation
(about 26 s) before Ember's rematch prompt opens over SFV's results. A review of another SFV
netplay mod's design recommended opening the rematch at the decision without a press.

**Decided by the owner.** Include it, as a room rule "Skip winner scenes", on by default, in
private and public rooms. The prompt still opens on SFV's results screen (never earlier), a press
still works, and the rule is the host's to change.

## What it does

With the rule on, each fighter copy asks for the skip on its own from the first frame past the
existing guard (`intro_skip::kScenesGuardFrames`, 60 session frames after it saw the match
decided), through the same path as a press: `Request::Next` sets the request, the skip bit rides
the exchanged input, and the adapter ends the scenes on confirmed skip frames (`skip.md`). So:

- Nothing is asked inside the guard, press or not. A press cannot make the ask earlier than the
  rule does; with the rule on it is redundant.
- The bit is read on skip frames (one frame in 15), so the first skip comes within 15 frames of
  the guard's end, then the same steps as a press: Finish to RoundOver to the winner's demo, then
  the demo's skip and the results' pages turning on the request (`PadsHear`). Expect the scenes
  to take about what `skip.md`'s `-skipAfter 90` run took, or less.
- The request clears when the scenes end (a rematch in place), then starts over for the next
  game's scenes: the guard applies again.
- Rule off: today's behaviour exactly, a new press by either player after the guard.
- The intro is untouched: the rule is about the winner scenes, the intro still needs a press.
- Watchers and replay playback never ask. They follow the fighters' confirmed skip frames, as for a
  press, so what they see and what a replay records is unchanged in kind (a replay of a rule-on
  game holds the skip bits like any other).

Why this is as safe as the press. It is the same request from a different trigger. The trigger
is a function of this copy's own frame count and the decided flag, which is rollback state, and
the request becomes part of the simulation only when the skip frame carrying it is confirmed
(`ReadsConfirmed`). If the two copies disagreed about the rule (a snapshot not yet equal), one
asks and the other does not: the confirmed bit is either player's, so both still skip on the same
frame. Nothing about the rule is hashed.

## The rule across the stack

Library (`b8/lib-autoskip`, modelled on Tiny fighters allowed, differing where noted):

- `room::Snapshot::skipScenes`, default **true**. JSON `skip_scenes`, written only when **false**,
  so a state without the key (an older copy, a saved checkpoint, every public room) reads as on.
  A non-boolean value is refused.
- `ActionKind::SetSkipScenes`, appended last (`LastActionKind` follows it), with
  `Action::skipScenes` (default true, written only when false). Host only, fenced on the room
  revision, gates no seat, moves nobody. **A server-owned (public) room refuses it**
  (`Unauthorized`): a public room always has the rule on. Unlike Tiny, the rule applies in public
  rooms; only changing it is a private room's.
- `RoomAuthority`'s new last constructor argument (default true),
  `SessionServer::EnableCustomRooms`, `RuntimeCommand::skipScenes`, `NetplayRuntime::skipScenes`: a
  replacement room keeps what its host chose. Create has no row: a room always opens with the rule
  on, and its host changes it in Room settings (`room-skip-scenes`, row "Skip winner scenes"),
  where every member sees it and only a private room's host can change it. The public room host
  binary builds its authority with the defaults, so its rooms are on and no roomhost, bridge or
  `server/` change is needed.
- `game::Capabilities::skipScenes` (default false; SFV sets it): the row is shown only where it
  is on (`ShellView::offersSkipScenes`), so SF4 shows nothing, reads nothing and its goldens are
  unchanged.
- Strings `room.skip_scenes` and `room.skip_scenes.detail` in all 14 catalogs (the localization
  test wants every msgid in every language). The new CJK text was checked against the embedded
  subset fonts' character maps; the subsets did not need regenerating.
- Test `RoomSkipScenes` (`src/tests/room_skip_scenes_test.cxx`).

SFV (`b8/autoskip`):

- `SfvCapabilities` sets `caps.skipScenes`.
- `RoomMatchNow` reads `snapshot.skipScenes` into `RoomMatch::skipScenes` (any room's, public
  included); `LaunchPicks` puts it in `MatchPicks::skipScenes`; `VersusLaunch` holds it until the
  battle binds (`TakeLinkedBattle`'s second out-parameter); `RoomBattle::SkipScenes()` is true only
  for a fighter's copy of a room battle (not a watcher, not a replay); `NetplayMode` gives it to
  `intro_skip::Request::SetAutoScenes` each frame.
- The rule is read **when the battle launches**. A host who changes it mid-game changes the next
  game's launch, not the one in play; a rematch in place keeps the game's value. Two fighters'
  copies launch from snapshots of the same room, so they agree except in the instant around a
  change, where the difference is harmless (above).
- The pipe's `room` command takes a sixth argument, skip winner scenes 0|1 (default 1), and
  `tools\runroom.ps1 -autoSkip` passes 1; without it runroom opens its rooms with the rule off so
  its older runs keep their meaning (no press, no skip).

## Wire and room host

No wire format changes: the rule is a room snapshot field and a room action, both in the room
protocol's JSON, and the skip bit it causes already travels in the exchanged input (`kSkipBit`,
section 52). A copy that has not heard of the field reads it as on; an older copy that receives
`skip_scenes: false` ignores it. The room host relays the snapshot and enforces the action as for
the other rules, so it needs no change: its public rooms keep the default, and a private room's
authority runs on the host's PC from the library. `kMatchBuild` stays 0.6: nothing the rule
touches is simulated before the confirmed skip frame, and beta.8 has not shipped.

## Tests

- `IntroSkipTests`: with the rule on, nothing asked on the deciding frame or anywhere inside the
  guard, the request up on the first frame past it with no press, held through the scenes, cleared
  by a rematch in place and starting over with its own guard; a mash inside the guard asks nothing;
  the intro still needs a press; with the rule off no ask without a press.
- `SfvCatalogTests`: `RoomMatch::skipScenes` (true by default) reaches `MatchPicks`; a launch of
  its own does not carry it. `EmberAdapterTests`: the capability.
- Library `RoomSkipScenes`: default on, host sets and unsets, a member is refused, the stale
  revision, a public room keeps it, no seat gated, the key written only when off and a bad value
  refused, a checkpoint keeps it, the action's keys, the kind order. The localization and
  source-coverage tests cover the strings and the `loc::T` literals.

Not built or run here: no builds on this PC.

## What a laptop room run must check

1. Unchanged baseline: `tools\runroom.ps1 -games 1 -lag 30` (rule off in runroom): scenes whole
   (about 1545 frames), no skip applied.
2. `-autoSkip -games 2 -lag 30`: no press, no `-skipAfter`. Both copies log `this copy asks to skip
   the scenes from frame F, 60 frame(s) into them (the room's Skip winner scenes rule ...)` and the
   same `scenes skipped at frame` lines and result frame; scenes under 700 frames; 0 differing
   checkpoints; the prompt opens on SFV's results (`-shots`: the after-skip and before-result shots
   as in `skip.md`). Look at every game's winner demo and the results: skipping is now the default
   path, so a bad pose or a cut presentation shows in every game.
3. `-autoSkip -watchers 1`: the watcher logs the same skip frames and result frame.
4. `-autoSkip -rematches 1 -padAnswers -lag 30`: the skip, the rematch in place and a second WIN:
   the second game asks again only after its own guard (no `asks to skip` line before its decision
   plus 60).
5. `-autoSkip -skipAfter 20` and `-skipAfter 90`: the press changes nothing (the ask is at the
   guard's end either way).
6. Rule off in a room (Room settings row, or `room ... 0` on the pipe): a plain run plays the
   scenes whole again, and a press still skips. The row reads Off for the guest, Left/Right changes
   it only for the host, and a public room's row is On and cannot be changed.
7. SF4 Ember (the library's own x86 build): no "Skip winner scenes" row anywhere, goldens
   unchanged.
8. A replay of an auto-skipped game plays back to the same result frame.
