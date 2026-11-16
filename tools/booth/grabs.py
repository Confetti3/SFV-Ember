"""Reading a grab: its colour (a BMP) and its depth sidecar (`<path>.depth` with `<path>.json`)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")


DEPTH_DIFF_EPS = 1e-5       # depth difference between the two grabs


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
