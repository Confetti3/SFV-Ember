"""The cutout mask: grab A (fighters) against grab B (fighters scaled away) of one held frame,
the split into the two sides and the master images written from it."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .checks import untextured
from .grabs import DEPTH_DIFF_EPS, depth_target_id, read_colour, read_depth
from .imageops import ALPHA_BOX_MIN, fit, keep_components, morph
from .space import MASTER_MARGIN, MASTER_SIZE, BoothError, master_path


COLOUR_DIFF_THRESHOLD = 10  # per-channel difference between grab A and grab B


MIN_MASK_FRACTION = 0.0005  # a side's cutout smaller than this share of the frame is a failure


MAX_MASK_FRACTION = 0.5


SPLIT_TOUCH_SHARE = 0.04    # fighters touching across fewer rows than this share are cut apart there


def colour_mask(a: np.ndarray, b: np.ndarray, threshold: int = COLOUR_DIFF_THRESHOLD) -> np.ndarray:
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2)
    mask = diff > threshold
    # Parts of a fighter that happen to match the background leave holes and gaps: close them.
    return morph(morph(mask, "dilate", 7), "erode", 7) | mask


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
    mask = keep_components(mask)
    return mask, a, used


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


def to_rgba(rgb: np.ndarray, mask: np.ndarray) -> Image.Image:
    """The colour with the mask as alpha, eroded by one pixel so the background's blended edge
    is gone, and cropped to the alpha box."""
    mask = morph(mask, "erode", 3)
    alpha = mask.astype(np.uint8) * 255
    rgba = np.dstack([rgb, alpha])
    im = Image.fromarray(rgba, "RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a >= ALPHA_BOX_MIN else 0).getbbox()
    if box is None:
        raise BoothError("nothing left of the cutout after the 1 px erosion")
    return im.crop(box)


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
