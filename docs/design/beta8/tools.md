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

## Per load (as DESIGN.md)

`launch versus A B TRN 1 inf` -> wait `launch.loaded` and `booth.battle` changed (mismatch or
failure = failed load) -> optional walk-in -> `booth hold F` -> 12 presents -> camera peek
(`CameraUnit[0]+0x450`, same twice) -> grab A (depth auto) -> `booth scale 0.001 0.001` -> 12
presents -> grab B -> `booth leave` -> main menu. Every wait has a timeout; on a timeout the copy
tries `booth leave`, else it is killed and relaunched (only that copy); every 200 loads a copy is
relaunched. P1 and P2 come from one grab pair: the combined mask is split at the widest empty
column run between the fighters (a pair whose fighters touch fails); an odd last look is loaded
alone with P2 scaled away. The mask is the depth difference (changed, not near/far) when both
depth sidecars exist and show something, else the colour difference; specks are dropped, the mask
eroded 1 px, cropped to the alpha box, P2 flipped, fitted 512x768 with a 32 px margin.

Loadout pictures (phase 3): P1 = costume 0, first colour, V-Skill I / V-Trigger I; P2 = II / II
(P2 only for choices the fighter has). The other side is scaled away. Hold at `start` (150),
`booth play <side> MP+MK:4`, `booth run N`, grab; `booth run 150`; `HP+HK:4`, `booth run N`, grab.
N is in `tools/booth/loadout-frames.json` (default V-Skill 20, V-Trigger 45; per-fighter overrides in
`fighters`). A second grab with the fighters scaled away finds the fighter, and the 16:9 crop (height
85% of the frame, above the gauge line, centred on the fighter) is saved. `--burst` also writes the
frames N-2..N+2 as JPEGs to `<store>\burst\<CODE>` to choose N by eye.

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
