"""The pick space, the path rules (pack.md) and the store layout of the booth tools; the sizes
and names every other booth module shares."""

from __future__ import annotations

import collections
import json
import re
from pathlib import Path


VERSION = "1.0"


PIPELINE = f"runbooth {VERSION} / boothpack {VERSION}"


DEFAULT_STORE = r"D:\sfv-ember-renders"


DEFAULT_PICK_SPACE = Path(__file__).resolve().parent.parent.parent / "data" / "pick-space.json"


MASTER_SIZE = (512, 768)


MASTER_MARGIN = 32


PACK_SIZE = (256, 384)


PACK_MARGIN = 16            # the same canvas and margin as SF4's importer, at half the master


PACK_COLOURS = 256


LOADOUT_PACK_SIZE = (256, 144)  # SF4's ultra-N.png: 256x144 RGB PNG, ImageOps.fit with Lanczos, no palette


PART_NAMES = {"vs": "vskill", "vt": "vtrigger"}


class BoothError(RuntimeError):
    """A grab pair that cannot become a master (the reason goes to the capture log)."""


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


_NUM = r"(0|[1-9][0-9]{0,5})"


CUTOUT_RE = re.compile(r"assets\\selection\\([^\\]+)\\costume-" + _NUM + r"\\color-" + _NUM + r"-cutout\.png")


LOADOUT_RE = re.compile(r"assets\\selection\\([^\\]+)\\(vs|vt)" + _NUM + r"\.png")


MANIFEST_PACKAGE_PATH = "assets\\selection\\manifest.json"


def is_selection_asset_path(path: str, space: dict) -> bool:
    """Exactly what IsSfvSelectionAssetPath accepts: `path` relative to the package folder,
    backslash separated, case-exact; the manifest, a cutout of a look the pick space has, or a
    loadout picture k within the fighter's V-Skill / V-Trigger count."""
    if path == MANIFEST_PACKAGE_PATH:
        return True
    m = CUTOUT_RE.fullmatch(path)
    if m:
        fighter = space["fighters"].get(m.group(1))
        if fighter is None:
            return False
        colours = fighter["costumes"].get(int(m.group(2)))
        return colours is not None and int(m.group(3)) in colours
    m = LOADOUT_RE.fullmatch(path)
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


def master_path(store: Path, code: str, costume: int, colour: int) -> Path:
    return store / "masters" / code / f"costume-{costume}" / f"color-{colour}.png"


def loadout_master_path(store: Path, code: str, part: str, choice: int) -> Path:
    return store / "masters" / code / f"{part}{choice}.png"
