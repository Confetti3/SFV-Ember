"""Photo booth post-processing: grabs -> cutout masters -> the picture pack.

Needs Pillow and numpy (tools/booth-requirements.txt). Python 3.10+.

  python -I tools/boothpack.py --selftest
  python -I tools/boothpack.py pack [--store D:\\sfv-ember-renders] [--except-failed] [--mod-revision HEX]
  python -I tools/boothpack.py validate [--store D:\\sfv-ember-renders]

Masters (written while the capture runs, by runbooth.py through make_masters):
  <store>\\masters\\<CODE>\\costume-<N>\\color-<M>.png   512x768 RGBA, lossless, the game's own numbers
  <store>\\masters\\<CODE>\\costume-<N>\\color-<M>.json  frame, stage, mask method
  <store>\\masters\\<CODE>\\vs1.png ...                  loadout pictures, 16:9, lossless (+ .json)

The pack (`pack`), exactly as docs/design/beta8/pack.md says:
  <store>\\pack\\assets\\selection\\<CODE>\\costume-<N>\\color-<M>-cutout.png   256x384, 256-colour palette PNG
  <store>\\pack\\assets\\selection\\<CODE>\\vs<k>.png, vt<k>.png               256x144 RGB PNG
  <store>\\pack\\manifest.json
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageFilter, ImageOps
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("boothpack needs Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

VERSION = "1.0"
PIPELINE = f"runbooth {VERSION} / boothpack {VERSION}"
DEFAULT_STORE = r"D:\sfv-ember-renders"
DEFAULT_PICK_SPACE = Path(__file__).resolve().parent.parent / "data" / "pick-space.json"

MASTER_SIZE = (512, 768)
MASTER_MARGIN = 32
PACK_SIZE = (256, 384)
PACK_MARGIN = 16            # the same canvas and margin as SF4's importer, at half the master
PACK_COLOURS = 256
ALPHA_BOX_MIN = 8           # alpha at or above this counts as the picture (the importer's rule)
LOADOUT_PACK_SIZE = (256, 144)  # SF4's ultra-N.png: 256x144 RGB PNG, ImageOps.fit with Lanczos, no palette
COLOUR_DIFF_THRESHOLD = 10  # per-channel difference between grab A and grab B
DEPTH_DIFF_EPS = 1e-5       # depth difference between the two grabs
MIN_MASK_FRACTION = 0.0005  # a side's cutout smaller than this share of the frame is a failure
MAX_MASK_FRACTION = 0.5
SPLIT_TOUCH_SHARE = 0.04    # fighters touching across fewer rows than this share are cut apart there

PART_NAMES = {"vs": "vskill", "vt": "vtrigger"}


class BoothError(RuntimeError):
    """A grab pair that cannot become a master (the reason goes to the capture log)."""


# --- the pick space ------------------------------------------------------------

def load_space(path: Path | str = DEFAULT_PICK_SPACE) -> dict:
    """The pick space as the capture needs it: build, game build and, per fighter code, its
    costume ids with their colours in the game's order and its loadout counts."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    fighters = collections.OrderedDict()
    for f in data["fighters"]:
        costumes = collections.OrderedDict((c["id"], list(c["colours"])) for c in f["costumes"])
        fighters[f["code"]] = {"costumes": costumes, "vs": int(f.get("v_skills", 0)), "vt": int(f.get("v_triggers", 0))}
    build = data["build"]
    return {"build": build, "gameBuild": build.split("/")[1], "fighters": fighters,
            "stages": list(data.get("stages", []))}


# --- the path rules (pack.md, re-implemented; the C++ function is the authority) ----

_NUM = r"(0|[1-9][0-9]{0,5})"
_CUTOUT_RE = re.compile(r"assets\\selection\\([^\\]+)\\costume-" + _NUM + r"\\color-" + _NUM + r"-cutout\.png")
_LOADOUT_RE = re.compile(r"assets\\selection\\([^\\]+)\\(vs|vt)" + _NUM + r"\.png")
MANIFEST_PACKAGE_PATH = "assets\\selection\\manifest.json"


def is_selection_asset_path(path: str, space: dict) -> bool:
    """Exactly what IsSfvSelectionAssetPath accepts: `path` relative to the package folder,
    backslash separated, case-exact; the manifest, a cutout of a look the pick space has, or a
    loadout picture k within the fighter's V-Skill / V-Trigger count."""
    if path == MANIFEST_PACKAGE_PATH:
        return True
    m = _CUTOUT_RE.fullmatch(path)
    if m:
        fighter = space["fighters"].get(m.group(1))
        if fighter is None:
            return False
        colours = fighter["costumes"].get(int(m.group(2)))
        return colours is not None and int(m.group(3)) in colours
    m = _LOADOUT_RE.fullmatch(path)
    if m:
        fighter = space["fighters"].get(m.group(1))
        if fighter is None:
            return False
        return 1 <= int(m.group(3)) <= fighter[m.group(2)]
    return False


def cutout_relpath(code: str, costume: int, colour: int) -> str:
    return f"assets/selection/{code}/costume-{costume}/color-{colour}-cutout.png"


def loadout_relpath(code: str, part: str, choice: int) -> str:
    return f"assets/selection/{code}/{part}{choice}.png"


# --- store layout ------------------------------------------------------------------

def master_path(store: Path, code: str, costume: int, colour: int) -> Path:
    return store / "masters" / code / f"costume-{costume}" / f"color-{colour}.png"


def loadout_master_path(store: Path, code: str, part: str, choice: int) -> Path:
    return store / "masters" / code / f"{part}{choice}.png"


# --- reading grabs --------------------------------------------------------------------

def read_colour(path: Path | str) -> np.ndarray:
    """A grab's BMP as an HxWx3 uint8 array (the alpha channel of a back buffer is not colour)."""
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB"))


def read_depth(path: Path | str) -> np.ndarray | None:
    """The grab's depth sidecar (`<path>.depth` raw 32-bit values, `<path>.json` beside it) as
    an HxW float32 array in 0..1, or None when either file is missing or unreadable."""
    path = Path(path)
    raw, meta = Path(str(path) + ".depth"), Path(str(path) + ".json")
    if not raw.is_file() or not meta.is_file():
        return None
    try:
        info = json.loads(meta.read_text(encoding="utf-8"))
        # The sidecar may be wrapped in a "depth" object; take the first mapping that has a width.
        if "width" not in info and isinstance(info.get("depth"), dict):
            info = info["depth"]
        width, height = int(info["width"]), int(info["height"])
        pitch = int(info.get("rowPitch", info.get("pitch", info.get("rowPitchBytes", width * 4))))
        fmt = str(info.get("format", "D32_FLOAT")).upper()
        values = str(info.get("values", "")).lower()  # the mod's word for the texel layout
        data = np.fromfile(raw, dtype=np.uint8)
        if pitch < width * 4 or data.size < pitch * height:
            return None
        words = np.ascontiguousarray(data[: pitch * height].reshape(height, pitch)[:, : width * 4]).view("<u4").reshape(height, width)
    except (OSError, ValueError, KeyError, TypeError):
        return None
    if values == "float32":
        return words.view("<f4").astype(np.float32)
    if values == "unorm16":
        return (words & 0xFFFF).astype(np.float32) / 65535.0
    if values == "unorm24_stencil8":
        return ((words & 0xFFFFFF).astype(np.float32)) / float(0xFFFFFF)
    if values:
        return None  # a layout this reader does not know: the colour difference is used
    if "FLOAT" in fmt and "X24" not in fmt and "S8" not in fmt:
        return words.view("<f4").astype(np.float32)
    if "D24" in fmt or "R24" in fmt:
        return ((words & 0xFFFFFF).astype(np.float32)) / float(0xFFFFFF)
    return words.astype(np.float32) / float(0xFFFFFFFF)


def depth_target_id(path: Path | str) -> int | None:
    """The texture (its address, the mod's `id`) the grab's `<path>.depth` was copied from; None
    for a sidecar that does not say (an older mod)."""
    try:
        info = json.loads(Path(str(path) + ".json").read_text(encoding="utf-8"))
        index = int(info["index"])
        for target in info["depthTargets"]["targets"]:
            if int(target["index"]) == index and "id" in target:
                return int(target["id"])
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


# --- the mask ---------------------------------------------------------------------------

def _filter(mask: np.ndarray, kind: str, size: int) -> np.ndarray:
    im = Image.fromarray((mask.astype(np.uint8)) * 255, "L")
    im = im.filter(ImageFilter.MaxFilter(size) if kind == "dilate" else ImageFilter.MinFilter(size))
    return np.asarray(im) > 127


def _keep_components(mask: np.ndarray, min_share: float = 0.02, cell: int = 4) -> np.ndarray:
    """Drops specks: connected pieces (8-neighbour, on a grid coarsened by `cell`) smaller than
    `min_share` of the mask's area. The two fighters are both kept."""
    h, w = mask.shape
    hh, ww = h // cell, w // cell
    if hh == 0 or ww == 0:
        return mask
    small = mask[: hh * cell, : ww * cell].reshape(hh, cell, ww, cell).any(axis=(1, 3))
    labels = np.zeros(small.shape, dtype=np.int32)
    areas: list[int] = [0]
    for y0, x0 in zip(*np.nonzero(small)):
        if labels[y0, x0]:
            continue
        areas.append(0)
        lab = len(areas) - 1
        stack = [(y0, x0)]
        labels[y0, x0] = lab
        while stack:
            y, x = stack.pop()
            areas[lab] += 1
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < hh and 0 <= nx < ww and small[ny, nx] and not labels[ny, nx]:
                        labels[ny, nx] = lab
                        stack.append((ny, nx))
    total = sum(areas)
    if total == 0:
        return mask
    keep = np.zeros(len(areas), dtype=bool)
    for lab in range(1, len(areas)):
        keep[lab] = areas[lab] >= min_share * total
    keep_small = keep[labels]
    keep_small = _filter(keep_small, "dilate", 3)  # a one-cell skirt so edges are not clipped
    full = np.zeros(mask.shape, dtype=bool)
    full[: hh * cell, : ww * cell] = np.repeat(np.repeat(keep_small, cell, axis=0), cell, axis=1)
    return mask & full


def colour_mask(a: np.ndarray, b: np.ndarray, threshold: int = COLOUR_DIFF_THRESHOLD) -> np.ndarray:
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    mask = diff > threshold
    # Parts of a fighter that happen to match the background leave holes and gaps: close them.
    return _filter(_filter(mask, "dilate", 7), "erode", 7) | mask


def depth_mask(da: np.ndarray, db: np.ndarray, shape: tuple[int, int], eps: float = DEPTH_DIFF_EPS) -> np.ndarray:
    """Pixels where the two grabs' depth differs. A depth target of another size than the
    picture is matched by nearest neighbour."""
    if da.shape != db.shape:
        raise BoothError(f"the depth sidecars differ in size ({da.shape} against {db.shape})")
    # "Changed", not near or far: the depth may be reversed-Z.
    mask = np.abs(da.astype(np.float32) - db.astype(np.float32)) > eps
    if mask.shape != shape:
        ys = np.minimum(np.arange(shape[0]) * mask.shape[0] // shape[0], mask.shape[0] - 1)
        xs = np.minimum(np.arange(shape[1]) * mask.shape[1] // shape[1], mask.shape[1] - 1)
        mask = mask[ys][:, xs]
    return mask


def compute_mask(a_path: Path | str, b_path: Path | str, method: str = "depth") -> tuple[np.ndarray, np.ndarray, str]:
    """(mask, colour of grab A, method used) for grab A (fighters normal) and grab B (fighters
    scaled away) of one frozen frame. `method` "depth" uses the depth difference when both
    sidecars exist and show something, and the colour difference otherwise; "colour" always the
    colour difference."""
    a, b = read_colour(a_path), read_colour(b_path)
    if a.shape != b.shape:
        raise BoothError(f"grab A is {a.shape[1]}x{a.shape[0]} and grab B {b.shape[1]}x{b.shape[0]}")
    colour = colour_mask(a, b)
    used = "colour"
    mask = colour
    if method == "depth":
        da, db = read_depth(a_path), read_depth(b_path)
        ida, idb = depth_target_id(a_path), depth_target_id(b_path)
        if ida is not None and idb is not None and ida != idb:
            # The two grabs copied different depth textures: no difference of them means anything.
            da = db = None
        if da is not None and db is not None:
            dm = depth_mask(da, db, a.shape[:2])
            if dm.mean() >= MIN_MASK_FRACTION:  # a depth target with no fighters in it shows no difference
                mask, used = dm, "depth"
    mask = _keep_components(mask)
    return mask, a, used


# --- grab checks (runbooth.py, before a master is written) ------------------------------------
#
# The first-colour capture of 2026-11-12 had SFV's main menu drawn over the battle in about a third
# of the looks and some fighters drawn before their costume had loaded (grey, or the default
# texture). The mod now grabs before the interface (grab ui=off) and reports SFV's loading; these
# checks catch what is left: a screen that changed between two grabs of the frozen frame.

CHECK_DOWN = 4             # the checks compare pictures shrunk 4x (480x270 from 1920x1080)
CHECK_DIFF = 18            # a pixel differs when a channel differs by more than this (0-255)
STABLE_MAX = 0.01          # two grabs of the held frame may differ on at most this share of the frame
OUTSIDE_MAX = 0.01         # grab A and the plate may differ outside the fighters on at most this share
GLOW_HALO = 41             # the glow zone: the fighters widened by 20 shrunk pixels (80 in the frame)
PLATE_MAX = 0.15           # the plate may differ from the stage's reference plate on at most this share


def _shrunk(path: Path | str, down: int = CHECK_DOWN) -> np.ndarray:
    rgb = read_colour(path)
    h, w = rgb.shape[0] // down * down, rgb.shape[1] // down * down
    return rgb[:h, :w].reshape(h // down, down, w // down, down, 3).astype(np.float32).mean(axis=(1, 3))


def _differs(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.abs(a - b).max(axis=-1) > CHECK_DIFF


def grabs_differ(a_path: Path | str, b_path: Path | str) -> float:
    """The share of the frame where two grabs differ (shrunk): 0 for two grabs of one held frame
    once everything has loaded."""
    a, b = _shrunk(a_path), _shrunk(b_path)
    if a.shape != b.shape:
        return 1.0
    return float(_differs(a, b).mean())


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
    shipped assets for, the same on every install."""
    if not mask.any():
        return False
    px = rgb[mask].astype(np.int32)
    mx, mn = px.max(axis=1), px.min(axis=1)
    sat = (mx - mn) / np.maximum(mx, 1)
    tint = float(((px[:, 2] >= px[:, 0]) & (px[:, 1] >= px[:, 0] - 4) & (sat < 0.35) & (mx > 110)).mean())
    return tint >= UNTEXTURED_TINT and warm_share(rgb, mask) <= UNTEXTURED_WARM


def check_pair(a_path: Path | str, b_path: Path | str, reference: Path | None) -> tuple[dict, str | None]:
    """Grab A (fighters) against grab B (the plate) of one held frame, before masters are made.
    Outside the fighters (the depth difference, widened) A must be B; B must be the stage as the
    reference plate shows it (`reference`, a shrunk PNG; written from this B when it does not exist
    yet and the pair passed). Returns the measures and the reason the pair is refused, or None."""
    a, b = _shrunk(a_path), _shrunk(b_path)
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
        fighters = _filter(fighters, "dilate", 7)
        outside = ~fighters
        # The fighters' shadows (and the scene's ambient occlusion around them) darken the floor in
        # A without changing its depth: a pixel only darker in A than in the plate is not a change.
        shadow = (a <= b + 4).all(axis=-1)
        # A fighter's glow (Kage's aura, fire) lights the stage around it without a depth of its
        # own: near the fighters a pixel only brighter in A than in the plate is not a change. A
        # menu or a toast is found by the rest of it, far from the fighters.
        near = _filter(fighters, "dilate", GLOW_HALO)
        glow = near & (a.mean(axis=-1) > b.mean(axis=-1))
        changed = _differs(a, b) & ~shadow & ~glow
        checks["outside"] = round(float(changed[outside].mean()) if outside.any() else 0.0, 5)
        checks["warm"] = round(warm_share(a, fighters & _differs(a, b)), 4)
        if checks["outside"] > OUTSIDE_MAX:
            return checks, (f"grab A and the plate differ outside the fighters on {checks['outside']:.1%} of the frame "
                            "(the screen changed between them)")
    if reference is not None:
        if reference.is_file():
            ref = np.asarray(Image.open(reference).convert("RGB")).astype(np.float32)
            checks["plate"] = round(float(_differs(b, ref).mean()) if ref.shape == b.shape else 1.0, 4)
            if checks["plate"] > PLATE_MAX:
                return checks, (f"the plate differs from the stage's reference on {checks['plate']:.0%} of the frame "
                                "(another screen over the battle)")
        else:
            reference.parent.mkdir(parents=True, exist_ok=True)
            Image.fromarray(np.clip(b, 0, 255).astype(np.uint8)).save(reference)
            checks["plate"] = 0.0
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
UNTEXTURED_WARM = 0.01     # ...with no more warm pixels than this is drawn without its textures
SUSPECT_LINE = 0.18        # a row with a straight edge across this share of the fighter's width...
SUSPECT_LINES = 3          # ...this many times is flagged


def split_sides(mask: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """P1's and P2's part of a mask with both fighters in it: split at the middle of the widest
    empty column run in the central half of the frame. BoothError when the fighters touch."""
    h, w = mask.shape
    cols = mask.any(axis=0)
    lo, hi = w // 4, w - w // 4
    best, run_start = None, None
    for x in range(lo, hi + 1):
        empty = x < hi and not cols[x]
        if empty and run_start is None:
            run_start = x
        elif not empty and run_start is not None:
            if best is None or x - run_start > best[1] - best[0]:
                best = (run_start, x)
            run_start = None
    if best is None:
        # They touch (a foot, a fist): the emptiest column of the central half, when hardly
        # anything crosses it (SPLIT_TOUCH_SHARE of the frame's height).
        counts = mask[:, lo:hi].sum(axis=0)
        x = int(np.argmin(counts))
        if counts[x] > SPLIT_TOUCH_SHARE * h:
            raise BoothError("the two fighters touch in the mask (no empty column between them)")
        best = (lo + x, lo + x + 1)
    cut = (best[0] + best[1]) // 2
    left, right = mask.copy(), mask.copy()
    left[:, cut:] = False
    right[:, :cut] = False
    return left, right


def check_fraction(mask: np.ndarray, what: str) -> None:
    share = float(mask.mean())
    if share < MIN_MASK_FRACTION:
        raise BoothError(f"{what}: the mask is empty ({share:.5f} of the frame)")
    if share > MAX_MASK_FRACTION:
        raise BoothError(f"{what}: the mask covers {share:.2f} of the frame")


# --- cutout images ------------------------------------------------------------------------

def to_rgba(rgb: np.ndarray, mask: np.ndarray) -> Image.Image:
    """The colour with the mask as alpha, eroded by one pixel so the background's blended edge
    is gone, and cropped to the alpha box."""
    mask = _filter(mask, "erode", 3)
    alpha = mask.astype(np.uint8) * 255
    rgba = np.dstack([rgb, alpha])
    im = Image.fromarray(rgba, "RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a >= ALPHA_BOX_MIN else 0).getbbox()
    if box is None:
        raise BoothError("nothing left of the cutout after the 1 px erosion")
    return im.crop(box)


def fit(image: Image.Image, size: tuple[int, int], margin: int) -> Image.Image:
    """`image` scaled to fit `size` less `margin` on every side (premultiplied alpha, Lanczos)
    and placed bottom-centred on a transparent canvas."""
    width, height = size
    factor = min((width - 2 * margin) / image.width, (height - 2 * margin) / image.height)
    target = (max(1, round(image.width * factor)), max(1, round(image.height * factor)))
    scaled = image.convert("RGBa").resize(target, Image.Resampling.LANCZOS).convert("RGBA")
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.alpha_composite(scaled, ((width - target[0]) // 2, height - margin - target[1]))
    return canvas


def alpha_crop(image: Image.Image) -> Image.Image:
    box = image.getchannel("A").point(lambda a: 255 if a >= ALPHA_BOX_MIN else 0).getbbox()
    if box is None:
        raise BoothError("empty picture")
    return image.crop(box)


def make_masters(a_path: Path | str, b_path: Path | str, looks: list[tuple[str, int, int] | None], store: Path,
                 method: str = "depth", meta: dict | None = None) -> dict:
    """Writes the masters for one grab pair. `looks` is [P1's look, P2's look], each
    (code, costume, colour) or None for a side not wanted (then the pair holds that side's
    fighter only if the other was scaled away). Returns {side index: path or BoothError}; the
    method used is in the returned "method" key."""
    mask, rgb, used = compute_mask(a_path, b_path, method)
    wanted = [i for i, look in enumerate(looks) if look is not None]
    if len(wanted) == 2:
        try:
            sides = split_sides(mask)
        except BoothError as e:
            return {"method": used, 0: e, 1: e}
    else:
        sides = (mask, mask)
    results: dict = {"method": used}
    for i in wanted:
        code, costume, colour = looks[i]  # type: ignore[misc]
        try:
            check_fraction(sides[i], f"{code} costume {costume} colour {colour}")
            if untextured(rgb, sides[i]):
                raise BoothError(f"{code} costume {costume} colour {colour} is drawn without its textures "
                                 "(grey all over, no warm pixel: this copy of the game lacks them?)")
            cutout = to_rgba(rgb, sides[i])
            if i == 1:
                cutout = cutout.transpose(Image.Transpose.FLIP_LEFT_RIGHT)  # P2 faces left on screen
            master = fit(cutout, MASTER_SIZE, MASTER_MARGIN)
            target = master_path(store, code, costume, colour)
            write_master(target, master, dict(meta or {}, mask=used, side=i + 1))
            results[i] = target
        except BoothError as e:
            results[i] = e
    return results


def write_master(target: Path, image: Image.Image, meta: dict) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(".tmp.png")
    image.save(temp, optimize=False, compress_level=6)
    os.replace(temp, target)
    target.with_suffix(".json").write_text(json.dumps(meta, sort_keys=True), encoding="utf-8")


# --- the pack -------------------------------------------------------------------------------

def pack_cutout(master: Path, target: Path) -> None:
    """A master as SF4's importer makes a render into a selection cutout: cropped to the alpha
    box, fitted 256x384 with 16 px margin bottom-centred (premultiplied-alpha downscale), 256
    colours with transparency."""
    with Image.open(master) as im:
        canvas = fit(alpha_crop(im.convert("RGBA")), PACK_SIZE, PACK_MARGIN)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(".tmp.png")
    canvas.quantize(colors=PACK_COLOURS, method=Image.Quantize.FASTOCTREE,
                    dither=Image.Dither.FLOYDSTEINBERG).save(temp, optimize=True)
    os.replace(temp, target)


def pack_loadout(master: Path, target: Path) -> None:
    """A loadout master as SF4 makes its Ultra pictures (the library's fetch-ultra-references.py):
    ImageOps.fit to 256x144 with Lanczos, saved as an RGB PNG (not quantized); optimize=True
    only shrinks the file, losslessly."""
    with Image.open(master) as im:
        rgb = ImageOps.fit(im.convert("RGB"), LOADOUT_PACK_SIZE, Image.Resampling.LANCZOS)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(".tmp.png")
    rgb.save(temp, "PNG", optimize=True)
    os.replace(temp, target)


def _pack_one(job: tuple[str, str, str]) -> None:  # module level so a process pool can run it
    kind, source, target = job
    (pack_cutout if kind == "cutout" else pack_loadout)(Path(source), Path(target))


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_meta(master: Path, default_frame: int, default_stage: str) -> tuple[int, str]:
    try:
        meta = json.loads(master.with_suffix(".json").read_text(encoding="utf-8"))
        return int(meta.get("frame", default_frame)), str(meta.get("stage", default_stage))
    except (OSError, ValueError, TypeError):
        return default_frame, default_stage


def failed_looks(store: Path) -> dict[tuple[str, int, int], str]:
    """The looks the capture log holds as failed (the latest reason) whose master is still missing."""
    out: dict[tuple[str, int, int], str] = {}
    log = store / "capture-log.jsonl"
    if not log.is_file():
        return out
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            entry = json.loads(line)
            look = entry["look"]
            key = (look["fighter"], int(look["costume"]), int(look["colour"]))
        except (ValueError, KeyError, TypeError):
            continue  # a loadout failure, or a damaged line
        out[key] = str(entry.get("reason", "capture failed"))
    return {k: v for k, v in out.items() if not master_path(store, *k).is_file()}


def build_pack(store: Path, space: dict, mod_revision: str | None, except_failed: bool, default_frame: int,
               default_stage: str, jobs: int, force: bool = False) -> dict:
    """Converts every master into the pack and writes manifest.json. Returns the manifest."""
    pack = store / "pack"
    work: list[tuple[str, str, str]] = []
    files: list[dict] = []
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            for colour in colours:
                master = master_path(store, code, costume, colour)
                if not master.is_file():
                    continue
                rel = cutout_relpath(code, costume, colour)
                target = pack / rel
                if force or not target.is_file() or target.stat().st_mtime < master.stat().st_mtime:
                    work.append(("cutout", str(master), str(target)))
                frame, stage = read_meta(master, default_frame, default_stage)
                files.append({"path": rel, "look": {"fighter": code, "costume": costume, "colour": colour},
                              "frame": frame, "stage": stage})
        for prefix, part in PART_NAMES.items():
            for choice in range(1, fighter[prefix] + 1):
                master = loadout_master_path(store, code, prefix, choice)
                if not master.is_file():
                    continue
                rel = loadout_relpath(code, prefix, choice)
                target = pack / rel
                if force or not target.is_file() or target.stat().st_mtime < master.stat().st_mtime:
                    work.append(("loadout", str(master), str(target)))
                frame, stage = read_meta(master, default_frame, default_stage)
                files.append({"path": rel, "look": {"fighter": code, "part": part, "choice": choice},
                              "frame": frame, "stage": stage})
    if jobs > 1 and len(work) > 8:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            list(pool.map(_pack_one, work, chunksize=16))
    else:
        for job in work:
            _pack_one(job)
    # A stale file of an earlier run whose master is gone is not in the manifest, so the package
    # would refuse it: remove what no master produces.
    wanted = {(pack / f["path"]).resolve() for f in files}
    sel = pack / "assets" / "selection"
    if sel.is_dir():
        for stale in [p for p in sel.rglob("*") if p.is_file() and p.resolve() not in wanted]:
            stale.unlink()
    for f in files:
        path = pack / f["path"]
        f["sha256"] = sha256_file(path)
        f["bytes"] = path.stat().st_size
    files = [dict((k, f[k]) for k in ("path", "sha256", "bytes", "look", "frame", "stage")) for f in files]
    exceptions = []
    if except_failed:
        have = {(f["look"]["fighter"], f["look"].get("costume"), f["look"].get("colour")) for f in files}
        for (code, costume, colour), reason in sorted(failed_looks(store).items()):
            fighter = space["fighters"].get(code)
            if fighter and colour in fighter["costumes"].get(costume, []) and (code, costume, colour) not in have:
                exceptions.append({"fighter": code, "costume": costume, "colour": colour, "reason": reason[:200]})
    manifest = {"format": 1, "gameBuild": space["gameBuild"], "modRevision": mod_revision or find_mod_revision(),
                "pipeline": PIPELINE, "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "exceptions": exceptions, "files": files}
    if not manifest["modRevision"]:
        raise BoothError("no mod revision: pass --mod-revision or build the mod (build-provenance.json)")
    pack.mkdir(parents=True, exist_ok=True)
    (pack / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return manifest


def find_mod_revision() -> str | None:
    """sourceRevision of the build receipt (build-provenance.json in the build tree)."""
    for folder in (os.environ.get("SFVE_BUILD_DIR"), r"D:\sfv-ember-build"):
        if not folder:
            continue
        try:
            receipt = json.loads((Path(folder) / "build-provenance.json").read_text(encoding="utf-8-sig"))
            rev = str(receipt["sourceRevision"]).lower()
            if re.fullmatch(r"[0-9a-f]{7,40}", rev):
                return rev
        except (OSError, ValueError, KeyError):
            continue
    return None


# --- validation (pack.md's Read-RenderStore rules) ----------------------------------------------

def validate_pack(pack: Path, space: dict) -> tuple[list[str], list[str]]:
    """(errors, warnings) for `<store>\\pack`: the manifest and the files, against pack.md."""
    errors: list[str] = []
    warnings: list[str] = []
    try:
        manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return [f"manifest.json: {e}"], []
    if manifest.get("format") != 1 or isinstance(manifest.get("format"), bool):
        errors.append("format is not 1")
    if manifest.get("gameBuild") != space["gameBuild"]:
        errors.append(f"gameBuild {manifest.get('gameBuild')!r} is not the pick space's {space['gameBuild']!r}")
    if not isinstance(manifest.get("modRevision"), str) or not re.fullmatch(r"[0-9a-f]{7,40}", manifest["modRevision"]):
        errors.append("modRevision is not 7 to 40 lowercase hex")
    if not isinstance(manifest.get("pipeline"), str) or not manifest["pipeline"].strip():
        errors.append("pipeline is empty")
    if not isinstance(manifest.get("generatedAt"), str) or not re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", manifest["generatedAt"]):
        errors.append("generatedAt is not ISO 8601 UTC")
    try:
        manifest["pipeline"].encode("ascii")
        json.dumps(manifest).encode("ascii")
    except (UnicodeEncodeError, KeyError, AttributeError):
        errors.append("the manifest is not ASCII")

    excepted: set[tuple[str, int, int]] = set()
    for i, ex in enumerate(manifest.get("exceptions", [])):
        fighter = space["fighters"].get(ex.get("fighter"))
        costume = ex.get("costume")
        if fighter is None or costume not in fighter["costumes"]:
            errors.append(f"exceptions[{i}]: not a fighter/costume of the pick space")
            continue
        colours = fighter["costumes"][costume]
        colour = ex.get("colour", colours[0])
        if colour not in colours:
            errors.append(f"exceptions[{i}]: colour {colour} is not one of the costume's")
        if not isinstance(ex.get("reason"), str) or not ex["reason"].strip():
            errors.append(f"exceptions[{i}]: no reason")
        excepted.add((ex["fighter"], costume, colour))

    seen: set[str] = set()
    cutouts: set[tuple[str, int, int]] = set()
    loadouts = 0
    total_bytes = 0
    for i, f in enumerate(manifest.get("files", [])):
        rel = f.get("path", "")
        where = f"files[{i}] {rel}"
        if not isinstance(rel, str) or "\\" in rel:
            errors.append(f"{where}: path must use forward slashes")
            continue
        backslash = rel.replace("/", "\\")
        if rel in seen:
            errors.append(f"{where}: listed twice")
        seen.add(rel)
        if backslash == MANIFEST_PACKAGE_PATH or not is_selection_asset_path(backslash, space):
            errors.append(f"{where}: not a path the pick space allows")
            continue
        file = pack / rel
        if not file.is_file():
            errors.append(f"{where}: file missing")
            continue
        size = file.stat().st_size
        total_bytes += size
        if not isinstance(f.get("bytes"), int) or f["bytes"] <= 0 or f["bytes"] != size:
            errors.append(f"{where}: bytes {f.get('bytes')} is not the file's {size}")
        if not isinstance(f.get("sha256"), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", f["sha256"]) \
                or f["sha256"].lower() != sha256_file(file):
            errors.append(f"{where}: sha256 does not match the file")
        if not isinstance(f.get("frame"), (int, float)) or isinstance(f.get("frame"), bool):
            errors.append(f"{where}: frame is not a number")
        if not isinstance(f.get("stage"), str):
            errors.append(f"{where}: stage is not a string")
        look = f.get("look", {})
        m = _CUTOUT_RE.fullmatch(backslash)
        if m:
            expect = {"fighter": m.group(1), "costume": int(m.group(2)), "colour": int(m.group(3))}
            cutouts.add((m.group(1), int(m.group(2)), int(m.group(3))))
        else:
            m = _LOADOUT_RE.fullmatch(backslash)
            expect = {"fighter": m.group(1), "part": PART_NAMES[m.group(2)], "choice": int(m.group(3))}
            loadouts += 1
        if look != expect:
            errors.append(f"{where}: look {look} does not agree with the path ({expect})")
    for key in sorted(cutouts & excepted):
        errors.append(f"{key}: excepted and also has a file")

    sel = pack / "assets" / "selection"
    if sel.is_dir():
        for p in sel.rglob("*"):
            if p.is_file():
                rel = p.relative_to(pack).as_posix()
                if rel not in seen:
                    errors.append(f"{rel}: in the pack but not in the manifest")
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            if (code, costume, colours[0]) not in cutouts and (code, costume, colours[0]) not in excepted:
                errors.append(f"{code} costume {costume}: first colour {colours[0]} has no cutout and no exception")
    if len(seen) >= 59800 or total_bytes >= 3 * 1024 ** 3:
        errors.append("the pack is too big for the updater's zip reader (59,800 files / 3 GB)")
    possible = sum(fighter["vs"] + fighter["vt"] for fighter in space["fighters"].values())
    if loadouts < possible:
        warnings.append(f"{loadouts} of {possible} loadout pictures present")
    looks = sum(len(c) for f in space["fighters"].values() for c in f["costumes"].values())
    warnings.append(f"{len(cutouts)} cutouts of {looks} looks, {len(excepted)} excepted, {loadouts} loadout pictures")
    return errors, warnings


# --- self test -------------------------------------------------------------------------------------

def _synthetic_space() -> dict:
    return {"build": "sfv-ember/13173604/0.5/test/layout-test", "gameBuild": "13173604", "stages": ["TRN"],
            "fighters": collections.OrderedDict([
                ("AAA", {"costumes": collections.OrderedDict([(0, [0, 1, 100]), (2, [0])]), "vs": 2, "vt": 2}),
                ("BBB", {"costumes": collections.OrderedDict([(0, [0, 1])]), "vs": 1, "vt": 1}),
            ])}


def _write_grabs(folder: Path, with_depth: bool, fighters_fill: tuple[tuple[int, int, int], tuple[int, int, int]]) -> tuple[Path, Path]:
    rng = np.random.default_rng(7)
    w, h = 1920, 1080
    ys, xs = np.mgrid[0:h, 0:w]
    background = np.dstack([(xs * 255 // w), (ys * 255 // h), np.full((h, w), 90)]).astype(np.uint8)
    a = background.copy()
    depth_b = np.full((h, w), 0.9, dtype=np.float32)
    depth_a = depth_b.copy()
    # P1: a 200x700 block at x 600-800. P2: 200x600 at x 1200-1400, with a marker on its screen-left edge.
    a[300:1000, 600:800] = fighters_fill[0]
    depth_a[300:1000, 600:800] = 0.4
    a[400:1000, 1200:1400] = fighters_fill[1]
    a[600:700, 1200:1240] = (255, 255, 255)      # the marker: left on screen, must end up on the right after the flip
    depth_a[400:1000, 1200:1400] = 0.4
    a[100:104, 100:104] = rng.integers(0, 255, (4, 4, 3))  # a speck of noise, below the component limit
    a_path, b_path = folder / "a.bmp", folder / "b.bmp"
    Image.fromarray(a, "RGB").save(a_path)
    Image.fromarray(background, "RGB").save(b_path)
    if with_depth:
        for path, depth in ((a_path, depth_a), (b_path, depth_b)):
            (Path(str(path) + ".depth")).write_bytes(depth.astype("<f4").tobytes())
            Path(str(path) + ".json").write_text(json.dumps(
                {"width": w, "height": h, "format": "D32_FLOAT", "rowPitch": w * 4, "present": 1}), encoding="utf-8")
    return a_path, b_path


def selftest() -> int:
    failures: list[str] = []

    def check(condition: bool, what: str) -> None:
        if not condition:
            failures.append(what)
        print(("ok   " if condition else "FAIL ") + what)

    space = _synthetic_space()
    # The path rules.
    good = ["assets\\selection\\manifest.json", "assets\\selection\\AAA\\costume-0\\color-100-cutout.png",
            "assets\\selection\\AAA\\costume-2\\color-0-cutout.png", "assets\\selection\\AAA\\vs2.png",
            "assets\\selection\\BBB\\vt1.png"]
    bad = ["assets\\selection\\AAA\\costume-1\\color-0-cutout.png", "assets\\selection\\AAA\\costume-0\\color-2-cutout.png",
           "assets\\selection\\AAA\\costume-0\\color-0-cutout.PNG", "assets\\selection\\AAA\\costume-00\\color-0-cutout.png",
           "assets\\selection\\AAA\\costume-0\\color-+0-cutout.png", "assets/selection/AAA/costume-0/color-0-cutout.png",
           "assets\\selection\\aaa\\vs1.png", "assets\\selection\\AAA\\vs3.png", "assets\\selection\\BBB\\vs2.png",
           "assets\\selection\\AAA\\vs0.png", "assets\\selection\\AAA\\vs1.jpg", "assets\\selection\\AAA\\vs1.png\\",
           "assets\\selection\\AAA\\costume-0\\color-0.png", "assets\\selection\\AAA\\..\\AAA\\vs1.png",
           "assets\\\\selection\\AAA\\vs1.png", "assets\\selection\\AAA\\vs1.png\x00", "assets\\selection\\AAA\\vs1.png:x",
           "assets\\selection\\AAA\\costume-0\\color-1234567-cutout.png", "assets\\selection\\AAAA\\vs1.png"]
    check(all(is_selection_asset_path(p, space) for p in good), "accepted forms are accepted")
    for p in bad:
        check(not is_selection_asset_path(p, space), f"refused {p!r}")
    check(is_selection_asset_path("assets\\selection\\manifest.json", {"fighters": {}}), "manifest accepted with an empty space")

    work = Path(tempfile.mkdtemp(prefix="boothpack-selftest-"))
    try:
        store = work / "store"
        # Depth path and colour path, each with two fighters and a speck of noise.
        d_dir = work / "d1"
        d_dir.mkdir()
        a, b = _write_grabs(d_dir, True, ((200, 40, 40), (40, 40, 200)))
        res = make_masters(a, b, [("AAA", 0, 0), ("BBB", 0, 0)], store, "depth", {"frame": 300, "stage": "TRN"})
        check(res.get("method") == "depth", "mask taken from the depth difference")
        for i, look in enumerate([("AAA", 0, 0), ("BBB", 0, 0)]):
            check(isinstance(res.get(i), Path) and res[i].is_file(), f"master for side {i + 1} written")
        m1 = Image.open(master_path(store, "AAA", 0, 0))
        m2 = Image.open(master_path(store, "BBB", 0, 0))
        check(m1.size == MASTER_SIZE and m1.mode == "RGBA", "master is 512x768 RGBA")
        check(m1.getchannel("A").getpixel((0, 0)) == 0 and m1.getchannel("A").getpixel((256, 600)) == 255, "master alpha: empty corner, solid body")
        a1 = np.asarray(m1)
        check(abs(int(a1[..., 0][a1[..., 3] > 200].mean()) - 200) < 3, "P1 colour kept")
        a2 = np.asarray(m2)
        solid = np.nonzero(a2[..., 3] > 200)
        white = (a2[..., 0] > 240) & (a2[..., 1] > 240) & (a2[..., 2] > 240) & (a2[..., 3] > 200)
        wx = np.nonzero(white)[1].mean()
        check(wx > (solid[1].min() + solid[1].max()) / 2, "P2 flipped: marker moved from the left edge to the right")
        ys = solid[0]
        check(ys.max() == MASTER_SIZE[1] - MASTER_MARGIN - 1 or ys.max() >= MASTER_SIZE[1] - MASTER_MARGIN - 2, "master bottom-aligned to the margin")
        # Colour fallback: no depth files.
        c_dir = work / "c1"
        c_dir.mkdir()
        a, b = _write_grabs(c_dir, False, ((10, 200, 10), (200, 200, 10)))
        res = make_masters(a, b, [("AAA", 0, 1), ("AAA", 0, 100)], store, "depth", {"frame": 300, "stage": "TRN"})
        check(res.get("method") == "colour" and isinstance(res.get(0), Path) and isinstance(res.get(1), Path), "colour fallback without depth sidecars")
        # One side only.
        res = make_masters(a, b, [("AAA", 2, 0), None], store, "colour", {"frame": 300, "stage": "TRN"})
        check(isinstance(res.get(0), Path), "single-side mask")
        # Failure: empty mask reports a reason.
        e_dir = work / "e1"
        e_dir.mkdir()
        Image.fromarray(np.zeros((1080, 1920, 3), np.uint8), "RGB").save(e_dir / "a.bmp")
        Image.fromarray(np.zeros((1080, 1920, 3), np.uint8), "RGB").save(e_dir / "b.bmp")
        res = make_masters(e_dir / "a.bmp", e_dir / "b.bmp", [("BBB", 0, 1), None], store, "colour")
        check(isinstance(res.get(0), BoothError), "empty mask is a failure with a reason")

        # Loadout masters (as runbooth writes them), then the pack.
        for code, prefix in (("AAA", "vs"), ("AAA", "vt"), ("BBB", "vs")):
            for choice in range(1, space["fighters"][code][prefix] + 1):
                p = loadout_master_path(store, code, prefix, choice)
                Image.fromarray(np.random.default_rng(choice).integers(0, 255, (936, 1664, 3), dtype=np.uint8), "RGB").save(p)
                p.with_suffix(".json").write_text(json.dumps({"frame": 640, "stage": "TRN"}))
        # BBB costume 1 is missing: AAA costume 0 colour 100 and costume 2 have no first-colour gap; except BBB 0/1 instead.
        (store / "capture-log.jsonl").write_text(json.dumps(
            {"look": {"fighter": "BBB", "costume": 0, "colour": 1}, "reason": "capture failed: costume does not load"}) + "\n")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        pack = store / "pack"
        errors, warnings = validate_pack(pack, space)
        check(not errors, "the pack validates against pack.md's rules" + (f": {errors[:3]}" if errors else ""))
        check([e["colour"] for e in manifest["exceptions"]] == [1], "a failed look with no master is excepted, with its reason")
        cut = pack / "assets/selection/AAA/costume-0/color-0-cutout.png"
        im = Image.open(cut)
        check(im.mode == "P" and "transparency" in im.info, "cutout is a palette PNG with transparency")
        rgba = im.convert("RGBA")
        check(rgba.size == PACK_SIZE and rgba.getpixel((0, 0))[3] == 0, "cutout is 256x384 with a transparent corner")
        box = rgba.getchannel("A").point(lambda v: 255 if v >= 8 else 0).getbbox()
        check(box[3] == PACK_SIZE[1] - PACK_MARGIN and box[1] >= PACK_MARGIN, "cutout bottom-centred with the 16 px margin")
        jpg = Image.open(pack / "assets/selection/AAA/vs1.png")
        check(jpg.format == "PNG" and jpg.mode == "RGB" and jpg.size == LOADOUT_PACK_SIZE, "loadout picture is a 256x144 RGB PNG")
        print(f"  sizes: cutout {cut.stat().st_size} bytes, loadout picture {(pack / 'assets/selection/AAA/vs1.png').stat().st_size} bytes")
        check({f["path"] for f in manifest["files"]} >= {"assets/selection/AAA/vs2.png", "assets/selection/BBB/vs1.png"}, "manifest lists the loadout pictures")
        # An accepted exception: remove BBB costume 0 colour 1's master and AAA's costume 2.
        master_path(store, "AAA", 2, 0).unlink()
        master_path(store, "AAA", 2, 0).with_suffix(".json").unlink()
        (store / "capture-log.jsonl").write_text(
            json.dumps({"look": {"fighter": "AAA", "costume": 2, "colour": 0}, "reason": "capture failed: look mismatch"}) + "\n")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        errors, _ = validate_pack(pack, space)
        check(not errors and len(manifest["exceptions"]) == 1, "--except-failed excepts a failed look and the pack validates" + (f": {errors[:3]}" if errors else ""))
        # Without the exception the missing first colour is an error.
        manifest = build_pack(store, space, "0123abc", False, 300, "TRN", 1)
        errors, _ = validate_pack(pack, space)
        check(any("first colour" in e for e in errors), "a missing first colour without an exception is refused")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        # Tampering is caught.
        victim = pack / "assets/selection/AAA/costume-0/color-0-cutout.png"
        original = victim.read_bytes()
        victim.write_bytes(original + b"x")
        errors, _ = validate_pack(pack, space)
        check(any("sha256" in e or "bytes" in e for e in errors), "a changed file is caught")
        victim.write_bytes(original)
        stray = pack / "assets/selection/AAA/costume-0/color-9-cutout.png"
        shutil.copyfile(victim, stray)
        errors, _ = validate_pack(pack, space)
        check(any("not in the manifest" in e for e in errors), "a file missing from the manifest is caught")
        stray.unlink()
        data = json.loads((pack / "manifest.json").read_text())
        data["files"][0]["path"] = data["files"][0]["path"].replace("-cutout.png", "-cutout.PNG")
        (pack / "manifest.json").write_text(json.dumps(data))
        errors, _ = validate_pack(pack, space)
        check(any("not a path" in e for e in errors), "a wrong-case path in the manifest is caught")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("SELFTEST " + ("FAILED: " + "; ".join(failures) if failures else "PASSED"))
    return 1 if failures else 0


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


# --- command line --------------------------------------------------------------------------------------

def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--selftest", action="store_true", help="synthetic grabs -> masks -> pack -> validation; no game")
    sub = p.add_subparsers(dest="cmd")
    for name in ("pack", "validate", "check"):
        s = sub.add_parser(name)
        s.add_argument("--store", default=DEFAULT_STORE)
        s.add_argument("--pick-space", default=str(DEFAULT_PICK_SPACE))
        if name == "pack":
            s.add_argument("--mod-revision", help="git revision of the mod that captured (default: the build receipt's sourceRevision)")
            s.add_argument("--except-failed", action="store_true", help="leave out, as manifest exceptions, the looks the capture log lists as failed")
            s.add_argument("--frame", type=int, default=300, help="frame recorded for a master with no sidecar")
            s.add_argument("--stage", default="TRN", help="stage recorded for a master with no sidecar")
            s.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
            s.add_argument("--force", action="store_true", help="rebuild every pack file")
        if name == "check":
            s.add_argument("--out", help="write the looks to capture again here, one CODE:costume:colour a line "
                                         "(for runbooth.py --looks-file); default <store>\\recheck.txt")
            s.add_argument("--unchecked", action="store_true",
                           help="also list masters captured before the grab checks (their sidecar has no checks)")
            s.add_argument("--quarantine-untextured", action="store_true",
                           help="set aside masters drawn without their textures and log them as failed looks")
    a = p.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.cmd:
        p.error("a command (pack, validate) or --selftest")
    space = load_space(a.pick_space)
    store = Path(a.store)
    if a.cmd == "check":
        return check_masters(store, space, Path(a.out) if a.out else store / "recheck.txt", a.unchecked, a.quarantine_untextured)
    try:
        if a.cmd == "pack":
            manifest = build_pack(store, space, a.mod_revision, a.except_failed, a.frame, a.stage, a.jobs, a.force)
            print(f"{len(manifest['files'])} files, {len(manifest['exceptions'])} exceptions -> {store / 'pack' / 'manifest.json'}")
        errors, warnings = validate_pack(store / "pack", space)
    except BoothError as e:
        print("error:", e, file=sys.stderr)
        return 2
    for w in warnings:
        print("note:", w)
    for e in errors[:50]:
        print("INVALID:", e)
    if len(errors) > 50:
        print(f"... and {len(errors) - 50} more")
    print("pack is valid" if not errors else f"pack is NOT valid ({len(errors)} problems)")
    return 3 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
