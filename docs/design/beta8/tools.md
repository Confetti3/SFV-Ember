# Photo booth capture tools (stream A-tools)

`tools/runbooth.py` drives several game copies through the mod's booth mode (`booth.md`,
`booth-protocol.md`) and writes cutout masters and loadout pictures into the render store;
`tools/boothpack.py` turns the masters into the pack (`pack.md`); `tools/boothsheet.py` makes
contact sheets. Development builds only; never on a player build.

Pack formats: costume cutouts are 256x384 palette PNGs (SF4's importer); loadout pictures are
`vs<k>.png` / `vt<k>.png`, 256x144 RGB PNGs made as SF4 makes its `ultra-N.png` (Lanczos
`ImageOps.fit` from the lossless 16:9 master, saved as PNG, no palette, `optimize=True`).

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
presents -> camera peek (`CameraUnit[0]+0x450`, same twice) -> wait until SFV reports nothing
loading for 60 battle frames (`booth.loading`, `loadedFrames`) -> grab A (depth auto) -> 20
presents -> grab A again (the held frame must be the same picture within 1 %, else A is taken again,
up to 5 times) -> `booth hide 1 1` (both fighters' actors hidden, cloth and shadow too) -> 12
presents -> grab B -> checks (boothpack `check_pair`: outside the fighters, i.e. the depth
difference widened, grab A must be the plate within 1 %, a pixel only darker in A being the
fighters' shadow, and one brighter in A within 80 pixels of them their glow (Kage's aura); the plate must be the stage as `<store>\reference\plate-<stage>-<frame>.png`
shows it within 15 %, that file written from the first plate that passes) -> `booth leave` -> main
menu -> 4 s (`--menu-settle`: a launch of the same stage sooner never finishes loading). A load
whose grabs fail a check is loaded again once; then its looks fail with the reason (also with
`--force` over an old master: the old master stays, the look is logged as failed). Every grab is
taken with `ui=off` (the colour from before the frame's first interface draw: no HUD gauges, no
menu left over, no overlay toast). A side drawn without its textures (boothpack `untextured`:
nearly all light cyan-grey and no warm pixel) fails as a look: on the laptop some costumes render
so every time, their texture data missing there. Each master's sidecar keeps `checks` (`outside`,
`plate`, `stable`, `warm`). Every wait has a timeout; on a timeout the copy
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
chosen from the burst by `tools/boothburst.py` (no one picks frames). SFV's camera follows the
fighter, so a burst grab sees the stage from elsewhere and the floor moves more than the far wall: no
2D shift lines the plate up (the first full run, 2026-11-12, framed most pictures far too wide that
way). The plate is grabbed with its depth, and the plate, the standing grab and every burst grab
with the camera they were drawn through (`peek CameraUnit[0]+0x100.presentation 112 40`: the point
it looks at, its eye, the horizontal field of view; `tools/boothcamera.py`); the plate is put back
in the world by its depth (reversed Z, near plane 10) and seen through each grab's camera, so it
lines up with the grab everywhere it reaches. What then differs is the subject: the fighter, its
effects and its shadow (specks and thin lines left out, and small pieces far from the rest, such as
the cloth a scaled-away fighter leaves on the floor). A grab is unusable when the stage is dimmed or
flashed (its brightness 12 % off the plate's as a whole, as a V-Trigger's activation dims it, or 10
levels off around the subject), when more than 5 % of the stage around the subject still differs,
or when the plate reaches less than 60 % of the picture. The score is the subject's size against the
standing fighter's, so a place scores nothing and a spreading pose, an aura or a projectile scores;
the picture is the usable grab with the largest subject (the first within 95 % of it), never before
12 frames after the press. Nothing usable: the grab nearest `default` (V-Skill 20, V-Trigger 45
frames after the press). `fighters` in the JSON holds exact frames as overrides only, for a move the
chooser gets wrong. The picture is framed as SF4 Ember's Ultra pictures: a 16:9 window centred on
the subject (the outermost 0.5 % of its pixels left out on each side), the subject filling 85 % of
its height, kept inside the frame. The master's
sidecar keeps the frame, how it was chosen (`auto`, `override`, `default`) and every grab's score.
`--burst` writes every grab, scored (`<move>-t<frames>-s<score x 1000>[-chosen|-unused].jpg`), to
`<store>\burst\<CODE>` for review. `boothsheet.py` writes `contact\loadout.png`: a row per fighter
with its four pictures, each labelled with how its frame was chosen.

Loadout time: per fighter two battles (about 15 s each with the leave) and four moves of about 15 s
(plate and standing grab 3 s, 30 burst grabs at about 0.35 s, the 150-frame gap 3 s), about 90 s a
fighter on one copy; 45 fighters about 70 copy-minutes, about 20-25 minutes on 4 copies with the
copies sharing the GPU. While a move's grabs wait to be chosen they take about 270 MB of raw BMPs
(deleted once its picture is saved); `--keep-raw` keeps them all, about 48 GB for the 180 moves.

## Checking and re-capturing masters

`python -I tools\boothpack.py check [--store S] [--out F] [--unchecked] [--quarantine-untextured]`
lists the masters that look wrong by their pixels (untextured: nearly all light cyan-grey and no
warm pixel; three or more rows with a straight edge across 18 % of the fighter's width: a menu
over it), with `--unchecked` also those captured before the grab checks, and writes them one
`CODE:costume:colour` a line to F (default `<store>\recheck.txt`). `--quarantine-untextured`
renames untextured masters `*.untextured.png` and logs them as failed looks (so `pack
--except-failed` leaves them out) and does not list them (capturing them again on the same copy
draws them the same). Capture the listed looks again with
`python -I tools\runbooth.py --looks-file F --force --copies 4`.

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
