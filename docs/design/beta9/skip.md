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
