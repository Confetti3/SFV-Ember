"""Grab checks (runbooth.py, before a master is written) and the check of masters already written.

The first-colour capture of 2026-11-12 had SFV's main menu drawn over the battle in about a third
of the looks and some fighters drawn before their costume had loaded (grey, or the default
texture). The mod now grabs before the interface (grab ui=off) and reports SFV's loading; these
checks catch what is left: a screen that changed between two grabs of the frozen frame."""

from __future__ import annotations

import collections
import json
import os
import sys
import threading
import time
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageFilter
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .grabs import DEPTH_DIFF_EPS, depth_target_id, read_depth
from .imageops import CHECK_DOWN, differs, morph, shrunk
from .space import master_path

# The reference plate of a stage is written by whichever copy gets there first; the copies are
# threads of one runbooth process, so one lock makes the first writer the only one.
_REFERENCE_LOCK = threading.Lock()


STABLE_MAX = 0.01          # two grabs of the held frame may differ on at most this share of the frame


OUTSIDE_MAX = 0.01         # grab A and the plate may differ outside the fighters on at most this share


GLOW_HALO = 41             # the glow zone: the fighters widened by 20 shrunk pixels (80 in the frame)


PLATE_MAX = 0.15           # the plate may differ from the stage's reference plate on at most this share


def warm_share(rgb: np.ndarray, mask: np.ndarray) -> float:
    """The share of the masked pixels that are warm (red over blue, red at least green): skin, hair,
    red and yellow cloth. A fighter drawn before its costume's colours loaded is a cold grey all
    over (measured on the 2026-11-12 capture: 0.00-0.01 for those, 0.4-0.9 for loaded ones)."""
    if not mask.any():
        return 0.0
    px = rgb[mask].astype(np.int32)
    return float(((px[:, 0] > px[:, 2] + 20) & (px[:, 0] >= px[:, 1])).mean())


def untextured(rgb: np.ndarray, mask: np.ndarray) -> bool:
    """The fighter in `mask` is drawn without its textures: nearly all of it the light, unsaturated
    cyan-grey of the bare model and no warm pixel at all (skin and hair are warm in every costume).
    Measured on the 2026-11-12 capture (875 masters): the bare models 0.47-0.84 grey and 0.00
    warm; every textured master with almost no warm pixel 0.16 grey at most. The bare models are
    the 45 costumes the game lists with no name (the raw text id ID_SYS_BSet_0224): slots it never
    shipped assets for, the same on every install.

    A textured costume in pale, cold colours with dark or blue-grey skin passes the grey and warm
    tests too (the 2026-11-17 every-colour capture: JRI 9/8, Z27 0/105, Z28 5/8, Z30 6/9), so a
    side that also has dark pixels or fine detail is textured: the bare model is light all over
    (dark 0.004-0.043) and smooth (detail 4.6-9.7), those four dark 0.05-0.27 and detail 10.1-12.4."""
    if not mask.any():
        return False
    px = rgb[mask].astype(np.int32)
    mx, mn = px.max(axis=1), px.min(axis=1)
    sat = (mx - mn) / np.maximum(mx, 1)
    tint = float(((px[:, 2] >= px[:, 0]) & (px[:, 1] >= px[:, 0] - 4) & (sat < 0.35) & (mx > 110)).mean())
    if tint < UNTEXTURED_TINT or warm_share(rgb, mask) > UNTEXTURED_WARM:
        return False
    inner = morph(mask, "erode", 5)  # away from the edge, where the stage blends in
    if not inner.any():
        return True
    if float((rgb[inner].max(axis=1) < 60).mean()) > UNTEXTURED_DARK:
        return False
    lum = rgb.astype(np.float32).mean(axis=2)
    blurred = np.asarray(Image.fromarray(lum.astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), dtype=np.float32)
    return float(np.abs(lum - blurred)[inner].mean()) <= UNTEXTURED_DETAIL


def write_reference_once(reference: Path, plate: np.ndarray) -> bool:
    """Writes `plate` (a shrunk picture) as the stage's reference plate unless one exists; True when
    this call wrote it. Under a lock, so two copies that finish their first pair of a stage together
    cannot both write: the second compares with the first's plate. The file appears whole or not at
    all (written beside it, then moved over it), so a copy reading it never sees half a PNG."""
    with _REFERENCE_LOCK:
        if reference.is_file():
            return False
        reference.parent.mkdir(parents=True, exist_ok=True)
        temp = reference.with_name(f"{reference.stem}.{os.getpid()}.tmp{reference.suffix}")
        Image.fromarray(np.clip(plate, 0, 255).astype(np.uint8)).save(temp, format="PNG")
        os.replace(temp, reference)
        return True


def check_pair(a_path: Path | str, b_path: Path | str, reference: Path | None,
               again: Path | str | None = None) -> tuple[dict, str | None]:
    """Grab A (fighters) against grab B (the plate) of one held frame, before masters are made.
    Outside the fighters (the depth difference, widened) A must be B, and A grabbed `again` a little
    later must be A (inside, a living costume may move); B must be the stage as the reference plate
    shows it (`reference`, a shrunk PNG; written from this B when it does not exist yet and the
    pair passed). Returns the measures and the reason the pair is refused, or None."""
    a, b = shrunk(a_path), shrunk(b_path)
    a2 = shrunk(again) if again is not None else None
    checks: dict = {}
    if a.shape != b.shape:
        return checks, "grab A and the plate differ in size"
    da, db = read_depth(a_path), read_depth(b_path)
    same_texture = depth_target_id(a_path) == depth_target_id(b_path)
    if da is not None and db is not None and da.shape == db.shape and same_texture:
        h, w = a.shape[:2]
        full = np.abs(da - db) > DEPTH_DIFF_EPS
        full = full[: h * CHECK_DOWN, : w * CHECK_DOWN]
        fighters = full.reshape(h, CHECK_DOWN, w, CHECK_DOWN).any(axis=(1, 3))
        fighters = morph(fighters, "dilate", 7)
        outside = ~fighters
        # The fighters' shadows (and the scene's ambient occlusion around them) darken the floor in
        # A without changing its depth: a pixel only darker in A than in the plate is not a change.
        shadow = (a <= b + 4).all(axis=-1)
        # A fighter's glow (Kage's aura, fire) lights the stage around it without a depth of its
        # own: near the fighters a pixel only brighter in A than in the plate is not a change. A
        # menu or a toast is found by the rest of it, far from the fighters.
        near = morph(fighters, "dilate", GLOW_HALO)
        glow = near & (a.mean(axis=-1) > b.mean(axis=-1))
        if a2 is not None and a2.shape == a.shape:
            moving = differs(a, a2)
            checks["stable"] = round(float(moving[~near].mean()) if (~near).any() else 0.0, 5)
            checks["stableInside"] = round(float(moving[near].mean()) if near.any() else 0.0, 5)
            if checks["stable"] > STABLE_MAX:
                return checks, (f"the held frame kept changing outside the fighters ({checks['stable']:.1%} of the frame "
                                "between two grabs)")
            a2 = None
        changed = differs(a, b) & ~shadow & ~glow
        checks["outside"] = round(float(changed[outside].mean()) if outside.any() else 0.0, 5)
        checks["warm"] = round(warm_share(a, fighters & differs(a, b)), 4)
        if checks["outside"] > OUTSIDE_MAX:
            return checks, (f"grab A and the plate differ outside the fighters on {checks['outside']:.1%} of the frame "
                            "(the screen changed between them)")
    if a2 is not None and a2.shape == a.shape:  # no depth to tell the fighters apart: the whole frame
        checks["stable"] = round(float(differs(a, a2).mean()), 5)
        if checks["stable"] > STABLE_MAX:
            return checks, f"the held frame kept changing ({checks['stable']:.1%} of the frame between two grabs)"
    if reference is not None:
        if write_reference_once(reference, b):
            checks["plate"] = 0.0
        else:
            with Image.open(reference) as plate:
                ref = np.asarray(plate.convert("RGB")).astype(np.float32)
            checks["plate"] = round(float(differs(b, ref).mean()) if ref.shape == b.shape else 1.0, 4)
            if checks["plate"] > PLATE_MAX:
                return checks, (f"the plate differs from the stage's reference on {checks['plate']:.0%} of the frame "
                                "(another screen over the battle)")
    return checks, None


def master_suspect(path: Path | str) -> list[str]:
    """Why a master (512x768 RGBA) looks wrong, by its pixels alone (masters captured before the
    checks above existed): grey all over (a costume not loaded), or long straight horizontal edges
    across the fighter (a menu drawn over it). Empty when it looks right. A heuristic: it flags
    looks to capture again, it does not prove them wrong."""
    with Image.open(path) as im:
        rgba = np.asarray(im.convert("RGBA"))
    mask = rgba[..., 3] > 128
    if mask.sum() < 500:
        return ["almost empty"]
    rgb = rgba[..., :3]
    why = []
    if untextured(rgb, mask):
        why.append("untextured (grey all over)")
    g = rgb.astype(np.float32).mean(axis=2)
    edge = (np.abs(np.diff(g, axis=0)) > 24) & mask[1:] & mask[:-1]
    width = max(1, int(mask.any(axis=0).sum()))
    runs = []
    for row in edge:
        best = run = 0
        for v in row:
            run = run + 1 if v else 0
            best = max(best, run)
        runs.append(best / width)
    lines = sum(1 for r in runs if r > SUSPECT_LINE)
    if lines >= SUSPECT_LINES:
        why.append(f"{lines} straight edges across it (a menu over it?)")
    return why


UNTEXTURED_TINT = 0.4      # a fighter this much light cyan-grey... (bare models 0.47-0.84, textured with no warm pixel 0.16 at most)


UNTEXTURED_WARM = 0.01     # ...with no more warm pixels than this is drawn without its textures...


UNTEXTURED_DARK = 0.1      # ...unless more of it than this is dark (bare models 0.043 at most)...


UNTEXTURED_DETAIL = 11.0   # ...or its fine detail averages more than this (bare models 9.7 at most)


SUSPECT_LINE = 0.18        # a row with a straight edge across this share of the fighter's width...


SUSPECT_LINES = 3          # ...this many times is flagged


def check_masters(store: Path, space: dict, out: Path, unchecked: bool, quarantine: bool = False) -> int:
    """Every look's master checked (master_suspect, and with `unchecked` the masters whose sidecar
    has no grab checks); the ones to capture again written to `out`. With `quarantine`, a master
    drawn without its textures is set aside (renamed *.untextured.png) and logged as a failed look,
    so the pack leaves it out (--except-failed) instead of shipping a grey fighter."""
    redo: list[str] = []
    log_lines: list[str] = []
    counts = collections.Counter()
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            for colour in colours:
                master = master_path(store, code, int(costume), int(colour))
                if not master.is_file():
                    continue
                counts["masters"] += 1
                why = master_suspect(master)
                try:
                    meta = json.loads(master.with_suffix(".json").read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    meta = {}
                if unchecked and "checks" not in meta:
                    why.append("captured before the grab checks")
                for w in why:
                    counts[w.split(" (")[0].split(" straight")[0] if "straight" not in w else "menu over it"] += 1
                if quarantine and any(w.startswith("untextured") for w in why):
                    for suffix in (".png", ".json"):
                        src = master.with_suffix(suffix)
                        if src.is_file():
                            os.replace(src, master.with_name(master.stem + ".untextured" + suffix))
                    log_lines.append(json.dumps({"time": time.strftime("%Y-%m-%dT%H:%M:%S"), "copy": -1, "phase": "looks",
                                                 "look": {"fighter": code, "costume": int(costume), "colour": int(colour)},
                                                 "reason": "drawn without its textures (this copy of the game lacks them?)",
                                                 "stage": meta.get("stage", "")}))
                    counts["set aside"] += 1
                if why:
                    print(f"{code}:{costume}:{colour}  " + "; ".join(why))
                    # Set aside as untextured: capturing it again on this copy draws it the same.
                    if not (quarantine and any(w.startswith("untextured") for w in why)):
                        redo.append(f"{code}:{costume}:{colour}")
    if log_lines:
        with open(store / "capture-log.jsonl", "a", encoding="utf-8") as f:
            f.write("".join(line + chr(10) for line in log_lines))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(redo) + ("\n" if redo else ""), encoding="utf-8")
    print(f"{len(redo)} of {counts['masters']} masters to capture again -> {out}  ({dict(counts)})")
    return 0
