"""The loadout pictures' frame, chosen from a burst of grabs (no one picks frames by hand).

For each V-Skill and V-Trigger runbooth.py grabs, from one held battle:
  plate  the fighter scaled away (the stage behind it), with its depth and the camera,
  pre    the fighter standing before the press, with the camera,
  burst  a grab every few fight frames after the press, up to a couple of seconds, each with the
         camera it was drawn through.
SFV's camera follows the fighter, so a burst grab sees the stage from elsewhere, the floor moving
more than the far wall. The plate is put back in the world by its depth and seen through each
grab's camera (boothcamera.py), so it lines up with the grab everywhere; what then differs is the
fighter, its effects and its shadow, wherever the move took it. For each grab:
  - the subject: that difference, without specks, thin lines (a stage line a pixel off), small
    pieces far from the rest (the cloth a scaled-away fighter leaves on the floor) and near-white
    glare (the white ring every V-Trigger's activation draws, larger than most fighters);
  - unusable: a flash or a dimmed stage (the stage as a whole, or around the subject, much brighter
    or darker than the plate; a V-Trigger's activation dims it), a stage that no longer matches the plate (a cinematic camera, a covered screen), or
    a camera that turned to where the plate never saw;
  - the score: the subject's size against the fighter's standing size, so a pose that spreads, an
    aura, a projectile score, and a place does not;
  - the choice: never before MIN_AFTER fight frames after the press (the move has not shown yet);
    of the rest, the usable grab with the largest subject (the first within PEAK_SHARE of it): the
    move's effect at its widest that is not a flash.
None when no grab is usable or the move adds too little (runbooth then takes the configured
default frame). The picture is framed as SF4 Ember's Ultra pictures: a 16:9 window on the subject,
the subject filling FILL of its height, centred, kept inside the frame (frame_window).

  python -I tools/boothburst.py --selftest

Needs numpy (tools/booth-requirements.txt). The scoring works on pictures shrunk by DOWN.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("boothburst needs numpy: python -m pip install -r tools/booth-requirements.txt")

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent))
import boothcamera as bc  # noqa: E402

DOWN = 4                  # scoring resolution: 1920x1080 -> 480x270
SOURCE_DOWN = 2           # the plate is put back in the world from a picture shrunk this much
DIFF = 18                 # a pixel differs when a channel differs by more than this (0-255)
STAGE_MAX = 0.05          # share of the stage around the subject that may still differ from the plate (cleaned)
COVER_MIN = 0.6           # share of the picture the plate must reach through the grab's camera
SUBJECT_MAX = 0.6         # a subject over this share of the picture is a stage that did not line up
GLARE_LUMA = 230.0        # near-white (brighter than this, channels within GLARE_SPREAD): the activation ring's glare
GLARE_SPREAD = 24.0
BAND = 0.2                # the top of the picture (the wall above the fighters): a shake is found on it
SHAKE_MAX = 8             # a shake of up to this many shrunk pixels either way is taken out...
SHAKE_GAIN = 0.8          # ...when lining it up leaves less than this share of the difference
SOFT_LUMA = 35.0          # a stage pixel lifted by less than this, every channel alike (within SOFT_SPREAD), is glow
SOFT_SPREAD = 12.0
RING_SHARE = 0.005         # glare over this share of the picture...
RING_WIDTH = 0.4          # ...spread over this share of its width is the ring or a flash: not used
GAIN_MAX = 0.12           # a stage this much darker or brighter than the plate is dimmed or flashed: not used
FLASH_LUMA = 10.0         # mean brightness change of the stage around the subject that counts as a flash or a dimmed stage (0-255)
PIECE_MIN = 0.06          # a piece of the subject smaller than this share of it is left out...
PIECE_NEAR = 0.04         # ...unless within this share of the picture's width of a larger piece
PEAK_SHARE = 0.95         # the first grab whose stable score reaches this share of the best: the peak
MIN_SCORE = 0.3           # a burst whose best subject is smaller than this share of the standing fighter shows nothing
MIN_AFTER = 12            # fight frames after the press before a grab may be chosen
BOX_TRIM = 0.005          # the subject box leaves out this share of its pixels on each side
FILL = 0.85               # the subject fills this share of the picture's height (SF4's Ultra pictures)
MIN_HEIGHT = 0.3          # the window is at least this share of the frame high


@dataclass
class Grab:
    """One burst grab's scores."""
    stage: float = 0.0     # share of the stage around the subject still differing from the plate
    cover: float = 1.0     # share of the picture the plate reaches
    gain: float = 1.0      # the stage's brightness against the plate's
    shake: tuple[int, int] = (0, 0)  # the shake taken out after the warp (shrunk pixels)
    ring: bool = False     # the activation's white ring (or a white flash) across the picture
    flash: bool = False
    score: float = 0.0     # the subject's size against the standing fighter's
    usable: bool = False
    mask: np.ndarray | None = field(default=None, repr=False)  # the subject, shrunk by DOWN


def shrink(rgb: np.ndarray, down: int = DOWN) -> np.ndarray:
    h, w = rgb.shape[0] // down * down, rgb.shape[1] // down * down
    return rgb[:h, :w].reshape(h // down, down, w // down, down, -1).astype(np.float32).mean(axis=(1, 3))


def luma(rgb: np.ndarray) -> np.ndarray:
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def differs(a: np.ndarray, b: np.ndarray, threshold: float = DIFF) -> np.ndarray:
    return np.abs(a - b).max(axis=-1) > threshold


def _is_red(rgb: np.ndarray) -> np.ndarray:
    return (rgb[..., 0] > 2 * rgb[..., 1] + 20) & (rgb[..., 0] > 2 * rgb[..., 2] + 20)


def _shift(img: np.ndarray, dy: int, dx: int) -> tuple[np.ndarray, np.ndarray]:
    """`img` moved by (dy, dx), and where it has pixels from inside it."""
    out = np.roll(img, (dy, dx), axis=(0, 1))
    inside = np.ones(img.shape[:2], dtype=bool)
    if dy > 0:
        inside[:dy] = False
    elif dy < 0:
        inside[dy:] = False
    if dx > 0:
        inside[:, :dx] = False
    elif dx < 0:
        inside[:, dx:] = False
    return out, inside


def shake(plate: np.ndarray, valid: np.ndarray, grab: np.ndarray) -> tuple[int, int]:
    """(dy, dx) that lines the seen plate up with the grab on the top BAND of the picture."""
    band = max(8, int(grab.shape[0] * BAND))
    pl, gl = luma(plate[: band + SHAKE_MAX]), luma(grab[:band])
    ok = valid[: band + SHAKE_MAX]

    def error(dy: int, dx: int) -> float:
        q, inside = _shift(pl, dy, dx)
        m = (_shift(ok, dy, dx)[0] & inside)[:band]
        return float(np.abs(q[:band] - gl)[m].mean()) if m.sum() > 0.5 * m.size else float("inf")

    still = error(0, 0)
    best = min(((error(dy, dx), dy, dx) for dy in range(-SHAKE_MAX, SHAKE_MAX + 1)
                for dx in range(-SHAKE_MAX, SHAKE_MAX + 1)), key=lambda e: e[0])
    return (best[1], best[2]) if best[0] < SHAKE_GAIN * still else (0, 0)


def _erode(m: np.ndarray) -> np.ndarray:
    er = np.zeros_like(m)
    er[1:-1, 1:-1] = m[1:-1, 1:-1] & m[:-2, 1:-1] & m[2:, 1:-1] & m[1:-1, :-2] & m[1:-1, 2:]
    return er


def _dilate(m: np.ndarray, times: int = 1) -> np.ndarray:
    for _ in range(times):
        d = m.copy()
        d[1:, :] |= m[:-1, :]
        d[:-1, :] |= m[1:, :]
        d[:, 1:] |= m[:, :-1]
        d[:, :-1] |= m[:, 1:]
        m = d
    return m


def clean(mask: np.ndarray) -> np.ndarray:
    """`mask` without its specks and thin lines (an erosion, then a dilation): a stage line a pixel
    off after the warp is not the subject."""
    return _dilate(_erode(mask))


def pieces(mask: np.ndarray) -> list[np.ndarray]:
    """The mask's connected pieces (8-neighbour), largest first, each as (N, 2) row/column indices."""
    h, w = mask.shape
    seen = np.zeros(mask.shape, dtype=bool)
    out = []
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        stack, members = [(int(y0), int(x0))], []
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            members.append((y, x))
            for ny in (y - 1, y, y + 1):
                for nx in (x - 1, x, x + 1):
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        out.append(np.array(members))
    out.sort(key=len, reverse=True)
    return out


def subject(mask: np.ndarray) -> np.ndarray:
    """The subject from a cleaned difference: the largest piece, every piece at least PIECE_MIN of
    the whole, and smaller ones close to a piece kept (sparks around the fighter); the rest (the
    cloth a scaled-away fighter leaves where it stood, a stray line) left out."""
    found = pieces(mask)
    if not found:
        return mask
    total = sum(len(p) for p in found)
    near = max(2, int(PIECE_NEAR * mask.shape[1]))
    kept = np.zeros(mask.shape, dtype=bool)
    small = []
    for p in found:
        if p is found[0] or len(p) >= PIECE_MIN * total:
            kept[p[:, 0], p[:, 1]] = True
        else:
            small.append(p)
    zone = _dilate(kept, near)
    for p in small:
        if zone[p[:, 0], p[:, 1]].any():
            kept[p[:, 0], p[:, 1]] = True
    return kept


class Stage:
    """The plate put back in the world (its depth through its camera), to be seen through each
    grab's camera."""

    def __init__(self, plate: np.ndarray, depth: np.ndarray, camera: bc.Camera, near: float = bc.NEAR):
        small = shrink(plate, SOURCE_DOWN)
        h, w = small.shape[:2]
        full = bc.fit_depth(depth, plate.shape[0], plate.shape[1])
        d = full[: h * SOURCE_DOWN: SOURCE_DOWN, : w * SOURCE_DOWN: SOURCE_DOWN]
        self.points = bc.unproject(camera, d, near)
        self.colours = small

    def seen(self, camera: bc.Camera, oh: int, ow: int) -> tuple[np.ndarray, np.ndarray]:
        return bc.warp_points(self.points, self.colours, camera, oh, ow)


def score_grab(stage: Stage, grab: np.ndarray, camera: bc.Camera, area: int) -> Grab:
    """One burst grab (shrunk by DOWN) through `camera`, against the stage; `area` the standing
    fighter's size (shrunk pixels)."""
    h, w = grab.shape[:2]
    plate, valid = stage.seen(camera, h, w)
    # A V-Trigger's activation shakes the picture without the camera the game reports: the shake
    # is found on the wall above the fighters (the placing within SHAKE_MAX that matches it best,
    # when clearly better than none) and taken out.
    dy, dx = shake(plate, valid, grab)
    if dy or dx:
        plate, inside = _shift(plate, dy, dx)
        valid = _shift(valid, dy, dx)[0] & inside
    g = Grab(cover=float(valid.mean()), shake=(dy, dx))
    # The stage dimmed (a V-Trigger's activation darkens it) or lit up as a whole: its brightness
    # against the plate's (the median, the subject being the smaller part).
    if valid.any():
        g.gain = float(np.median(((luma(grab) + 1) / (luma(plate) + 1))[valid]))
        plate = plate * g.gain
    # A V-Trigger's activation draws the same white ring for every fighter, larger than most of
    # them: near-white glare is neither the subject nor a changed stage.
    core = (luma(grab) > GLARE_LUMA) & ((grab.max(axis=-1) - grab.min(axis=-1)) < GLARE_SPREAD)
    cols = np.nonzero(core.any(axis=0))[0]
    # The ring itself (or a white flash across the screen): a picture of the activation, not of the
    # fighter, and its glow lights the whole wall.
    g.ring = bool(core.mean() > RING_SHARE and cols.size and (cols[-1] - cols[0]) > RING_WIDTH * w)
    glare = _dilate(core, 3)  # and its halo
    # The glow such light leaves on the wall (and dust): the stage a little brighter, its colour kept.
    lift = grab - plate
    soft = ((luma(grab) - luma(plate)) < SOFT_LUMA) & (lift.min(axis=-1) > -4) & ((lift.max(axis=-1) - lift.min(axis=-1)) < SOFT_SPREAD)
    # The training stage's red lines change brightness through a move: red in both is the line.
    # (a shake moves them a few pixels: red in the grab next to the plate's line is the line too).
    plate_red = _is_red(plate)
    red = _dilate(plate_red, 1) | (_is_red(grab) & _dilate(plate_red, 5))
    cleaned = clean(differs(grab, plate) & valid & ~glare & ~soft & ~red)
    g.mask = subject(cleaned)
    around = valid & ~_dilate(g.mask, 8) & ~glare
    if around.any():
        g.stage = float(cleaned[around].mean())
        g.flash = float(np.abs(luma(grab[around]) - luma(plate[around])).mean()) > FLASH_LUMA
    else:
        g.stage = 1.0
    g.flash = g.flash or abs(g.gain - 1) > GAIN_MAX or g.ring
    g.score = float(g.mask.sum()) / max(area, 1)
    g.usable = (not g.flash and g.stage <= STAGE_MAX and g.cover >= COVER_MIN
                and float(g.mask.sum()) <= SUBJECT_MAX * max(int(valid.sum()), 1))
    return g


def score_move(plate: np.ndarray, depth: np.ndarray, plate_camera: bc.Camera, pre: np.ndarray, pre_camera: bc.Camera,
               grabs: list[np.ndarray], cameras: list[bc.Camera]) -> list[Grab]:
    """Every burst grab scored (full-size pictures; the plate's depth; each picture's camera)."""
    stage = Stage(plate, depth, plate_camera)
    standing = score_grab(stage, shrink(pre), pre_camera, 1)
    area = max(int(standing.mask.sum()), 1)
    return [score_grab(stage, shrink(g), c, area) for g, c in zip(grabs, cameras)]


def choose(scores: list[Grab], times: list[int] | None = None, min_after: int = MIN_AFTER,
           peak_share: float = PEAK_SHARE, min_score: float = MIN_SCORE) -> int | None:
    """The chosen grab's index, or None (see the module's text). `times` are the grabs' fight
    frames after the press; grabs before `min_after` are never chosen (the move has not shown yet)."""
    value = [g.score if g.usable and (times is None or times[i] >= min_after) else 0.0 for i, g in enumerate(scores)]
    best = max(value, default=0.0)
    if best < min_score:
        return None
    return next(i for i, v in enumerate(value) if v >= peak_share * best)


def subject_box(g: Grab, trim: float = BOX_TRIM) -> tuple[int, int, int, int] | None:
    """Full size: (y0, y1, x0, x1) of the chosen grab's subject, the outermost `trim` share of its
    pixels on each side left out. None when there is none."""
    if g.mask is None:
        return None
    ys, xs = np.nonzero(g.mask)
    if ys.size < 20:
        return None
    y0, y1 = np.quantile(ys, [trim, 1 - trim])
    x0, x1 = np.quantile(xs, [trim, 1 - trim])
    return int(y0) * DOWN, (int(y1) + 1) * DOWN, int(x0) * DOWN, (int(x1) + 1) * DOWN


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
#
# A synthetic room: a back wall and a floor seen by a camera that pans with the fighter; the plate,
# its depth and the grabs are drawn by projecting the room, so the warp is tested as the game uses it.

def _room(cam: bc.Camera, h: int, w: int, seed: int = 1) -> tuple[np.ndarray, np.ndarray]:
    """The room through `cam`: colour (a grid on the wall and the floor, a red line) and depth."""
    eye, forward, right, up = cam.axes()
    tx, ty = cam.tans()
    xs = ((np.arange(w) + 0.5) / w) * 2 - 1
    ys = 1 - ((np.arange(h) + 0.5) / h) * 2
    dirs = forward + xs[None, :, None] * tx * right + ys[:, None, None] * ty * up
    # wall: y = -300 (the camera looks along -y); floor: z = 0
    with np.errstate(divide="ignore", invalid="ignore"):
        t_wall = (-300.0 - eye[1]) / dirs[..., 1]
        t_floor = (0.0 - eye[2]) / dirs[..., 2]
    t_floor = np.where(t_floor > 0, t_floor, np.inf)
    t = np.minimum(np.where(t_wall > 0, t_wall, np.inf), t_floor)
    pts = eye + t[..., None] * dirs
    on_floor = t_floor <= t
    u = np.where(on_floor, pts[..., 0], pts[..., 0])
    v = np.where(on_floor, pts[..., 1], pts[..., 2])
    grid = ((np.mod(u, 40) < 2) | (np.mod(v, 40) < 2))
    rng = np.random.default_rng(seed)
    base = np.where(on_floor, 150.0, 115.0) + (np.floor(u / 40) * 7 + np.floor(v / 40) * 13) % 9
    img = np.dstack([base] * 3)
    img[grid] = 70.0
    img[np.abs(pts[..., 0]) < 3] = (200.0, 30.0, 30.0)  # the red centre line
    img += rng.normal(0, 2, size=img.shape)
    dist = t * (dirs @ forward)  # distance along the view
    depth = bc.NEAR / dist
    return np.clip(img, 0, 255).astype(np.float32), depth.astype(np.float32)


def _with_fighter(img: np.ndarray, cam: bc.Camera, x: float, extra: float = 0.0, arm: float = 0.0) -> np.ndarray:
    """`img` with a fighter (a box 60 wide, 180 high standing at world x, on the floor at y=0)
    drawn through `cam`, `extra` units of aura around it and an arm `arm` units long."""
    h, w = img.shape[:2]
    out = img.copy()

    def block(x0: float, x1: float, z0: float, z1: float, colour) -> None:
        corners = np.array([[x0, 0.0, z0], [x1, 0.0, z1]])
        col, row, _ = bc.project(cam, corners, h, w)
        c0, c1 = sorted((int(col[0]), int(col[1])))
        r0, r1 = sorted((int(row[0]), int(row[1])))
        out[max(r0, 0):max(r1, 0), max(c0, 0):max(c1, 0)] = colour

    if extra:
        block(x - extra, x + 60 + extra, 0.0, 180 + extra, (90, 160, 255))
    block(x, x + 60, 0.0, 180.0, (230, 190, 150))
    if arm:
        block(x + 60, x + 60 + arm, 120.0, 135.0, (230, 190, 150))
    return out


def _cam(x: float) -> bc.Camera:
    return bc.Camera((x, 0.0, 120.0), (x, 400.0, 100.0), 65.0)


def selftest() -> int:
    failures = []

    def check(ok: bool, what: str) -> None:
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    h, w = 540, 960
    cam0 = _cam(0.0)
    plate, depth = _room(cam0, h, w)
    pre = _with_fighter(plate, cam0, -30)

    def frame(x_cam: float, x: float, **kw) -> tuple[np.ndarray, bc.Camera]:
        cam = _cam(x_cam)
        room, _ = _room(cam, h, w)
        return _with_fighter(room, cam, x, **kw), cam

    # The plate through a panned camera lines up with the stage the grab sees.
    g, cam = frame(60.0, 30)
    stage = Stage(plate, depth, cam0)
    seen, valid = stage.seen(cam, h // DOWN, w // DOWN)
    room_small = shrink(_room(cam, h, w)[0])
    off = differs(room_small, seen) & valid
    check(float(off.mean()) < 0.03 and float(valid.mean()) > 0.8,
          f"the plate seen through a panned camera matches the stage ({off.mean():.3f} differs, {valid.mean():.2f} reached)")

    # A dash: the fighter only changes place (the camera follows) - the same score as standing.
    still = score_move(plate, depth, cam0, pre, cam0, [g], [cam])[0]
    check(still.usable and 0.8 < still.score < 1.25, f"a change of place scores as standing ({still.score:.2f})")

    # A pose that spreads and an aura score more.
    g2, cam2 = frame(60.0, 30, arm=90.0)
    g3, cam3 = frame(60.0, 30, extra=40.0)
    sc = score_move(plate, depth, cam0, pre, cam0, [g2, g3], [cam2, cam3])
    check(sc[0].score > still.score * 1.08 and sc[1].score > still.score * 1.8,
          f"a reaching arm and an aura score more ({sc[0].score:.2f}, {sc[1].score:.2f})")

    # A flash (the whole stage brighter) is not used.
    flash = np.clip(g + 60, 0, 255)
    check(not score_move(plate, depth, cam0, pre, cam0, [flash], [cam])[0].usable, "a flash is not used")

    dim = g * 0.7
    check(not score_move(plate, depth, cam0, pre, cam0, [dim], [cam])[0].usable, "a dimmed stage is not used")

    # The choice: the held aura, not its first rise; nothing before MIN_AFTER.
    burst, cams = [], []
    for e in (0, 10, 25, 40, 40, 40, 40, 20, 0):
        img, c = frame(60.0, 30, extra=float(e))
        burst.append(img)
        cams.append(c)
    times = [4, 8, 12, 16, 20, 24, 28, 32, 36]
    scores = score_move(plate, depth, cam0, pre, cam0, burst, cams)
    chosen = choose(scores, times)
    check(chosen == 3, f"the aura at its widest is chosen, not its rise (chose {chosen})")
    early = [burst[3]] + burst[:1] * 4
    early_scores = score_move(plate, depth, cam0, pre, cam0, early, [cams[3]] + cams[:1] * 4)
    check(choose(early_scores, [4, 12, 16, 20, 24], min_score=0.1) != 0, "a grab 4 frames after the press is not chosen")
    check(choose(score_move(plate, depth, cam0, pre, cam0, [pre] * 5, [cam0] * 5)) is not None,
          "a fighter standing still is still a subject (the default frame is for nothing usable)")

    # The box and the window.
    box = subject_box(scores[chosen])
    x0, y0, cw, ch = frame_window(box, h, w)
    fits = box is not None and x0 <= box[2] and y0 <= box[0] and x0 + cw >= box[3] and y0 + ch >= box[1]
    check(fits and abs(cw / ch - 16 / 9) < 0.01 and (box[1] - box[0]) / ch > 0.75,
          f"the window is 16:9, holds the subject and the subject fills most of its height ({box}, {cw}x{ch})")
    x, y, cw, ch = frame_window((900, 1080, 1700, 1920), 1080, 1920)
    check(x + cw <= 1920 and y + ch <= 1080 and x >= 0 and y >= 0, "a window at the picture's edge is kept inside it")

    # The cloth a scaled-away fighter leaves on the floor is not the subject.
    m = np.zeros((100, 200), dtype=bool)
    m[20:90, 40:70] = True
    m[86:90, 150:160] = True
    check(not subject(m)[88, 155] and subject(m)[50, 50], "a small piece far from the fighter is left out")
    m[60:64, 74:78] = True
    check(subject(m)[62, 76], "a spark next to the fighter is kept")

    print("SELFTEST " + ("FAILED: " + "; ".join(failures) if failures else "PASSED"))
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(__doc__)
