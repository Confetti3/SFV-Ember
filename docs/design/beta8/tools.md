# Photo booth capture tools (stream A-tools)

`tools/runbooth.py` drives several game copies through the mod's booth mode (`booth.md`,
`booth-protocol.md`) and writes cutout masters and loadout pictures into the render store;
`tools/boothpack.py` turns the masters into the pack (`pack.md`); `tools/boothsheet.py` makes
contact sheets. Development builds only; never on a player build.

## Setup

Python 3.10+, Pillow and numpy pinned in `tools/booth-requirements.txt` (Pillow 11.1.0, numpy
2.2.6: wheels exist for 3.10 to 3.13). Use a venv, not the global Python:

```
python -m venv D:\sfv-booth-venv
D:\sfv-booth-venv\Scripts\python -m pip install -r tools\booth-requirements.txt
D:\sfv-booth-venv\Scripts\python -I tools\boothpack.py --selftest
```

The selftest needs no game: synthetic grabs (with and without depth sidecars) -> masks -> P2 flip
-> pack -> validation against the path rules and manifest rules of `pack.md` (re-implemented in
Python), including negative cases (wrong case, stray file, changed hash, missing first colour).

Run with the Steam overlay off (a toast would land in the grabs) and the dev build staged
(`SFVE_VIA=launcher SFVE_PACKAGE_DIR=...` or the deployed proxy, as for the other runners).
Copies are launched with `-sfve_mode=booth -sfve_pads=0 -sfve_background=1`, windowed at
`SFVE_RES` (default here 1920x1080), through `sfvelib` (run registry, one run per process, so only
this run's copies are ever killed). The launch lock `D:\sfv-re\sfv-launch.lock` is taken first.

## Store

```
<store>\raw\<load>\a.bmp b.bmp (+ .depth/.json)     deleted once the masters exist (--keep-raw keeps; ~30 MB a load)
<store>\masters\<CODE>\costume-N\color-M.png|.json   512x768 RGBA, game's own numbers, frame/stage/mask method in the .json
<store>\masters\<CODE>\vs1.png vs2 vt1 vt2           loadout pictures, 16:9 crop, lossless (+ .json)
<store>\contact\  <store>\burst\  <store>\capture-log.jsonl
<store>\pack\assets\selection\...  <store>\pack\manifest.json
```

## Commands

Plan (no game): `python -I tools\runbooth.py --plan --phase all --copies 4 --secs-per-load 20`.
The full worklist is 3,258 loads (3,213 look loads of two looks, 45 loadout loads for 180 pictures); a load is one launch, so **3 hours needs about
18 loads/min over all copies**, i.e. 4 copies at roughly 13 s per load, or 6 at 20 s.

Pilot (20 looks over 5 fighters, 2 copies):

```
python -I tools\runbooth.py --phase 1 --fighters RYU,KEN,CMY,ZGF,Z21 --per-fighter 4 --copies 2 --keep-raw
python -I tools\boothsheet.py --fighters RYU,KEN,CMY,ZGF,Z21
python -I tools\runbooth.py --phase 3 --fighters RYU,KEN --burst --copies 1
```

Extra pilot shapes: `--looks RYU:0:1,RYU:0:2` (exact looks), `--sides 1`, `--mask colour`,
`--walk`, `--frame N`, `--target training`, `--hud-hide "SetVisiblePlayerInfo 0"` (sent after each
load; the HUD-hide search itself, `booth hudlog on` and trying methods by hand with `ctl`, belongs
in the pilot, not the main loop).

Full run, then pack:

```
python -I tools\runbooth.py --phase all --copies 4
python -I tools\runbooth.py --retry-failed --copies 2        # looks the log lists as failed
python -I tools\boothsheet.py
python -I tools\boothpack.py pack --except-failed [--mod-revision <hex>]
scripts\package.ps1 -RenderStore D:\sfv-ember-renders
```

Resumable: a look whose master exists is skipped (`--force` captures again). Ctrl-C stops after the
current step. Progress lines (per copy: loads, failures, loads/min, ETA) every 30 s.

## Time estimate method

Run the pilot, read each copy's `loads/min`, and compute `3258 / (copies x loads_per_min_per_copy)`
minutes. Measure again with 4 copies at once: the copies share the GPU, so per-copy speed drops;
raise `--copies` until the total stops growing. `--plan --secs-per-load S` prints the same sum.
Measured on the laptop (2026-11-12, fight frame 60): 4 copies 14-15 look loads/min together
(28-30 looks/min), so the 3,213 look loads take about 3.5-4 h; phase 1 (875 looks) about 30 min;
phase 3 about 20-25 min more.

## Per load (as DESIGN.md)

`booth hudhide on` once after each boot (the HUD stays hidden for every battle; `--show-hud` keeps
it). `launch versus A B TRN 1 inf` -> wait `launch.loaded` and `booth.battle` changed (mismatch or
failure = failed load; still loading after `--load-stuck` 25 s = stuck: the copy is relaunched and
the load tried once more) -> optional walk-in -> `booth hold F` (`--frame`, default 60) -> 12
presents -> camera peek (`CameraUnit[0]+0x450`, same twice) -> grab A (depth auto) -> `booth hide 1
1` (both fighters' actors hidden, cloth and shadow too) -> 12 presents -> grab B -> `booth leave` ->
main menu -> 4 s (`--menu-settle`: a launch of the same stage sooner never finishes loading). Every
wait has a timeout; on a timeout the copy
tries `booth leave`, else it is killed and relaunched (only that copy); every 200 loads a copy is
relaunched. P1 and P2 come from one grab pair: the combined mask is split at the widest empty
column run between the fighters (or at the emptiest central column when fewer than 4 % of the rows
cross it; more fails); an odd last look is loaded alone with P2 hidden. The mask is the depth
difference (changed, not near/far) when both depth sidecars exist, name the same texture and show
something, else the colour difference; specks are dropped, the mask
eroded 1 px, cropped to the alpha box, P2 flipped, fitted 512x768 with a 32 px margin.

Loadout pictures (phase 3): P1 = costume 0, first colour, V-Skill I / V-Trigger I; P2 = II / II
(P2 only for choices the fighter has), each side in a battle of its own with the other side's actor
hidden (a hidden side is never shown again: that would show a partner the game keeps hidden, R.
Mika's). Hold at `start` (150); per move: the plate (the fighter scaled to 0.001, grabbed, scaled
back), 20 frames for the cloth, the standing grab, `booth play <side> MP+MK:4` (or `HP+HK:4`), then a
burst: `booth run 4` and a grab, up to 120 frames after the press (`auto` in
`tools/booth/loadout-frames.json`); `booth run 150` before the side's next move. The picture is
chosen from the burst by `tools/boothburst.py` (no one picks frames): each grab's camera pan against
the standing grab is lined up (phase correlation); a grab whose background still differs (zoom, a
dimmed or covered stage) or whose brightness jumps (a flash) is not used; the score is the share of
the fighter's widened region that changed; the first grab whose stable score (the least of it and
its two neighbours) reaches 80 % of the best is the picture. Nothing usable or nothing moving: the
grab nearest `default` (V-Skill 20, V-Trigger 45 frames after the press). `fighters` in the JSON
holds exact frames as overrides only, for a move the chooser gets wrong. The 16:9 crop is the window
around the fighter, its new pose and its effects (where the chosen grab differs from the standing
grab, and where the fighter stood), 12 % margin, at least 60 % of the frame high. The master's
sidecar keeps the frame, how it was chosen (`auto`, `override`, `default`) and every grab's score.
`--burst` writes every grab, scored (`<move>-t<frames>-s<score x 1000>[-chosen|-unused].jpg`), to
`<store>\burst\<CODE>` for review. `boothsheet.py` writes `contact\loadout.png`: a row per fighter
with its four pictures, each labelled with how its frame was chosen.

Loadout time: per fighter two battles (about 15 s each with the leave) and four moves of about 15 s
(plate and standing grab 3 s, 30 burst grabs at about 0.35 s, the 150-frame gap 3 s), about 90 s a
fighter on one copy; 45 fighters about 70 copy-minutes, about 20-25 minutes on 4 copies with the
copies sharing the GPU. While a move's grabs wait to be chosen they take about 270 MB of raw BMPs
(deleted once its picture is saved); `--keep-raw` keeps them all, about 48 GB for the 180 moves.

## What to check on the contact sheet

Pilot sheet, then the full one: cutouts whole (no missing arm, hair or weapon: holes mean the depth
or colour mask missed it, try `--mask colour`); no stage background, shadow or HUD left on the edge;
no second fighter or effect attached; the pose the same across colours of a costume; P2 pictures face
right like P1; sizes similar between colours; a crossed-out cell is a look with no master (see
`capture-log.jsonl`). The overview sheet shows the first colour of every costume: scan for wrong or
swapped fighters. Loadout pictures: the effect is visible, the fighter centred, no HUD in the crop.

## Protocol assumptions the mod must meet

Pipe replies and `status` JSON as `booth.md`: `status.scene` equals the main-menu vtable rva
(`sfvelib.MAIN_MENU_SCENE`); `status.launch` has `loaded`, `mismatch`, `failure`, `state`; `status.booth`
has `battle`, `fightFrame`, `held`, `presents`, `pending`, `lastGrab {path, ok, error, depth}`;
`lastGrab.path` is compared case-insensitively with forward slashes; `pending` false means the BMP and
depth files are written; `peek CameraUnit[0]+0x450 0 16` answers `ok <words>` (if it errors the camera
check is skipped with one warning). Not verified: any of this against a running game; the loadout
start frame (150), the 16:9 crop fraction and the camera check's meaning need the pilot.
