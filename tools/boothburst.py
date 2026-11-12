"""The loadout pictures' frame, chosen from a burst of grabs (no one picks frames by hand).

For each V-Skill and V-Trigger runbooth.py grabs, from one held battle:
  pre    the fighter standing before the press,
  plate  the same, the fighter scaled away (the stage behind it),
  burst  a grab every few fight frames after the press, up to a couple of seconds.
This module scores each burst grab and picks one:
  - the camera: the top band of the frame (above the fighters) against the pre grab's, after the
    pan that best lines them up (phase correlation). A grab whose band still differs (the camera
    zoomed or turned, the stage dimmed or covered) is not used;
  - a flash: a grab whose band is much brighter or darker than the pre grab's is not used;
  - the score: what changed below the band (pose, aura, effects), against the fighter's own size;
  - the choice: never before MIN_AFTER fight frames after the press (the move has not shown yet);
    of the rest, the peak: the first grab whose stable score (the least of it and its two
    neighbours, an unusable neighbour counting 0) reaches PEAK_SHARE of the best stable score, so
    a one-frame spike or a mid-flash grab is not taken over a held pose.
None when no grab is usable or moves enough (runbooth then takes the configured default frame).
The picture is framed as SF4 Ember's Ultra pictures: a 16:9 window on the fighter and its effects
(subject_box: where the chosen grab differs from the plate), the subject filling FILL of its
height, kept inside the frame (frame_window).

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
CAMERA_MAX = 0.05         # share of the top band that may still differ after the pan
SHIFT_MAX = 0.2           # a pan larger than this share of the picture is not a usable grab
FLASH_LUMA = 30.0         # mean brightness change of the top band that counts as a flash (0-255)
BAND = 0.2                # the top of the frame, above the fighters: the camera and flashes are judged on it
PEAK_SHARE = 0.95         # the first grab whose stable score reaches this share of the best: the peak
MIN_SCORE = 0.05          # a move that changes less than this share of the fighter's size shows nothing
MIN_AFTER = 12            # fight frames after the press before a grab may be chosen
BOX_TRIM = 0.01           # the subject box leaves out this share of its pixels on each side
FILL = 0.85               # the subject fills this share of the picture's height (SF4's Ultra pictures)
MIN_HEIGHT = 0.3          # the window is at least this share of the frame high


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


def score_grab(pre: np.ndarray, grab: np.ndarray, area: int) -> Grab:
    """One shrunk burst grab against the shrunk pre grab; `area` the fighter's pixels in pre."""
    h, w = pre.shape[:2]
    band = max(8, int(h * BAND))
    # The camera and a flash are judged on the band at the top of the frame, above the fighters:
    # effects and dashes below it do not count against a grab.
    dy, dx = phase_shift(luma(pre[:band]), luma(grab[:band]))
    g = Grab(dy=dy, dx=dx)
    aligned, valid = moved(pre, dy, dx)
    change = differs(grab, aligned) & valid
    top = valid[:band]
    g.camera = float(change[:band][top].mean()) if top.any() else 1.0
    lum = luma(grab[:band])[top].mean() - luma(aligned[:band])[top].mean() if top.any() else 0.0
    g.flash = abs(float(lum)) > FLASH_LUMA
    # The move: what changed below the band, against the fighter's own size (pose, aura, effects).
    g.score = float(change[band:].sum()) / max(area, 1)
    g.usable = (not g.flash and g.camera <= CAMERA_MAX and abs(dy) <= SHIFT_MAX * h and abs(dx) <= SHIFT_MAX * w)
    return g


def score_shrunk(pre: np.ndarray, plate: np.ndarray, grabs: list[np.ndarray]) -> list[Grab]:
    """Every burst grab scored, all pictures already shrunk (`shrink`)."""
    area = int(differs(pre, plate).sum())
    return [score_grab(pre, g, area) for g in grabs]


def score_burst(pre: np.ndarray, plate: np.ndarray, grabs: list[np.ndarray]) -> list[Grab]:
    """Every burst grab scored (full-size uint8 pictures in; shrunk here)."""
    return score_shrunk(shrink(pre), shrink(plate), [shrink(g) for g in grabs])


def choose(scores: list[Grab], times: list[int] | None = None, min_after: int = MIN_AFTER,
           peak_share: float = PEAK_SHARE, min_score: float = MIN_SCORE) -> int | None:
    """The chosen grab's index, or None (see the module's text). `times` are the grabs' fight
    frames after the press; grabs before `min_after` are never chosen (the move has not shown yet)."""
    def s(i: int) -> float | None:
        if i < 0 or i >= len(scores):
            return None
        return scores[i].score if scores[i].usable else 0.0

    stable = []
    for i, g in enumerate(scores):
        early = times is not None and times[i] < min_after
        if not g.usable or early:
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


def subject_box(plate: np.ndarray, grab: np.ndarray, g: Grab, trim: float = BOX_TRIM) -> tuple[int, int, int, int] | None:
    """Full size: (y0, y1, x0, x1) of the fighter and its effects in the chosen grab: where it
    differs from the plate (the stage, lined up with the grab's camera), with the outermost `trim`
    share of those pixels on each side left out (a stray spark or a far-off flicker does not
    stretch the box). None when nothing differs."""
    dy, dx = g.dy * DOWN, g.dx * DOWN  # the pan was measured shrunk
    aligned, valid = moved(plate.astype(np.int16), dy, dx)
    mask = differs(grab.astype(np.int16), aligned) & valid
    ys, xs = np.nonzero(mask)
    if ys.size < 50:
        return None
    y0, y1 = np.quantile(ys, [trim, 1 - trim])
    x0, x1 = np.quantile(xs, [trim, 1 - trim])
    return int(y0), int(y1) + 1, int(x0), int(x1) + 1


def frame_window(box: tuple[int, int, int, int] | None, height: int, width: int, fill: float = FILL,
                 side: int = 0) -> tuple[int, int, int, int]:
    """(x0, y0, w, h): the 16:9 window, as SF4 Ember's Ultra pictures are framed, where the
    subject's box fills `fill` of the height (or of the width, when it is wider than 16:9), centred
    on it, kept inside the picture (moved in, and shrunk only when the picture itself is smaller)."""
    if box is None:  # nothing found: where the side stands, at half the picture's height
        cx, cy, bh, bw = width * (0.3 if side == 0 else 0.7), height * 0.62, height * 0.45, height * 0.3
    else:
        y0, y1, x0, x1 = box
        cx, cy, bh, bw = (x0 + x1) / 2, (y0 + y1) / 2, y1 - y0, x1 - x0
    crop_h = max(bh / fill, bw / fill * 9 / 16, height * MIN_HEIGHT)
    crop_h = min(crop_h, height, width * 9 / 16)
    crop_w = round(crop_h * 16 / 9)
    crop_h = round(crop_w * 9 / 16)
    x = int(min(max(cx - crop_w / 2, 0), width - crop_w))
    y = int(min(max(cy - crop_h / 2, 0), height - crop_h))
    return x, y, crop_w, crop_h


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

    # A grab before MIN_AFTER is never chosen, however much it changed.
    early = [_fighter(stage, 500, 120)] + [_fighter(stage, 500, 40)] * 4
    chosen = choose(score_burst(pre, plate, early), times=[4, 12, 16, 20, 24])
    check(chosen == 1, f"a grab 4 frames after the press is not chosen (chose {chosen})")

    # Nothing moves: no choice (runbooth takes the default frame).
    check(choose(score_burst(pre, plate, [pre] * 5)) is None, "a burst where nothing changes chooses nothing")
    # Nothing usable: no choice.
    check(choose([Grab(score=0.5, usable=False)] * 4) is None, "a burst with no usable grab chooses nothing")

    # The picture is framed on the fighter and its aura, filling most of the height.
    chosen_grab = _fighter(stage, 500, 60)
    g = score_burst(pre, plate, [chosen_grab])[0]
    box = subject_box(plate, chosen_grab, g)
    check(box is not None and box[2] <= 450 and box[3] >= 770 and box[0] <= 450,
          f"the subject box holds the fighter and its aura ({box})")
    x, y, cw, ch = frame_window(box, 1080, 1920)
    inside = box is not None and x <= box[2] and box[3] <= x + cw and y <= box[0] and box[1] <= y + ch
    check(inside and abs(cw * 9 - ch * 16) <= 16 and (box[1] - box[0]) >= 0.8 * ch,
          f"the window is 16:9, holds the box and the box fills most of its height ({x}, {y}, {cw}x{ch})")
    x, y, cw, ch = frame_window((900, 1075, 1700, 1919), 1080, 1920)
    check(x + cw <= 1920 and y + ch <= 1080 and x >= 0 and y >= 0, "a window at the picture's edge is kept inside it")

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
