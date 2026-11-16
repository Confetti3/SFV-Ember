"""Picture operations the booth modules share: the 3x3-style morphology on a mask, connected
components, the shrunk comparison of two grabs, fitting a cutout to a canvas."""

from __future__ import annotations

import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageFilter
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .grabs import read_colour
from .space import BoothError


ALPHA_BOX_MIN = 8           # alpha at or above this counts as the picture (the importer's rule)


def morph(mask: np.ndarray, kind: str, size: int) -> np.ndarray:
    im = Image.fromarray((mask.astype(np.uint8)) * 255, "L")
    im = im.filter(ImageFilter.MaxFilter(size) if kind == "dilate" else ImageFilter.MinFilter(size))
    return np.asarray(im) > 127


def keep_components(mask: np.ndarray, min_share: float = 0.02, cell: int = 4) -> np.ndarray:
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
    keep_small = morph(keep_small, "dilate", 3)  # a one-cell skirt so edges are not clipped
    full = np.zeros(mask.shape, dtype=bool)
    full[: hh * cell, : ww * cell] = np.repeat(np.repeat(keep_small, cell, axis=0), cell, axis=1)
    return mask & full


CHECK_DOWN = 4             # the checks compare pictures shrunk 4x (480x270 from 1920x1080)


CHECK_DIFF = 18            # a pixel differs when a channel differs by more than this (0-255)


def shrunk(path: Path | str, down: int = CHECK_DOWN) -> np.ndarray:
    rgb = read_colour(path)
    h, w = rgb.shape[0] // down * down, rgb.shape[1] // down * down
    return rgb[:h, :w].reshape(h // down, down, w // down, down, 3).astype(np.float32).mean(axis=(1, 3))


def differs(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.abs(a - b).max(axis=-1) > CHECK_DIFF


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
