"""Validation of a pack against pack.md's Read-RenderStore rules."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .pack import sha256_file
from .space import (CUTOUT_RE, LOADOUT_PACK_SIZE, LOADOUT_RE, MANIFEST_PACKAGE_PATH, PACK_SIZE, PART_NAMES,
                    is_selection_asset_path)


def image_problem(file: Path, kind: str) -> str | None:
    """Why `file` is not the picture pack.md describes, or None: a PNG that decodes, a cutout 256x384
    palette with transparency, a loadout picture 256x144 RGB. The sha256 and size in the manifest say
    the file is the one that was packed, not that it is an image."""
    try:
        with Image.open(file) as im:
            fmt, size, mode, transparent = im.format, im.size, im.mode, "transparency" in im.info
            im.verify()
    except (OSError, ValueError, SyntaxError) as e:  # Pillow: truncated, not an image, a bad chunk
        return f"not a readable picture ({e})"
    want_size, want_mode = (PACK_SIZE, "P") if kind == "cutout" else (LOADOUT_PACK_SIZE, "RGB")
    if fmt != "PNG":
        return f"is {fmt}, not PNG"
    if size != want_size:
        return f"is {size[0]}x{size[1]}, not {want_size[0]}x{want_size[1]}"
    if mode != want_mode:
        return f"is mode {mode}, not {want_mode}"
    if kind == "cutout" and not transparent:
        return "has no transparency"
    return None


def validate_pack(pack: Path, space: dict) -> tuple[list[str], list[str]]:
    """(errors, warnings) for `<store>\\pack`: the manifest and the files, against pack.md."""
    errors: list[str] = []
    warnings: list[str] = []
    try:
        manifest = json.loads((pack / "manifest.json").read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        return [f"manifest.json: {e}"], []
    if manifest.get("format") != 1 or isinstance(manifest.get("format"), bool):
        errors.append("format is not 1")
    if manifest.get("gameBuild") != space["gameBuild"]:
        errors.append(f"gameBuild {manifest.get('gameBuild')!r} is not the pick space's {space['gameBuild']!r}")
    if not isinstance(manifest.get("modRevision"), str) or not re.fullmatch(r"[0-9a-f]{7,40}", manifest["modRevision"]):
        errors.append("modRevision is not 7 to 40 lowercase hex")
    if not isinstance(manifest.get("pipeline"), str) or not manifest["pipeline"].strip():
        errors.append("pipeline is empty")
    if not isinstance(manifest.get("generatedAt"), str) or not re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", manifest["generatedAt"]):
        errors.append("generatedAt is not ISO 8601 UTC")
    try:
        manifest["pipeline"].encode("ascii")
        json.dumps(manifest).encode("ascii")
    except (UnicodeEncodeError, KeyError, AttributeError):
        errors.append("the manifest is not ASCII")

    excepted: set[tuple[str, int, int]] = set()
    for i, ex in enumerate(manifest.get("exceptions", [])):
        fighter = space["fighters"].get(ex.get("fighter"))
        costume = ex.get("costume")
        if fighter is None or costume not in fighter["costumes"]:
            errors.append(f"exceptions[{i}]: not a fighter/costume of the pick space")
            continue
        colours = fighter["costumes"][costume]
        colour = ex.get("colour", colours[0])
        if colour not in colours:
            errors.append(f"exceptions[{i}]: colour {colour} is not one of the costume's")
        if not isinstance(ex.get("reason"), str) or not ex["reason"].strip():
            errors.append(f"exceptions[{i}]: no reason")
        excepted.add((ex["fighter"], costume, colour))

    seen: set[str] = set()
    cutouts: set[tuple[str, int, int]] = set()
    loadouts = 0
    total_bytes = 0
    for i, f in enumerate(manifest.get("files", [])):
        rel = f.get("path", "")
        where = f"files[{i}] {rel}"
        if not isinstance(rel, str) or "\\" in rel:
            errors.append(f"{where}: path must use forward slashes")
            continue
        backslash = rel.replace("/", "\\")
        if rel in seen:
            errors.append(f"{where}: listed twice")
        seen.add(rel)
        if backslash == MANIFEST_PACKAGE_PATH or not is_selection_asset_path(backslash, space):
            errors.append(f"{where}: not a path the pick space allows")
            continue
        file = pack / rel
        if not file.is_file():
            errors.append(f"{where}: file missing")
            continue
        size = file.stat().st_size
        total_bytes += size
        if not isinstance(f.get("bytes"), int) or f["bytes"] <= 0 or f["bytes"] != size:
            errors.append(f"{where}: bytes {f.get('bytes')} is not the file's {size}")
        if not isinstance(f.get("sha256"), str) or not re.fullmatch(r"[0-9a-fA-F]{64}", f["sha256"]) \
                or f["sha256"].lower() != sha256_file(file):
            errors.append(f"{where}: sha256 does not match the file")
        problem = image_problem(file, "loadout" if LOADOUT_RE.fullmatch(backslash) else "cutout")
        if problem:
            errors.append(f"{where}: {problem}")
        if not isinstance(f.get("frame"), (int, float)) or isinstance(f.get("frame"), bool):
            errors.append(f"{where}: frame is not a number")
        if not isinstance(f.get("stage"), str):
            errors.append(f"{where}: stage is not a string")
        look = f.get("look", {})
        m = CUTOUT_RE.fullmatch(backslash)
        if m:
            expect = {"fighter": m.group(1), "costume": int(m.group(2)), "colour": int(m.group(3))}
            cutouts.add((m.group(1), int(m.group(2)), int(m.group(3))))
        else:
            m = LOADOUT_RE.fullmatch(backslash)
            expect = {"fighter": m.group(1), "part": PART_NAMES[m.group(2)], "choice": int(m.group(3))}
            loadouts += 1
        if look != expect:
            errors.append(f"{where}: look {look} does not agree with the path ({expect})")
    for key in sorted(cutouts & excepted):
        errors.append(f"{key}: excepted and also has a file")

    sel = pack / "assets" / "selection"
    if sel.is_dir():
        for p in sel.rglob("*"):
            if p.is_file():
                rel = p.relative_to(pack).as_posix()
                if rel not in seen:
                    errors.append(f"{rel}: in the pack but not in the manifest")
    for code, fighter in space["fighters"].items():
        for costume, colours in fighter["costumes"].items():
            if (code, costume, colours[0]) not in cutouts and (code, costume, colours[0]) not in excepted:
                errors.append(f"{code} costume {costume}: first colour {colours[0]} has no cutout and no exception")
    if len(seen) >= 59800 or total_bytes >= 3 * 1024 ** 3:
        errors.append("the pack is too big for the updater's zip reader (59,800 files / 3 GB)")
    possible = sum(fighter["vs"] + fighter["vt"] for fighter in space["fighters"].values())
    if loadouts < possible:
        warnings.append(f"{loadouts} of {possible} loadout pictures present")
    looks = sum(len(c) for f in space["fighters"].values() for c in f["costumes"].values())
    warnings.append(f"{len(cutouts)} cutouts of {looks} looks, {len(excepted)} excepted, {loadouts} loadout pictures")
    return errors, warnings
