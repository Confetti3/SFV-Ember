"""The stage plate seen through another camera: SFV's camera follows the fighters, so a grab taken
fight frames after the plate looks at the stage from elsewhere, and a wall far behind moves less
on the screen than the floor under the fighters. A 2D shift cannot line the plate up with it; the
plate's own depth and the two cameras can (every stage pixel is put back in the world and seen
through the grab's camera).

The camera is read through the pipe's `peek` (CameraUnit, the run the rollback restores, at +0x100:
CAMERA_PEEK): the point it looks at and its eye in the engine's units (z up), and the horizontal
field of view in degrees, for a 16:9 picture (as the hitbox viewer reads it, SfvBoxOffsets.hxx). The depth is the scene's (reversed Z, no
far plane: d = near / distance along the view).

  python -I tools/boothcamera.py --selftest
"""
from __future__ import annotations

import math
import struct
import sys
from dataclasses import dataclass

try:
    import numpy as np
except ImportError:  # pragma: no cover
    sys.exit("boothcamera needs numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

ASPECT = 16 / 9
# peek <region> <offset> <bytes>: the camera run (CameraUnit +0x100) from +0x170 on, ten floats.
CAMERA_PEEK = "peek CameraUnit[0]+0x100.presentation 112 40"
NEAR = 10.0   # the engine's near plane (UE4's default), in its units; reversed Z: depth = NEAR / distance


@dataclass
class Camera:
    target: tuple[float, float, float]
    eye: tuple[float, float, float]
    fov: float  # horizontal, degrees

    @staticmethod
    def from_reply(reply: str) -> "Camera | None":
        """From CAMERA_PEEK's reply ("ok" and ten hex words), or None."""
        words = reply.split()
        if not words or words[0] != "ok" or len(words) < 11:
            return None
        return Camera.from_words([struct.unpack("<f", struct.pack("<I", int(x, 16)))[0] for x in words[1:11]])

    def words(self) -> list[float]:
        return [*self.target, *self.eye, 0.0, 0.0, 0.0, self.fov]

    @staticmethod
    def from_words(values: list[float]) -> "Camera":
        """From the camera's floats: target (3), eye (3), and the field of view 6 floats later
        (CameraUnit +0x170, +0x17C, +0x194)."""
        return Camera(tuple(values[0:3]), tuple(values[3:6]), float(values[9]))

    def axes(self) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
        eye = np.asarray(self.eye, dtype=np.float64)
        forward = np.asarray(self.target, dtype=np.float64) - eye
        forward /= np.linalg.norm(forward)
        right = np.cross(np.array([0.0, 0.0, 1.0]), forward)
        right /= np.linalg.norm(right)
        up = np.cross(forward, right)
        return eye, forward, right, up

    def tans(self) -> tuple[float, float]:
        tx = math.tan(math.radians(self.fov) / 2)
        return tx, tx / ASPECT


def unproject(cam: Camera, depth: np.ndarray, near: float = NEAR) -> np.ndarray:
    """Every pixel of a depth picture (HxW, reversed Z) as a world point, HxWx3; NaN where the depth
    is 0 (the sky, nothing drawn)."""
    h, w = depth.shape
    eye, forward, right, up = cam.axes()
    tx, ty = cam.tans()
    xs = ((np.arange(w) + 0.5) / w) * 2 - 1
    ys = 1 - ((np.arange(h) + 0.5) / h) * 2
    with np.errstate(divide="ignore", invalid="ignore"):
        dist = np.where(depth > 0, near / depth, np.nan)
    px = xs[None, :] * tx * dist
    py = ys[:, None] * ty * dist
    return eye + dist[..., None] * forward + px[..., None] * right + py[..., None] * up


def project(cam: Camera, points: np.ndarray, h: int, w: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """World points (...x3) seen through `cam` in a h x w picture: column, row (floats) and the
    distance along the view (<= 0 behind the camera)."""
    eye, forward, right, up = cam.axes()
    tx, ty = cam.tans()
    rel = points - eye
    z = rel @ forward
    with np.errstate(divide="ignore", invalid="ignore"):
        x = (rel @ right) / (z * tx)
        y = (rel @ up) / (z * ty)
    return (x + 1) / 2 * w - 0.5, (1 - y) / 2 * h - 0.5, z


def warp_points(points: np.ndarray, colours: np.ndarray, cam_to: Camera, oh: int, ow: int) -> tuple[np.ndarray, np.ndarray]:
    """Stage points (HxWx3, `unproject`) and their colours (HxWxC) seen through `cam_to` in an
    oh x ow picture: each output pixel the mean of the points that land in it (a stage seen from
    nearly the same place never folds over itself). Returns the picture (float32) and where any
    point landed (elsewhere the plate never saw the stage: a wall come into view, the edge)."""
    col, row, z = project(cam_to, points, oh, ow)
    ok = np.isfinite(col) & np.isfinite(row) & (z > 0)
    ci = np.floor(np.where(ok, col, -1) + 0.5).astype(np.int64)
    ri = np.floor(np.where(ok, row, -1) + 0.5).astype(np.int64)
    ok &= (ci >= 0) & (ci < ow) & (ri >= 0) & (ri < oh)
    idx = (ri * ow + ci)[ok]
    rgb = colours.reshape(-1, colours.shape[-1])[ok.reshape(-1)].astype(np.float64)
    count = np.bincount(idx, minlength=oh * ow)
    out = np.zeros((oh * ow, rgb.shape[1]), dtype=np.float64)
    for c in range(rgb.shape[1]):
        out[:, c] = np.bincount(idx, weights=rgb[:, c], minlength=oh * ow)
    valid = count > 0
    out[valid] /= count[valid, None]
    return out.reshape(oh, ow, -1).astype(np.float32), valid.reshape(oh, ow)


def fit_depth(depth: np.ndarray, h: int, w: int) -> np.ndarray:
    """The depth picture at the colour picture's size: a depth target a few rows taller (SFV's
    960x544 for 960x540) is cut, one of another size picked from by nearest pixel."""
    dh, dw = depth.shape
    if dh >= h and dw >= w and dh - h <= 16 and dw - w <= 16:
        return depth[:h, :w]
    rows = np.minimum((np.arange(h) * dh / h).astype(int), dh - 1)
    cols = np.minimum((np.arange(w) * dw / w).astype(int), dw - 1)
    return depth[rows][:, cols]


def warp(plate: np.ndarray, depth: np.ndarray, cam_from: Camera, cam_to: Camera, down: int = 1,
         near: float = NEAR) -> tuple[np.ndarray, np.ndarray]:
    """The plate (HxWx3, its depth, taken through `cam_from`) as `cam_to` sees it, at 1/`down` of
    the size (warp_points)."""
    h, w = plate.shape[:2]
    points = unproject(cam_from, fit_depth(depth, h, w), near)
    return warp_points(points, plate, cam_to, h // down, w // down)


def selftest() -> int:
    failures = []

    def check(ok: bool, what: str) -> None:
        print(("ok   " if ok else "FAIL ") + what)
        if not ok:
            failures.append(what)

    # A wall 1000 units ahead and a floor: depth through one camera, the same world point found
    # through another.
    cam = Camera((1000.0, 0.0, 100.0), (0.0, 0.0, 100.0), 60.0)
    h, w = 90, 160
    tx, ty = cam.tans()
    xs = ((np.arange(w) + 0.5) / w) * 2 - 1
    ys = 1 - ((np.arange(h) + 0.5) / h) * 2
    dist = np.full((h, w), 1000.0)
    # the floor (z = 0) below the horizon: distance along the view where the ray meets it
    below = ys < 0
    floor = 100.0 / (-ys[below] * ty)
    dist[below] = np.minimum(dist[below], floor[:, None])
    depth = NEAR / dist
    pts = unproject(cam, depth)
    check(abs(pts[0, 80, 0] - 1000.0) < 1e-6, "a wall pixel is put back on the wall")
    check(abs(pts[-1, 80, 2]) < 1e-6, "a floor pixel is put back on the floor")
    col, row, _ = project(cam, pts, h, w)
    check(np.nanmax(np.abs(col - np.arange(w)[None, :])) < 1e-6 and np.nanmax(np.abs(row - np.arange(h)[:, None])) < 1e-6,
          "a point put back and seen again lands where it was")
    moved = Camera((1000.0, 50.0, 100.0), (0.0, 50.0, 100.0), 60.0)
    col2, _, _ = project(moved, pts, h, w)
    wall_shift = col2[5, 80] - col[5, 80]
    floor_shift = col2[-1, 80] - col[-1, 80]
    check(wall_shift < 0 and floor_shift < wall_shift, f"after a pan the floor moves more than the wall ({floor_shift:.1f}, {wall_shift:.1f})")
    plate = np.dstack([np.tile(np.arange(w, dtype=np.float32), (h, 1))] * 3)
    img, valid = warp(plate, depth, cam, cam, down=1)
    check(valid.all() and np.abs(img - plate).max() < 1e-3, "a plate warped to its own camera is itself")
    print("SELFTEST " + ("FAILED: " + "; ".join(failures) if failures else "PASSED"))
    return 1 if failures else 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest())
    sys.exit(__doc__)
