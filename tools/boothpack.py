"""Photo booth post-processing: grabs -> cutout masters -> the picture pack.

Needs Pillow and numpy (tools/booth-requirements.txt). Python 3.10+.

  python -I tools/boothpack.py --selftest
  python -I tools/boothpack.py pack [--store D:\\sfv-ember-renders] [--except-failed] [--mod-revision HEX]
  python -I tools/boothpack.py validate [--store D:\\sfv-ember-renders]

Masters (written while the capture runs, by runbooth.py through make_masters):
  <store>\\masters\\<CODE>\\costume-<N>\\color-<M>.png   512x768 RGBA, lossless, the game's own numbers
  <store>\\masters\\<CODE>\\costume-<N>\\color-<M>.json  frame, stage, mask method
  <store>\\masters\\<CODE>\\vs1.png ...                  loadout pictures, 16:9, lossless (+ .json)

The pack (`pack`), exactly as docs/design/beta8/pack.md says:
  <store>\\pack\\assets\\selection\\<CODE>\\costume-<N>\\color-<M>-cutout.png   256x384 palette PNG, transparent
  <store>\\pack\\assets\\selection\\<CODE>\\vs<k>.png, vt<k>.png               256x144 palette PNG
(booth/palette.py: the fewest colours that keep a cutout as close as boothpack 1.0's 256 did)
  <store>\\pack\\manifest.json
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I leaves the script's folder out; a process pool's children need it too

# The modules under tools/booth are the work; the names other tools use (runbooth.py, boothsheet.py)
# stay reachable here.
from booth.checks import check_masters, check_pair, master_suspect, untextured  # noqa: E402,F401
from booth.grabs import depth_target_id, read_colour, read_depth  # noqa: E402,F401
from booth.mask import compute_mask, make_masters  # noqa: E402,F401
from booth.pack import build_pack, failed_looks, find_mod_revision  # noqa: E402,F401
from booth.selftest import selftest  # noqa: E402
from booth.space import (DEFAULT_PICK_SPACE, DEFAULT_STORE, PART_NAMES, PIPELINE, VERSION, BoothError,  # noqa: E402,F401
                         cutout_relpath, is_selection_asset_path, load_space, loadout_master_path,
                         loadout_relpath, master_path)
from booth.validate import validate_pack  # noqa: E402


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--selftest", action="store_true", help="synthetic grabs -> masks -> pack -> validation; no game")
    sub = p.add_subparsers(dest="cmd")
    for name in ("pack", "validate", "check"):
        s = sub.add_parser(name)
        s.add_argument("--store", default=DEFAULT_STORE)
        s.add_argument("--pick-space", default=str(DEFAULT_PICK_SPACE))
        if name == "pack":
            s.add_argument("--mod-revision", help="git revision of the mod that captured (default: the build receipt's sourceRevision)")
            s.add_argument("--except-failed", action="store_true", help="leave out, as manifest exceptions, the looks the capture log lists as failed")
            s.add_argument("--frame", type=int, default=300, help="frame recorded for a master with no sidecar")
            s.add_argument("--stage", default="TRN", help="stage recorded for a master with no sidecar")
            s.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
            s.add_argument("--force", action="store_true", help="rebuild every pack file")
        if name == "check":
            s.add_argument("--out", help="write the looks to capture again here, one CODE:costume:colour a line "
                                         "(for runbooth.py --looks-file); default <store>\\recheck.txt")
            s.add_argument("--unchecked", action="store_true",
                           help="also list masters captured before the grab checks (their sidecar has no checks)")
            s.add_argument("--quarantine-untextured", action="store_true",
                           help="set aside masters drawn without their textures and log them as failed looks")
    a = p.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.cmd:
        p.error("a command (pack, validate) or --selftest")
    space = load_space(a.pick_space)
    store = Path(a.store)
    if a.cmd == "check":
        return check_masters(store, space, Path(a.out) if a.out else store / "recheck.txt", a.unchecked, a.quarantine_untextured)
    try:
        if a.cmd == "pack":
            manifest = build_pack(store, space, a.mod_revision, a.except_failed, a.frame, a.stage, a.jobs, a.force)
            print(f"{len(manifest['files'])} files, {len(manifest['exceptions'])} exceptions -> {store / 'pack' / 'manifest.json'}")
        errors, warnings = validate_pack(store / "pack", space)
    except BoothError as e:
        print("error:", e, file=sys.stderr)
        return 2
    for w in warnings:
        print("note:", w)
    for e in errors[:50]:
        print("INVALID:", e)
    if len(errors) > 50:
        print(f"... and {len(errors) - 50} more")
    print("pack is valid" if not errors else f"pack is NOT valid ({len(errors)} problems)")
    return 3 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
