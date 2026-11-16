"""The picture pack: masters converted to the cutouts and loadout pictures of pack.md, and the
manifest that lists them."""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .imageops import alpha_crop, fit
from .space import (LOADOUT_PACK_SIZE, PACK_COLOURS, PACK_MARGIN, PACK_SIZE, PART_NAMES, PIPELINE, BoothError,
                    cutout_relpath, loadout_master_path, loadout_relpath, master_path)


def pack_cutout(master: Path, target: Path) -> None:
    """A master as SF4's importer makes a render into a selection cutout: cropped to the alpha
    box, fitted 256x384 with 16 px margin bottom-centred (premultiplied-alpha downscale), 256
    colours with transparency."""
    with Image.open(master) as im:
        canvas = fit(alpha_crop(im.convert("RGBA")), PACK_SIZE, PACK_MARGIN)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(".tmp.png")
    canvas.quantize(colors=PACK_COLOURS, method=Image.Quantize.FASTOCTREE,
                    dither=Image.Dither.FLOYDSTEINBERG).save(temp, optimize=True)
    os.replace(temp, target)


def pack_loadout(master: Path, target: Path) -> None:
    """A loadout master as SF4 makes its Ultra pictures (the library's fetch-ultra-references.py):
    ImageOps.fit to 256x144 with Lanczos, saved as an RGB PNG (not quantized); optimize=True
    only shrinks the file, losslessly."""
    with Image.open(master) as im:
        rgb = ImageOps.fit(im.convert("RGB"), LOADOUT_PACK_SIZE, Image.Resampling.LANCZOS)
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_suffix(".tmp.png")
    rgb.save(temp, "PNG", optimize=True)
    os.replace(temp, target)


def _pack_one(job: tuple[str, str, str]) -> None:  # module level so a process pool can run it
    kind, source, target = job
    (pack_cutout if kind == "cutout" else pack_loadout)(Path(source), Path(target))


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_meta(master: Path, default_frame: int, default_stage: str) -> tuple[int, str]:
    try:
        meta = json.loads(master.with_suffix(".json").read_text(encoding="utf-8"))
        return int(meta.get("frame", default_frame)), str(meta.get("stage", default_stage))
    except (OSError, ValueError, TypeError):
        return default_frame, default_stage


def failed_looks(store: Path) -> dict[tuple[str, int, int], str]:
    """The looks the capture log holds as failed (the latest reason) whose master is still missing."""
    out: dict[tuple[str, int, int], str] = {}
    log = store / "capture-log.jsonl"
    if not log.is_file():
        return out
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            entry = json.loads(line)
            look = entry["look"]
            key = (look["fighter"], int(look["costume"]), int(look["colour"]))
        except (ValueError, KeyError, TypeError):
            continue  # a loadout failure, or a damaged line
        out[key] = str(entry.get("reason", "capture failed"))
    return {k: v for k, v in out.items() if not master_path(store, *k).is_file()}


def build_pack(store: Path, space: dict, mod_revision: str | None, except_failed: bool, default_frame: int,
               default_stage: str, jobs: int, force: bool = False) -> dict:
    """Converts every master into the pack and writes manifest.json. Returns the manifest."""
    pack = store / "pack"
    work: list[tuple[str, str, str]] = []
    files: list[dict] = []
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            for colour in colours:
                master = master_path(store, code, costume, colour)
                if not master.is_file():
                    continue
                rel = cutout_relpath(code, costume, colour)
                target = pack / rel
                if force or not target.is_file() or target.stat().st_mtime < master.stat().st_mtime:
                    work.append(("cutout", str(master), str(target)))
                frame, stage = read_meta(master, default_frame, default_stage)
                files.append({"path": rel, "look": {"fighter": code, "costume": costume, "colour": colour},
                              "frame": frame, "stage": stage})
        for prefix, part in PART_NAMES.items():
            for choice in range(1, fighter[prefix] + 1):
                master = loadout_master_path(store, code, prefix, choice)
                if not master.is_file():
                    continue
                rel = loadout_relpath(code, prefix, choice)
                target = pack / rel
                if force or not target.is_file() or target.stat().st_mtime < master.stat().st_mtime:
                    work.append(("loadout", str(master), str(target)))
                frame, stage = read_meta(master, default_frame, default_stage)
                files.append({"path": rel, "look": {"fighter": code, "part": part, "choice": choice},
                              "frame": frame, "stage": stage})
    if jobs > 1 and len(work) > 8:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=jobs) as pool:
            list(pool.map(_pack_one, work, chunksize=16))
    else:
        for job in work:
            _pack_one(job)
    # A stale file of an earlier run whose master is gone is not in the manifest, so the package
    # would refuse it: remove what no master produces.
    wanted = {(pack / f["path"]).resolve() for f in files}
    sel = pack / "assets" / "selection"
    if sel.is_dir():
        for stale in [p for p in sel.rglob("*") if p.is_file() and p.resolve() not in wanted]:
            stale.unlink()
    for f in files:
        path = pack / f["path"]
        f["sha256"] = sha256_file(path)
        f["bytes"] = path.stat().st_size
    files = [dict((k, f[k]) for k in ("path", "sha256", "bytes", "look", "frame", "stage")) for f in files]
    exceptions = []
    if except_failed:
        have = {(f["look"]["fighter"], f["look"].get("costume"), f["look"].get("colour")) for f in files}
        for (code, costume, colour), reason in sorted(failed_looks(store).items()):
            fighter = space["fighters"].get(code)
            if fighter and colour in fighter["costumes"].get(costume, []) and (code, costume, colour) not in have:
                exceptions.append({"fighter": code, "costume": costume, "colour": colour, "reason": reason[:200]})
    manifest = {"format": 1, "gameBuild": space["gameBuild"], "modRevision": mod_revision or find_mod_revision(),
                "pipeline": PIPELINE, "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "exceptions": exceptions, "files": files}
    if not manifest["modRevision"]:
        raise BoothError("no mod revision: pass --mod-revision or build the mod (build-provenance.json)")
    pack.mkdir(parents=True, exist_ok=True)
    (pack / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return manifest


def find_mod_revision() -> str | None:
    """sourceRevision of the build receipt (build-provenance.json in the build tree)."""
    for folder in (os.environ.get("SFVE_BUILD_DIR"), r"D:\sfv-ember-build"):
        if not folder:
            continue
        try:
            receipt = json.loads((Path(folder) / "build-provenance.json").read_text(encoding="utf-8-sig"))
            rev = str(receipt["sourceRevision"]).lower()
            if re.fullmatch(r"[0-9a-f]{7,40}", rev):
                return rev
        except (OSError, ValueError, KeyError):
            continue
    return None
