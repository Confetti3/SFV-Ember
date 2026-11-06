"""SFV Ember's brand art: Ember's mark in SFV Ember's colours, and the menu ground (requires Pillow and numpy).

  python scripts/build-sfv-brand.py [--ember-root DIR]

SFV Ember's mark is Ember's own, its shapes untouched (docs/design/EMBER_LIBRARY.md
section 16): the fire character in its brush circle, with the tail. Only its
colours change, to the Band theme's (SfvProduct.cxx): Ember's ivory becomes the
theme's text, its orange (the left dot and the tail) the theme's gold, and the
tail's light orange the gold's light step.

  art/brand/sfv-ember-mark.svg                 the mark's source, Ember's SVG with its fills swapped
  art/brand/sfv-ember-mark.png                 its export, 1024 x 1024 RGBA
  art/brand/sfv-ember-mark.rgba                128 x 128 straight RGBA, embedded in the font atlas
  assets/brand/sfv-ember-menu-background.png   the menu's ground: flat ink, no picture

The exports are recoloured from Ember's own export (src/ui/ember.png in the
Ember library): each pixel keeps its alpha, and its colour moves by its weights
over Ember's three fills, so edges and texture stay as they are. The ground is
the theme's ink: SFV Ember draws no picture behind its menus.

--ember-root is the Ember library checkout (default: SFVE_EMBER_ROOT).
Normal builds use the checked-in exports and need neither Python nor Pillow.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

# Ember's fills and the Band theme's colours that replace them.
SWAP = {"F3EBDD": "EDECE0",   # ivory -> the theme's text
        "FF8738": "DDB961",   # orange -> the theme's gold (its accent)
        "F3C58B": "F2DD9D"}   # the tail's light orange -> the gold's light step
GROUND = "100F0D"             # the theme's ink


def rgb(hex6: str) -> np.ndarray:
    return np.array([int(hex6[i:i + 2], 16) for i in (0, 2, 4)], np.float64)


def recolour(image: Image.Image) -> Image.Image:
    """Each pixel's colour as weights over Ember's three fills (summing to 1,
    least squares), its offset from that blend kept, and the weights applied
    to the replacement colours."""
    a = np.asarray(image.convert("RGBA"), np.float64)
    src = np.stack([rgb(k) for k in SWAP]); dst = np.stack([rgb(v) for v in SWAP.values()])
    px = a[..., :3].reshape(-1, 3)
    base = src[2]
    basis = np.stack([src[0] - base, src[1] - base])
    w2, *_ = np.linalg.lstsq(basis.T, (px - base).T, rcond=None)
    w2 = np.clip(w2.T, 0, 1)
    w = np.concatenate([w2, np.clip(1 - w2.sum(1, keepdims=True), 0, 1)], 1)
    w /= w.sum(1, keepdims=True)
    offset = px - w @ src
    a[..., :3] = (w @ dst + offset).clip(0, 255).reshape(a.shape[:2] + (3,))
    return Image.fromarray((a + .5).astype(np.uint8), "RGBA")


def premultiplied_resize(image: Image.Image, size: int) -> Image.Image:
    """Lanczos without dark fringes: resize premultiplied, then straighten."""
    a = np.asarray(image, dtype=np.float32) / 255
    a[..., :3] *= a[..., 3:4]
    channels = [Image.fromarray(a[..., c]).resize((size, size), Image.Resampling.LANCZOS) for c in range(4)]
    out = np.stack([np.asarray(c) for c in channels], axis=-1).clip(0, 1)
    alpha = out[..., 3:4]
    out[..., :3] = np.where(alpha > 1e-4, out[..., :3] / np.maximum(alpha, 1e-4), 0)
    return Image.fromarray((out.clip(0, 1) * 255 + .5).astype(np.uint8), "RGBA")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--ember-root", type=Path, default=os.environ.get("SFVE_EMBER_ROOT"))
    a = p.parse_args()
    if not a.ember_root:
        raise SystemExit("name the Ember library checkout with --ember-root or SFVE_EMBER_ROOT")
    art, assets = ROOT / "art" / "brand", ROOT / "assets" / "brand"
    art.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    svg = (a.ember_root / "assets" / "brand" / "source" / "ember-mark.svg").read_text(encoding="utf-8")
    for old, new in SWAP.items():
        svg = svg.replace(f"#{old}", f"#{new}").replace(f"#{old.lower()}", f"#{new}")
    (art / "sfv-ember-mark.svg").write_text(svg, encoding="utf-8", newline="\n")
    with Image.open(a.ember_root / "src" / "ui" / "ember.png") as source:
        mark = recolour(source)
    premultiplied_resize(mark, 1024).save(art / "sfv-ember-mark.png", optimize=True)
    # As scripts/build-ember-brand.py makes Ember's: a plain Lanczos resize of the export.
    (art / "sfv-ember-mark.rgba").write_bytes(mark.resize((128, 128), Image.Resampling.LANCZOS).tobytes())
    Image.new("RGB", (2048, 1152), tuple(int(v) for v in rgb(GROUND))).save(assets / "sfv-ember-menu-background.png", optimize=True)
    print("written under", ROOT)


if __name__ == "__main__":
    main()
