"""The pack's palette pictures: each in the fewest colours that keep it as close to its source as
boothpack 1.0's 256-colour octree palette kept it, with no dithering (dither noise is what a PNG
compresses worst). The palette is a weighted k-means in premultiplied RGBA from k-means++ seeds,
so an edge pixel counts as much as its alpha shows; a fixed seed makes every run give the same
bytes. numpy and Pillow only: no quantizing library the tool environments may lack."""

from __future__ import annotations

import io
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")


# The colour counts a cutout may get, fewest first; a cutout that none of them keeps as close as
# the octree palette did is packed as boothpack 1.0 packed it.
CUTOUT_STEPS = (32, 40, 48, 56, 64, 80, 96, 128, 160, 192, 256)


# A loadout picture (a game shot, no transparency) in 256 colours: about 44 dB from its source.
LOADOUT_COLOURS = 256


KMEANS_ROUNDS = 12


def _premultiplied(rgba: np.ndarray) -> np.ndarray:
    rgba = rgba.astype(np.float64)
    return np.concatenate([rgba[..., :3] * (rgba[..., 3:4] / 255), rgba[..., 3:4]], -1)


def _nearest(points: np.ndarray, centres: np.ndarray) -> np.ndarray:
    distance = (points * points).sum(1)[:, None] - 2 * points @ centres.T + (centres * centres).sum(1)[None]
    return distance.argmin(1)


def _kmeans(points: np.ndarray, weights: np.ndarray, count: int) -> np.ndarray:
    """`count` centres of the weighted `points`: k-means++ seeds, then KMEANS_ROUNDS of Lloyd."""
    rng = np.random.default_rng(0)
    centres = [points[int(np.argmax(weights))]]
    gap = ((points - centres[0]) ** 2).sum(1)
    while len(centres) < count:
        mass = weights * gap
        if not mass.sum() > 0:
            break  # every colour is a centre already
        pick = int(rng.choice(len(points), p=mass / mass.sum()))
        centres.append(points[pick])
        gap = np.minimum(gap, ((points - points[pick]) ** 2).sum(1))
    centres = np.array(centres)
    for _ in range(KMEANS_ROUNDS):
        owner = _nearest(points, centres)
        sums = np.zeros_like(centres)
        np.add.at(sums, owner, points * weights[:, None])
        held = np.bincount(owner, weights=weights, minlength=len(centres))
        live = held > 0
        centres[live] = sums[live] / held[live, None]
    return centres


def palette_picture(image: Image.Image, colours: int) -> Image.Image:
    """`image` as a palette picture of at most `colours` entries, every pixel its nearest entry (no
    dithering). Fully transparent pixels share one transparent entry; the translucent entries come
    first, so the PNG's transparency chunk is as short as it can be, and an opaque picture has none."""
    rgba = np.asarray(image.convert("RGBA"))
    height, width = rgba.shape[:2]
    flat = rgba.reshape(-1, 4)
    shown = flat[:, 3] > 0
    clear = int(not shown.all())
    unique, owner, counts = np.unique(np.ascontiguousarray(flat[shown]).view(np.uint32).ravel(),
                                      return_inverse=True, return_counts=True)
    points = _premultiplied(unique.view(np.uint8).reshape(-1, 4))
    centres = _kmeans(points, counts.astype(np.float64), min(colours - clear, len(points)))
    # Back to straight alpha and rounded, as the entries are stored; each colour then takes the
    # stored entry nearest to it.
    alpha = np.clip(np.round(centres[:, 3]), 1, 255)
    straight = np.clip(np.round(centres[:, :3] * 255 / alpha[:, None]), 0, 255)
    live = np.unique(np.concatenate([straight, alpha[:, None]], 1).astype(np.uint8), axis=0)
    table = np.concatenate([np.zeros((clear, 4), np.uint8), live])
    order = np.argsort(table[:, 3] == 255, kind="stable")
    place = np.empty(len(order), np.int64)
    place[order] = np.arange(len(order))
    index = np.full(height * width, place[0], np.int64)
    index[shown] = place[_nearest(points, _premultiplied(live)) + clear][owner.ravel()]
    stored = table[order]
    picture = Image.fromarray(index.reshape(height, width).astype(np.uint8), "P")
    picture.putpalette(stored[:, :3].ravel().tolist())
    translucent = np.nonzero(stored[:, 3] < 255)[0]
    if len(translucent):
        picture.info["transparency"] = bytes(stored[: translucent.max() + 1, 3].tolist())
    return picture


def png_bytes(picture: Image.Image) -> bytes:
    """The picture as a PNG at the strongest zlib setting Pillow has."""
    out = io.BytesIO()
    extra = {"transparency": picture.info["transparency"]} if "transparency" in picture.info else {}
    picture.save(out, "PNG", optimize=True, **extra)
    return out.getvalue()


def closeness(source: Image.Image, data: bytes) -> float:
    """How close the PNG `data` decodes to `source`: PSNR in dB over premultiplied RGBA."""
    with Image.open(io.BytesIO(data)) as decoded:
        a, b = _premultiplied(np.asarray(source.convert("RGBA"))), _premultiplied(np.asarray(decoded.convert("RGBA")))
    return float(10 * np.log10(255 ** 2 / max(float(((a - b) ** 2).mean()), 1e-12)))


def octree_png(canvas: Image.Image) -> bytes:
    """A cutout as boothpack 1.0 packed it: Pillow's 256-colour octree palette."""
    return png_bytes(canvas.quantize(colors=256, method=Image.Quantize.FASTOCTREE))


def cutout_png(canvas: Image.Image) -> tuple[bytes, int]:
    """The cutout's PNG and its colour count: the fewest of CUTOUT_STEPS (a binary search, the
    closeness growing with the count) whose picture is as close to `canvas` as the octree palette's,
    or the octree palette's own (count 0) where none is or where it is the smaller file."""
    octree = octree_png(canvas)
    bar = closeness(canvas, octree)
    best: tuple[bytes, int] = (octree, 0)
    low, high = 0, len(CUTOUT_STEPS) - 1
    while low <= high:
        middle = (low + high) // 2
        data = png_bytes(palette_picture(canvas, CUTOUT_STEPS[middle]))
        if closeness(canvas, data) >= bar:
            best, high = (data, CUTOUT_STEPS[middle]), middle - 1
        else:
            low = middle + 1
    return best if len(best[0]) <= len(octree) else (octree, 0)


def loadout_png(picture: Image.Image) -> bytes:
    return png_bytes(palette_picture(picture.convert("RGB"), LOADOUT_COLOURS))
