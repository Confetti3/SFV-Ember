"""The loadout pictures' frame, chosen from a burst of grabs (no one picks frames by hand).

For each V-Skill and V-Trigger runbooth.py grabs, from one held battle:
  pre    the fighter standing before the press,
  plate  the same, the fighter scaled away (the stage behind it),
  burst  a grab every few fight frames after the press, up to a couple of seconds.
This module scores each burst grab and picks one:
  - the camera: the burst grab's background against the pre grab's, after the pan that best lines
    them up (phase correlation). A grab whose background still differs (the camera zoomed,
    turned, or a full-screen effect covers the stage) is not used;
  - a flash: a grab much brighter or darker than the pre grab over the whole frame is not used;
  - the score: the share of the fighter's region (where the fighter stood, widened) that differs
    from the pre grab (pose, aura, effects);
  - the choice: the first grab whose stable score (the least of it and its two neighbours, an
    unusable neighbour counting 0) reaches PEAK_SHARE of the best stable score. A one-frame spike
    is not taken over a held pose, and a mid-flash grab is not taken.
None when no grab is usable or moves enough (runbooth then takes the configured default frame).

  python -I tools/boothburst.py --selftest

Needs numpy (tools/booth-requirements.txt). The scoring works on pictures shrunk by DOWN.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("boothburst needs numpy: python -m pip install -r tools/booth-requirements.txt")

DOWN = 4                  # scoring resolution: 1920x1080 -> 480x270
DIFF = 18                 # a pixel differs when a channel differs by more than this (0-255)
CAMERA_MAX = 0.12         # background share that may still differ after the pan
SHIFT_MAX = 0.2           # a pan larger than this share of the picture is not a usable grab
FLASH_LUMA = 30.0         # mean brightness change over the frame that counts as a flash (0-255)
PEAK_SHARE = 0.8          # the first grab whose stable score reaches this share of the best
MIN_SCORE = 0.02          # a move that changes less than this share of its region shows nothing
REGION_WIDEN = (0.5, 0.2)  # the fighter's box widened by this share of its size, sideways and up/down


@dataclass
class Grab:
    """One burst grab's scores."""
    dy: int = 0
    dx: int = 0
    camera: float = 0.0   # background share still differing after the pan
    flash: bool = False
    score: float = 0.0    # share of the fighter's region differing from the pre grab
    usable: bool = False


def shrink(rgb: np.ndarray, down: int = DOWN) -> np.ndarray:
    """An HxWx3 uint8 picture as float32, each `down` x `down` block averaged."""
    h, w = rgb.shape[0] // down * down, rgb.shape[1] // down * down
    return rgb[:h, :w].reshape(h // down, down, w // down, down, 3).astype(np.float32).mean(axis=(1, 3))


def luma(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def phase_shift(a: np.ndarray, b: np.ndarray) -> tuple[int, int]:
    """(dy, dx) such that `b` is `a` moved by it (b[y, x] ~ a[y - dy, x - dx]), for two grey
    pictures of one size, by phase correlation."""
    win = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    fa = np.fft.fft2((a - a.mean()) * win)
    fb = np.fft.fft2((b - b.mean()) * win)
    cross = fb * np.conj(fa)
    cross /= np.abs(cross) + 1e-9
    corr = np.abs(np.fft.ifft2(cross))
    dy, dx = np.unravel_index(int(np.argmax(corr)), corr.shape)
    h, w = a.shape
    return (int(dy) - h if dy > h // 2 else int(dy), int(dx) - w if dx > w // 2 else int(dx))


def moved(img: np.ndarray, dy: int, dx: int) -> tuple[np.ndarray, np.ndarray]:
    """`img` moved by (dy, dx), and the mask of the pixels that came from inside it."""
    out = np.roll(img, (dy, dx), axis=(0, 1))
    valid = np.ones(img.shape[:2], dtype=bool)
    if dy > 0:
        valid[:dy] = False
    elif dy < 0:
        valid[dy:] = False
    if dx > 0:
        valid[:, :dx] = False
    elif dx < 0:
        valid[:, dx:] = False
    return out, valid


def differs(a: np.ndarray, b: np.ndarray, threshold: float = DIFF) -> np.ndarray:
    return np.abs(a - b).max(axis=-1) > threshold


def fighter_box(pre: np.ndarray, plate: np.ndarray) -> tuple[int, int, int, int] | None:
    """(y0, y1, x0, x1) of where the pre grab differs from the plate (the fighter), widened by
    REGION_WIDEN; None when they do not differ."""
    mask = differs(pre, plate)
    rows, cols = np.nonzero(mask.any(axis=1))[0], np.nonzero(mask.any(axis=0))[0]
    if not rows.size:
        return None
    h, w = mask.shape
    y0, y1, x0, x1 = rows.min(), rows.max() + 1, cols.min(), cols.max() + 1
    gx, gy = int((x1 - x0) * REGION_WIDEN[0]), int((y1 - y0) * REGION_WIDEN[1])
    return max(0, y0 - gy), min(h, y1 + gy), max(0, x0 - gx), min(w, x1 + gx)


def score_grab(pre: np.ndarray, grab: np.ndarray, box: tuple[int, int, int, int]) -> Grab:
    """One shrunk burst grab against the shrunk pre grab, the fighter's region `box`."""
    h, w = pre.shape[:2]
    dy, dx = phase_shift(luma(pre), luma(grab))
    g = Grab(dy=dy, dx=dx)
    aligned, valid = moved(pre, dy, dx)
    region = np.zeros((h, w), dtype=bool)
    y0, y1, x0, x1 = box
    region[y0:y1, x0:x1] = True
    region, _ = moved(region, dy, dx)
    change = differs(grab, aligned) & valid
    background = valid & ~region
    g.camera = float(change[background].mean()) if background.any() else 1.0
    lum = luma(grab)[valid].mean() - luma(aligned)[valid].mean() if valid.any() else 0.0
    g.flash = abs(float(lum)) > FLASH_LUMA
    inside = region & valid
    g.score = float(change[inside].mean()) if inside.any() else 0.0
    g.usable = (not g.flash and g.camera <= CAMERA_MAX and abs(dy) <= SHIFT_MAX * h and abs(dx) <= SHIFT_MAX * w)
    return g


def score_shrunk(pre: np.ndarray, plate: np.ndarray, grabs: list[np.ndarray]) -> list[Grab]:
    """Every burst grab scored, all pictures already shrunk (`shrink`)."""
    box = fighter_box(pre, plate)
    if box is None:  # no fighter found: the whole frame is its region, so nothing is usable
        box = (0, pre.shape[0], 0, pre.shape[1])
    return [score_grab(pre, g, box) for g in grabs]


def score_burst(pre: np.ndarray, plate: np.ndarray, grabs: list[np.ndarray]) -> list[Grab]:
    """Every burst grab scored (full-size uint8 pictures in; shrunk here)."""
    return score_shrunk(shrink(pre), shrink(plate), [shrink(g) for g in grabs])


def choose(scores: list[Grab], peak_share: float = PEAK_SHARE, min_score: float = MIN_SCORE) -> int | None:
    """The chosen grab's index, or None (see the module's text)."""
    def s(i: int) -> float:
        if i < 0 or i >= len(scores):
            return None  # type: ignore[return-value]
        return scores[i].score if scores[i].usable else 0.0

    stable = []
    for i, g in enumerate(scores):
        if not g.usable:
            stable.append(0.0)
            continue
        around = [v for v in (s(i - 1), s(i), s(i + 1)) if v is not None]
        stable.append(min(around))
    best = max(stable, default=0.0)
    if best < min_score:
        return None
    for i, v in enumerate(stable):
        if v >= peak_share * best:
            return i
    return None  # pragma: no cover - the best itself always qualifies


def change_mask(pre: np.ndarray, plate: np.ndarray, grab: np.ndarray, g: Grab) -> np.ndarray:
    """Full size: where the chosen grab differs from the pre grab (after the pan), or the fighter
    stood: the crop's box covers the fighter, its new pose and its effects."""
    dy, dx = g.dy * DOWN, g.dx * DOWN  # the pan was measured shrunk
    pre32, grab32, plate32 = pre.astype(np.int16), grab.astype(np.int16), plate.astype(np.int16)
    aligned, valid = moved(pre32, dy, dx)
    fighter, _ = moved(differs(pre32, plate32), dy, dx)
    return (differs(grab32, aligned) & valid) | fighter


# --- selftest -----------------------------------------------------------------------------------

def _stage(h: int = 1080, w: int = 1920, seed: int = 1) -> np.ndarray:
    """A training-stage-like picture: grey with a grid and some texture, not periodic."""
    rng = np.random.default_rng(seed)
    img = np.full((h, w, 3), 110, dtype=np.int16)
    img += rng.integers(-12, 12, size=(h, w, 1), dtype=np.int16)
    for x in range(37, w, 96):
        img[:, x:x + 3] = 60
    for y in range(29, h, 96):
        img[y:y + 3, :] = 60
    img[:, 900:912] = (200, 30, 30)  # the stage's red line
    return np.clip(img, 0, 255).astype(np.uint8)


def _fighter(img: np.ndarray, x: int, extra: int = 0, colour=(230, 190, 150)) -> np.ndarray:
    """`img` with a fighter (a block) standing at x, and `extra` pixels of aura around it."""
    out = img.copy()
    if extra:
        out[500 - extra:960 + extra // 4, x - extra:x + 220 + extra] = (90, 160, 255)
    out[500:960, x:x + 220] = colour
    return out


def selftest() -> int:
    failures = []

    def check(ok: bool, what: str) -> None:
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    stage = _stage()
    pre = _fighter(stage, 500)
    plate = stage

    # The pan is found.
    a = luma(shrink(stage))
    b = luma(shrink(np.roll(stage, (8, -20), axis=(0, 1))))
    check(phase_shift(a, b) == (2, -5), f"a pan of 8, -20 pixels is found at a quarter size ({phase_shift(a, b)})")

    # A V-Trigger: the aura grows to a held size over a few grabs, with a flash on the second.
    burst = [_fighter(stage, 500, 0), _fighter(stage, 500, 10)]
    flash = np.clip(_fighter(stage, 500, 20).astype(np.int16) + 90, 0, 255).astype(np.uint8)
    burst.append(flash)
    for extra in (40, 60, 64, 66, 66, 66):
        burst.append(_fighter(stage, 500, extra))
    scores = score_burst(pre, plate, burst)
    check(scores[2].flash and not scores[2].usable, "a flash grab is not used")
    check(all(g.usable for i, g in enumerate(scores) if i != 2), "the other grabs are used")
    chosen = choose(scores)
    check(chosen is not None and chosen >= 4, f"the held aura is chosen, not its first rise (chose {chosen})")

    # A one-grab spike (a hit spark) is not taken over a held pose a little smaller.
    spike = [_fighter(stage, 500, 0)] + [_fighter(stage, 500, 40)] * 3 + [_fighter(stage, 500, 120)] + [_fighter(stage, 500, 5)] * 3
    chosen = choose(score_burst(pre, plate, spike))
    check(chosen in (2, 3), f"the held pose is chosen over a one-grab spike (chose {chosen})")

    # The camera pans with a dash: the pan is lined up and the grab is used.
    panned = np.roll(_fighter(stage, 500, 50), (0, -60), axis=(0, 1))
    g = score_burst(pre, plate, [panned])[0]
    check(g.usable and g.dx == -15, f"a camera pan is lined up (dx {g.dx}, camera {g.camera:.3f})")

    # The camera zooms (the stage scaled): not usable.
    zoom = np.asarray(_stage_zoom(stage, 1.15))
    g = score_burst(pre, plate, [_fighter(zoom, 500, 40)])[0]
    check(not g.usable, f"a camera zoom is not used (camera {g.camera:.3f})")

    # Nothing moves: no choice (runbooth takes the default frame).
    check(choose(score_burst(pre, plate, [pre] * 5)) is None, "a burst where nothing changes chooses nothing")
    # Nothing usable: no choice.
    check(choose([Grab(score=0.5, usable=False)] * 4) is None, "a burst with no usable grab chooses nothing")

    # The crop's mask covers the fighter and the aura of the chosen grab.
    chosen_grab = _fighter(stage, 500, 60)
    g = score_burst(pre, plate, [chosen_grab])[0]
    mask = change_mask(pre, plate, chosen_grab, g)
    rows, cols = np.nonzero(mask.any(axis=1))[0], np.nonzero(mask.any(axis=0))[0]
    check(cols.min() <= 440 + 4 and cols.max() >= 780 - 4 and rows.min() <= 440 + 4,
          f"the crop's mask holds the fighter and its aura (x {cols.min()}-{cols.max()}, y {rows.min()}-{rows.max()})")

    print("SELFTEST " + ("PASSED" if not failures else f"FAILED ({len(failures)})"))
    return 0 if not failures else 1


def _stage_zoom(stage: np.ndarray, factor: float):
    from PIL import Image  # only the selftest needs Pillow here
    h, w = stage.shape[:2]
    big = Image.fromarray(stage).resize((round(w * factor), round(h * factor)), Image.Resampling.BILINEAR)
    left, top = (big.width - w) // 2, (big.height - h) // 2
    return big.crop((left, top, left + w, top + h))


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(__doc__)
