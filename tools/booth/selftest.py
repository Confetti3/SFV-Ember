"""Self-test of the booth pack tools: synthetic grabs -> masks -> pack -> validation. No game.
Run through `python -I tools/boothpack.py --selftest`."""

from __future__ import annotations

import collections
import json
import shutil
import sys
import tempfile
from pathlib import Path

try:
    import numpy as np
    from PIL import Image
except ImportError:  # pragma: no cover - environment problem, said clearly
    sys.exit("the booth tools need Pillow and numpy: python -m pip install -r tools/booth-requirements.txt (in a venv)")

from .checks import write_reference_once
from .mask import make_masters
from .pack import build_pack
from .space import (LOADOUT_PACK_SIZE, MASTER_MARGIN, MASTER_SIZE, PACK_MARGIN, PACK_SIZE, BoothError,
                    is_selection_asset_path, loadout_master_path, master_path)
from .validate import image_problem, validate_pack


def _synthetic_space() -> dict:
    return {"build": "sfv-ember/13173604/0.5/test/layout-test", "gameBuild": "13173604", "stages": ["TRN"],
            "fighters": collections.OrderedDict([
                ("AAA", {"costumes": collections.OrderedDict([(0, [0, 1, 100]), (2, [0])]), "vs": 2, "vt": 2}),
                ("BBB", {"costumes": collections.OrderedDict([(0, [0, 1])]), "vs": 1, "vt": 1}),
            ])}


def _write_grabs(folder: Path, with_depth: bool, fighters_fill: tuple[tuple[int, int, int], tuple[int, int, int]]) -> tuple[Path, Path]:
    rng = np.random.default_rng(7)
    w, h = 1920, 1080
    ys, xs = np.mgrid[0:h, 0:w]
    background = np.dstack([(xs * 255 // w), (ys * 255 // h), np.full((h, w), 90)]).astype(np.uint8)
    a = background.copy()
    depth_b = np.full((h, w), 0.9, dtype=np.float32)
    depth_a = depth_b.copy()
    # P1: a 200x700 block at x 600-800. P2: 200x600 at x 1200-1400, with a marker on its screen-left edge.
    a[300:1000, 600:800] = fighters_fill[0]
    depth_a[300:1000, 600:800] = 0.4
    a[400:1000, 1200:1400] = fighters_fill[1]
    a[600:700, 1200:1240] = (255, 255, 255)      # the marker: left on screen, must end up on the right after the flip
    depth_a[400:1000, 1200:1400] = 0.4
    a[100:104, 100:104] = rng.integers(0, 255, (4, 4, 3))  # a speck of noise, below the component limit
    a_path, b_path = folder / "a.bmp", folder / "b.bmp"
    Image.fromarray(a, "RGB").save(a_path)
    Image.fromarray(background, "RGB").save(b_path)
    if with_depth:
        for path, depth in ((a_path, depth_a), (b_path, depth_b)):
            (Path(str(path) + ".depth")).write_bytes(depth.astype("<f4").tobytes())
            Path(str(path) + ".json").write_text(json.dumps(
                {"width": w, "height": h, "format": "D32_FLOAT", "rowPitch": w * 4, "present": 1}), encoding="utf-8")
    return a_path, b_path


def selftest() -> int:
    failures: list[str] = []

    def check(condition: bool, what: str) -> None:
        if not condition:
            failures.append(what)
        print(("ok   " if condition else "FAIL ") + what)

    space = _synthetic_space()
    # The path rules.
    good = ["assets\\selection\\manifest.json", "assets\\selection\\AAA\\costume-0\\color-100-cutout.png",
            "assets\\selection\\AAA\\costume-2\\color-0-cutout.png", "assets\\selection\\AAA\\vs2.png",
            "assets\\selection\\BBB\\vt1.png"]
    bad = ["assets\\selection\\AAA\\costume-1\\color-0-cutout.png", "assets\\selection\\AAA\\costume-0\\color-2-cutout.png",
           "assets\\selection\\AAA\\costume-0\\color-0-cutout.PNG", "assets\\selection\\AAA\\costume-00\\color-0-cutout.png",
           "assets\\selection\\AAA\\costume-0\\color-+0-cutout.png", "assets/selection/AAA/costume-0/color-0-cutout.png",
           "assets\\selection\\aaa\\vs1.png", "assets\\selection\\AAA\\vs3.png", "assets\\selection\\BBB\\vs2.png",
           "assets\\selection\\AAA\\vs0.png", "assets\\selection\\AAA\\vs1.jpg", "assets\\selection\\AAA\\vs1.png\\",
           "assets\\selection\\AAA\\costume-0\\color-0.png", "assets\\selection\\AAA\\..\\AAA\\vs1.png",
           "assets\\\\selection\\AAA\\vs1.png", "assets\\selection\\AAA\\vs1.png\x00", "assets\\selection\\AAA\\vs1.png:x",
           "assets\\selection\\AAA\\costume-0\\color-1234567-cutout.png", "assets\\selection\\AAAA\\vs1.png"]
    check(all(is_selection_asset_path(p, space) for p in good), "accepted forms are accepted")
    for p in bad:
        check(not is_selection_asset_path(p, space), f"refused {p!r}")
    check(is_selection_asset_path("assets\\selection\\manifest.json", {"fighters": {}}), "manifest accepted with an empty space")

    work = Path(tempfile.mkdtemp(prefix="boothpack-selftest-"))
    try:
        store = work / "store"
        # Depth path and colour path, each with two fighters and a speck of noise.
        d_dir = work / "d1"
        d_dir.mkdir()
        a, b = _write_grabs(d_dir, True, ((200, 40, 40), (40, 40, 200)))
        res = make_masters(a, b, [("AAA", 0, 0), ("BBB", 0, 0)], store, "depth", {"frame": 300, "stage": "TRN"})
        check(res.get("method") == "depth", "mask taken from the depth difference")
        for i, look in enumerate([("AAA", 0, 0), ("BBB", 0, 0)]):
            check(isinstance(res.get(i), Path) and res[i].is_file(), f"master for side {i + 1} written")
        m1 = Image.open(master_path(store, "AAA", 0, 0))
        m2 = Image.open(master_path(store, "BBB", 0, 0))
        check(m1.size == MASTER_SIZE and m1.mode == "RGBA", "master is 512x768 RGBA")
        check(m1.getchannel("A").getpixel((0, 0)) == 0 and m1.getchannel("A").getpixel((256, 600)) == 255, "master alpha: empty corner, solid body")
        a1 = np.asarray(m1)
        check(abs(int(a1[..., 0][a1[..., 3] > 200].mean()) - 200) < 3, "P1 colour kept")
        a2 = np.asarray(m2)
        solid = np.nonzero(a2[..., 3] > 200)
        white = (a2[..., 0] > 240) & (a2[..., 1] > 240) & (a2[..., 2] > 240) & (a2[..., 3] > 200)
        wx = np.nonzero(white)[1].mean()
        check(wx > (solid[1].min() + solid[1].max()) / 2, "P2 flipped: marker moved from the left edge to the right")
        ys = solid[0]
        check(ys.max() == MASTER_SIZE[1] - MASTER_MARGIN - 1 or ys.max() >= MASTER_SIZE[1] - MASTER_MARGIN - 2, "master bottom-aligned to the margin")
        # Colour fallback: no depth files.
        c_dir = work / "c1"
        c_dir.mkdir()
        a, b = _write_grabs(c_dir, False, ((10, 200, 10), (200, 200, 10)))
        res = make_masters(a, b, [("AAA", 0, 1), ("AAA", 0, 100)], store, "depth", {"frame": 300, "stage": "TRN"})
        check(res.get("method") == "colour" and isinstance(res.get(0), Path) and isinstance(res.get(1), Path), "colour fallback without depth sidecars")
        # One side only.
        res = make_masters(a, b, [("AAA", 2, 0), None], store, "colour", {"frame": 300, "stage": "TRN"})
        check(isinstance(res.get(0), Path), "single-side mask")
        # Failure: empty mask reports a reason.
        e_dir = work / "e1"
        e_dir.mkdir()
        Image.fromarray(np.zeros((1080, 1920, 3), np.uint8), "RGB").save(e_dir / "a.bmp")
        Image.fromarray(np.zeros((1080, 1920, 3), np.uint8), "RGB").save(e_dir / "b.bmp")
        res = make_masters(e_dir / "a.bmp", e_dir / "b.bmp", [("BBB", 0, 1), None], store, "colour")
        check(isinstance(res.get(0), BoothError), "empty mask is a failure with a reason")

        # Loadout masters (as runbooth writes them), then the pack.
        for code, prefix in (("AAA", "vs"), ("AAA", "vt"), ("BBB", "vs")):
            for choice in range(1, space["fighters"][code][prefix] + 1):
                p = loadout_master_path(store, code, prefix, choice)
                Image.fromarray(np.random.default_rng(choice).integers(0, 255, (936, 1664, 3), dtype=np.uint8), "RGB").save(p)
                p.with_suffix(".json").write_text(json.dumps({"frame": 640, "stage": "TRN"}))
        # BBB costume 1 is missing: AAA costume 0 colour 100 and costume 2 have no first-colour gap; except BBB 0/1 instead.
        (store / "capture-log.jsonl").write_text(json.dumps(
            {"look": {"fighter": "BBB", "costume": 0, "colour": 1}, "reason": "capture failed: costume does not load"}) + "\n")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        pack = store / "pack"
        errors, warnings = validate_pack(pack, space)
        check(not errors, "the pack validates against pack.md's rules" + (f": {errors[:3]}" if errors else ""))
        check([e["colour"] for e in manifest["exceptions"]] == [1], "a failed look with no master is excepted, with its reason")
        cut = pack / "assets/selection/AAA/costume-0/color-0-cutout.png"
        im = Image.open(cut)
        check(im.mode == "P" and "transparency" in im.info, "cutout is a palette PNG with transparency")
        rgba = im.convert("RGBA")
        check(rgba.size == PACK_SIZE and rgba.getpixel((0, 0))[3] == 0, "cutout is 256x384 with a transparent corner")
        box = rgba.getchannel("A").point(lambda v: 255 if v >= 8 else 0).getbbox()
        check(box[3] == PACK_SIZE[1] - PACK_MARGIN and box[1] >= PACK_MARGIN, "cutout bottom-centred with the 16 px margin")
        jpg = Image.open(pack / "assets/selection/AAA/vs1.png")
        check(jpg.format == "PNG" and jpg.mode == "RGB" and jpg.size == LOADOUT_PACK_SIZE, "loadout picture is a 256x144 RGB PNG")
        print(f"  sizes: cutout {cut.stat().st_size} bytes, loadout picture {(pack / 'assets/selection/AAA/vs1.png').stat().st_size} bytes")
        check({f["path"] for f in manifest["files"]} >= {"assets/selection/AAA/vs2.png", "assets/selection/BBB/vs1.png"}, "manifest lists the loadout pictures")
        # An accepted exception: remove BBB costume 0 colour 1's master and AAA's costume 2.
        master_path(store, "AAA", 2, 0).unlink()
        master_path(store, "AAA", 2, 0).with_suffix(".json").unlink()
        (store / "capture-log.jsonl").write_text(
            json.dumps({"look": {"fighter": "AAA", "costume": 2, "colour": 0}, "reason": "capture failed: look mismatch"}) + "\n")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        errors, _ = validate_pack(pack, space)
        check(not errors and len(manifest["exceptions"]) == 1, "--except-failed excepts a failed look and the pack validates" + (f": {errors[:3]}" if errors else ""))
        # Without the exception the missing first colour is an error.
        manifest = build_pack(store, space, "0123abc", False, 300, "TRN", 1)
        errors, _ = validate_pack(pack, space)
        check(any("first colour" in e for e in errors), "a missing first colour without an exception is refused")
        manifest = build_pack(store, space, "0123abc", True, 300, "TRN", 1)
        # The pictures are opened: a file that is not the picture it claims to be is caught.
        cut_ok = pack / "assets/selection/AAA/costume-0/color-0-cutout.png"
        vs_ok = pack / "assets/selection/AAA/vs1.png"
        check(image_problem(cut_ok, "cutout") is None and image_problem(vs_ok, "loadout") is None, "the packed pictures pass the image check")
        check(image_problem(cut_ok, "loadout") is not None, "a cutout is not a loadout picture (size and mode)")
        cut_bytes = cut_ok.read_bytes()
        (work / "cut-cut.png").write_bytes(cut_bytes[: len(cut_bytes) // 2])
        (work / "cut-text.png").write_bytes(b"not a picture")
        Image.new("RGBA", PACK_SIZE).save(work / "cut-rgba.png")
        check(image_problem(work / "cut-cut.png", "cutout") is not None, "a truncated PNG is caught")
        check(image_problem(work / "cut-text.png", "cutout") is not None, "a file that is no picture is caught")
        check("mode" in (image_problem(work / "cut-rgba.png", "cutout") or ""), "a cutout that is not a palette picture is caught")
        # The reference plate is written once, whole, however many copies get there together.
        from concurrent.futures import ThreadPoolExecutor
        plate = np.full((8, 8, 3), 7, np.float32)
        ref = work / "reference" / "plate.png"
        with ThreadPoolExecutor(max_workers=8) as pool:
            wrote = list(pool.map(lambda i: write_reference_once(ref, plate + i), range(8)))
        check(sum(wrote) == 1 and ref.is_file(), "of eight copies finishing together exactly one writes the reference plate")
        check(not list(ref.parent.glob("*.tmp*")), "no temporary plate is left beside the reference")
        with Image.open(ref) as shown:
            check(shown.size == (8, 8), "the reference plate reads back whole")
        # Tampering is caught.
        victim = pack / "assets/selection/AAA/costume-0/color-0-cutout.png"
        original = victim.read_bytes()
        victim.write_bytes(original + b"x")
        errors, _ = validate_pack(pack, space)
        check(any("sha256" in e or "bytes" in e for e in errors), "a changed file is caught")
        victim.write_bytes(original)
        stray = pack / "assets/selection/AAA/costume-0/color-9-cutout.png"
        shutil.copyfile(victim, stray)
        errors, _ = validate_pack(pack, space)
        check(any("not in the manifest" in e for e in errors), "a file missing from the manifest is caught")
        stray.unlink()
        data = json.loads((pack / "manifest.json").read_text())
        data["files"][0]["path"] = data["files"][0]["path"].replace("-cutout.png", "-cutout.PNG")
        (pack / "manifest.json").write_text(json.dumps(data))
        errors, _ = validate_pack(pack, space)
        check(any("not a path" in e for e in errors), "a wrong-case path in the manifest is caught")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("SELFTEST " + ("FAILED: " + "; ".join(failures) if failures else "PASSED"))
    return 1 if failures else 0
