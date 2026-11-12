# Photo booth pipe protocol

The contract between the mod's booth mode (`src/mod/BoothMode.*`, stream A-mod) and the
capture driver (`tools/runbooth.py`, stream A-tools). Frozen at step 0; change it only in
this file and in both streams together.

## Availability

- Development builds only, started with the game option `-sfve_mode=booth`. A player build
  never makes the mode (`ModConfig` reads the option only in a development build) and its
  pipe answers `ping` and `status` only, so none of the commands below exist there.
- The pipe is the usual one, `\\.\pipe\sfve-<instance>`: one request line in, one reply line
  out. Replies are `ok ...` or `error <reason>`. Handlers run on the pipe thread and post
  anything that touches the game to the game thread, so a reply says the command was
  accepted (or why not), not that it has finished; the driver waits on the `booth` status
  field.
- The booth drives an offline Versus or Training battle started with the existing
  `launch versus <A> <B> <stage> 1 inf` (or `training`) command. Tiny scaling is off for
  pipe launches, and the overlay's interface is not shown, so the back buffer is clean.

## What the mode does

`BoothMode` steps the battle itself, one `SfvAdapter::Advance` per frame:

- the intro ends with the skip bit (as an offline battle does);
- both players' gauges are filled once the layout is bound (`SetPracticeGauges`);
- fight frames (flow state Fight) are counted from 0;
- while held, nothing advances but the game keeps presenting, so grabs of one frozen frame
  can be taken as often as wanted.

Inputs in a held battle are ignored; `play` and `run` are the only ways the battle moves
once the mode is idle.

## Commands

All arguments are space separated; `<x>` is required, `[x]` optional. Numbers are decimal.

| Command | Reply | Meaning |
|---|---|---|
| `booth hold <fightFrame>` | `ok` | Advance until the fight frame counter reaches `fightFrame`, then hold there. A frame already past is an error (`error frame <n> is past`); the battle must be restarted. 0 holds at the first fight frame. |
| `booth run <n>` | `ok` | Advance `n` more fight frames (1 or more), then hold again. |
| `booth play <side> <steps>` | `ok` | Side 0 (P1) or 1 (P2) plays scripted buttons while the battle advances, one step after another, then goes neutral. The other side plays neutral. The battle then holds when the steps are done. See Steps. |
| `booth scale <p1> <p2>` | `ok` | Set each side's fighter mesh to `p1` / `p2` times its own size at once (`ApplyFighterScaleNow`), on the game thread, so it shows in the next present of the frozen frame. 1 is normal, 0.001 takes a fighter out of a plate. Factors must be positive and finite. `error scale: <reason>` if a side's part is not known yet. |
| `booth grab <path> [after=<presents>] [depth=auto\|<n>\|off]` | `ok` | Queue one frame grab (`overlay/FrameGrab.hxx`): the back buffer, at the present `after` presents after the command (default 0, the next), to `<path>` as a 32-bit BMP, and unless `depth=off` the chosen depth target to `<path>.depth` with `<path>.json` beside it. `depth=auto` (default) picks the target with the most draws; `<n>` the n-th by first use. `error grab pending` while one is queued. |
| `booth depth` | `ok <json>` | The depth targets counted in the frame before the last present: `{"targets":[{"index":0,"format":"D32_FLOAT","width":1920,"height":1080,"draws":123}]}`. |
| `booth hud <method> <number> [text]` | `ok` / `error hud: <reason>` | Call the HUD movie's `method` with `number` and optional `text` (the rest of the line), on the game thread: the probe for the call that hides the HUD. `error hud: no movie` before the HUD has been seen. |
| `booth hudlog on\|off` | `ok` | Log every HUD invoke (method, number, text) to the game's log. |
| `booth leave` | `ok` | `mod::LeaveBattle()`: leave the battle for the menu. The driver then waits for the main menu. |

Any other `booth` word or a malformed argument is `error <reason>`.

### Steps

`<steps>` is a comma-separated list, no spaces, each step its buttons joined by `+`, then
`:` and a number of fight frames:

```
MP+MK:4,RIGHT:40
```

Buttons: `UP DOWN LEFT RIGHT LP MP HP LK MK HK`, or `NONE` for nothing pressed. The frame
count is 1 or more and defaults to 1. Case does not matter. `RIGHT` and `LEFT` are screen
directions, not forward and back. V-Skill is `MP+MK`, V-Trigger `HP+HK`, throw `LP+LK`
(`mod/GameInput.hxx`). Scripted fighters never jump: a direction is a grounded one.

## Status field

The pipe's `status` carries a `booth` field (its `Describe()`): `null` before a battle,
then

```json
{
  "battle": 3,
  "flow": 6,
  "fightFrame": 600,
  "held": true,
  "presents": 18452,
  "pending": false,
  "lastGrab": {"path": "D:/.../a.bmp", "ok": true, "error": "", "present": 18440}
}
```

- `battle`: the battle's number (`SfvAdapter::Battle`), 0 before any; it changes when a new
  battle binds, which tells the driver a `launch` has loaded.
- `flow`: the round flow's state number (`sfv::flow`; 6 is the fight).
- `fightFrame`: fight frames advanced in this battle; `-1` before the fight starts.
- `held`: the mode is holding (nothing advances).
- `presents`: presents seen since the present tap was installed.
- `pending`: a grab is queued and not yet taken.
- `lastGrab`: the last grab, or `null` before the first. `ok` false carries `error`.

The existing `launch` status field also gains `looks` (the loaded fighters' costumes and
colours as the game has them), `mismatch` (true when they are not the ones asked for, which
makes the driver skip the load) and `failure` (the reason a launch failed, or empty).

## Per-load sequence (the driver)

1. `launch versus A B <stage> 1 inf`; wait for `launch.loaded`; `mismatch` means failed,
   skip.
2. Optional walk-in; `booth hold <F>`; wait for `held` and 12 presents; check the camera
   with `peek` (`CameraUnit[0]+0x450`).
3. `booth grab A.bmp` (fighters normal); wait for `lastGrab`.
4. `booth scale 0.001 0.001`; wait 12 presents; `booth grab B.bmp`; wait.
5. `booth leave`; wait for the main menu.

The cutout is grab A against grab B of the same frozen frame: the mask comes from the depth
difference (primary) or the colour difference (fallback).
