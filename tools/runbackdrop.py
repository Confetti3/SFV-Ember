"""The menu's artwork backdrop on one copy (docs/design/beta8/backdrop.md).

  python tools/runbackdrop.py --out DIR [--report] [--sheets] [--shots] [--keep]

One copy (development build), its virtual pad 0 driving Ember's shell as a
physical pad does, the pipe reading the status ("backdrop") and sending the
backdrop's test command:

--report  `backdrop report <DIR>`: every path of data/menu-art.json, used or
          not, loaded through the game at its main menu, one at a time;
          DIR/backdrop-report.tsv has a row per path (index, family, use,
          path, result, width, height, DXGI format, load ms, sheet, cell).
          Prints how many loaded, at what sizes, and the slowest loads.
--sheets  with --report: also DIR/backdrop-sheet-NN.bmp (to .png where
          Pillow is installed), 12 x 12 tiles of 128 x 72, each picture as
          the main menu shows it (cover fit, the art's tint, Home's scrim);
          the TSV's sheet and cell (row,column) say which tile is which.
--shots   DIR/bd-*.png: Ember's pages with the backdrop, the picture stepped
          between them (`backdrop step`, then the fade waited out): Home,
          Online play, Settings > Interface > Look with the Background
          artwork row (on, then off, then on again), Profile, Replays,
          Training select, the dummy's board, its costume and V-Skill and
          V-Trigger cards, the stage cards; Home with the backdrop off; and
          SFV's Training battle, where the backdrop must be neither drawn
          nor loaded.

The pictures are Capcom's: DIR stays on this PC, out of the repository and
any package. Prints PASS or FAIL; closes the copy unless --keep. Takes the
launch lock as runtraining.py does. Room pages: runshell.py --shots DIR
--step-backdrop.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import runshell as shell_steps  # noqa: E402
import runtraining as training_steps  # noqa: E402
import sfvelib as lib  # noqa: E402

status = shell_steps.status
shell = shell_steps.shell
press = shell_steps.press
wait_for = shell_steps.wait_for
expect = shell_steps.expect
seek = shell_steps.seek
expect_pick = shell_steps.expect_pick
seek_pick = shell_steps.seek_pick
seek_runs = shell_steps.seek_runs
pick_place = shell_steps.pick_place
backdrop = shell_steps.backdrop
step_backdrop = shell_steps.step_backdrop

problems: list[str] = []


def note(problem: str) -> None:
    print("PROBLEM:", problem)
    problems.append(problem)


def shot(out: Path, name: str, step: bool = True) -> None:
    current = step_backdrop(0) if step else backdrop(0)["current"]
    time.sleep(0.4)
    lib.screenshot(lib.status_pid(0), out / f"{name}.png")
    b = backdrop(0)
    print(f"{name}: page {b['page']}, drawn {b['drawn']}, picture {current}")


def reach(row: str) -> None:
    """A row of a list wherever it is: up to its top, then down."""
    seek_runs(0, row, [("UP", 16), ("DOWN", 16)])


def home() -> None:
    """Back to Home: B until the shell is there."""
    for _ in range(8):
        if shell(0)["screen"] == "home":
            return
        press(0, "B")
    expect(0, "home")


def report(out: Path, sheets: bool) -> None:
    reply = lib.request(0, f"backdrop report {'sheets ' if sheets else ''}{out}")
    print("report:", reply)
    if not reply.startswith("ok"):
        raise lib.SfveError(f"the report did not start: {reply}")
    last = -1
    started = time.time()
    while backdrop(0)["report"]["running"]:
        r = backdrop(0)["report"]
        if r["done"] != last and r["done"] % 50 == 0:
            print(f"report: {r['done']}/{r['total']}, {r['loaded']} loaded ({r['last']})")
            last = r["done"]
        if time.time() - started > 45 * 60:
            raise lib.SfveError("the report did not finish in 45 minutes")
        time.sleep(2)
    rows = list(csv.DictReader((out / "backdrop-report.tsv").open(encoding="utf-8"), delimiter="\t"))
    loaded = [r for r in rows if r["result"].startswith("loaded")]
    print(f"report: {len(rows)} paths, {len(loaded)} loaded")
    sizes: dict[str, int] = {}
    for r in loaded:
        sizes[f"{r['width']}x{r['height']} format {r['format']}"] = sizes.get(f"{r['width']}x{r['height']} format {r['format']}", 0) + 1
    for size, count in sorted(sizes.items(), key=lambda item: -item[1]):
        print(f"  {count:5} at {size}")
    by_family: dict[str, list[int]] = {}
    for r in rows:
        tally = by_family.setdefault(f"{r['family']} ({r['use']})", [0, 0])
        tally[0] += 1
        tally[1] += r["result"].startswith("loaded")
    for family, (count, ok) in by_family.items():
        print(f"  {family}: {ok}/{count} loaded")
    slow = sorted(rows, key=lambda r: -float(r["load_ms"] or 0))[:5]
    print("  slowest loads:", ", ".join(f"{r['path'].rsplit('/', 1)[-1]} {r['load_ms']} ms" for r in slow))
    for r in rows:
        if not r["result"].startswith("loaded") and r["use"] == "used":
            print(f"  not loaded (used): {r['path']}: {r['result']}")
    if sheets:
        try:
            from PIL import Image  # type: ignore
        except ImportError:
            print("report: Pillow is not installed; the sheets stay .bmp")
            return
        for bmp in sorted(out.glob("backdrop-sheet-*.bmp")):
            with Image.open(bmp) as picture:
                picture.convert("RGB").save(bmp.with_suffix(".png"), optimize=True)
            bmp.unlink()
        print(f"report: {len(list(out.glob('backdrop-sheet-*.png')))} contact sheets in {out}")


def pages(out: Path) -> None:
    shot(out, "bd-01-home")
    # Online play.
    reach("online")
    press(0, "A")
    expect(0, "online")
    shot(out, "bd-02-online")
    home()
    # Settings > Interface > Look, with its Background artwork row.
    reach("settings")
    press(0, "A")
    wait_for("Settings", 10, lambda: shell(0)["screen"].startswith("tab-"))
    for _ in range(4):
        if shell(0)["screen"] == "tab-interface":
            break
        press(0, "RB")
    expect(0, "tab-interface")
    reach("tab-interface/look")
    press(0, "A")
    expect(0, "tab-interface/look")
    seek(0, "menu-artwork", ["DOWN"], 4)
    shot(out, "bd-03-settings-look")
    press(0, "LEFT")
    wait_for("the backdrop off", 10, lambda: not backdrop(0)["enabled"] and not backdrop(0)["drawn"])
    shot(out, "bd-04-settings-look-off", step=False)
    if backdrop(0)["alive"] != 0:
        note(f"the backdrop is off but {backdrop(0)['alive']} pictures are alive")
    press(0, "RIGHT")
    wait_for("the backdrop on again", 60, lambda: backdrop(0)["enabled"] and backdrop(0)["drawn"])
    shot(out, "bd-05-settings-look-on", step=False)
    home()
    # Profile and Replays.
    for n, row in (("06", "profile"), ("07", "replays")):
        reach(row)
        press(0, "A")
        time.sleep(1)
        shot(out, f"bd-{n}-{row}")
        home()
    # Training select, and the dummy's pick screen pages (runtraining's solo steps).
    reach("training")
    press(0, "A")
    wait_for("Training select", 20, lambda: shell(0)["shown"] and shell(0)["screen"] == "training")
    shot(out, "bd-08-training-select")
    seek(0, "training-dummy", ["DOWN"], 6)
    press(0, "A")
    expect_pick(0, "home")
    time.sleep(1)
    shot(out, "bd-09-board")
    press(0, "RIGHT")
    press(0, "RIGHT")
    for _ in range(6):
        press(0, "A")
        if pick_place(0)["page"] == "look":
            break
        press(0, "RIGHT")
    expect_pick(0, "look")
    seek_pick(0, "costume", ["DOWN"], 12)
    press(0, "A")
    expect_pick(0, "costumes")
    time.sleep(1)
    shot(out, "bd-10-costumes")
    press(0, "B")
    seek_pick(0, "vskill", ["DOWN"], 8)
    press(0, "A")
    expect_pick(0, "loadout")
    time.sleep(1)
    shot(out, "bd-11-loadout")
    press(0, "B")
    press(0, "B")
    expect_pick(0, "home")
    press(0, "B")
    expect(0, "training")
    seek(0, "training-stage", ["DOWN"], 6)
    press(0, "A")
    expect_pick(0, "stage")
    time.sleep(1)
    shot(out, "bd-12-stages")
    press(0, "B")
    expect(0, "training")
    home()
    # Home with the backdrop off (the pipe sets the same saved choice as the row).
    print("backdrop off:", lib.request(0, "backdrop off"))
    wait_for("the backdrop off", 10, lambda: not backdrop(0)["drawn"])
    shot(out, "bd-13-home-off", step=False)
    print("backdrop on:", lib.request(0, "backdrop on"))
    wait_for("the backdrop on again", 60, lambda: backdrop(0)["drawn"])
    # SFV's Training battle: no backdrop drawn, none loaded.
    loads = lib.count_log(0, "backdrop: ")
    reach("training")
    press(0, "A")
    wait_for("Training select", 20, lambda: shell(0)["shown"] and shell(0)["screen"] == "training")
    seek(0, "training-start", ["DOWN"], 6)
    press(0, "A")
    wait_for("SFV's Training battle", 90, lambda: training_steps.scene(0) == training_steps.TRAINING_SCENE and not shell(0)["shown"])
    time.sleep(5)
    b = backdrop(0)
    lib.screenshot(lib.status_pid(0), out / "bd-14-training-battle.png")
    if b["allowed"] or b["drawn"]:
        note(f"the backdrop is allowed or drawn in the Training battle: {json.dumps(b)}")
    after = [line for line in lib.log_path(0).read_text(errors="replace").splitlines() if "backdrop: " in line]
    loaded_in_battle = [line for line in after[loads:] if "loaded in" in line]
    if loaded_in_battle:
        note(f"pictures loaded around the Training battle: {loaded_in_battle}")


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--report", action="store_true")
    p.add_argument("--sheets", action="store_true")
    p.add_argument("--shots", action="store_true")
    p.add_argument("--keep", action="store_true")
    a = p.parse_args(argv)
    if not (a.report or a.shots):
        p.error("nothing to do: --report and/or --shots")
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=True)
    lib.begin_run()
    training_steps.take_launch_lock()
    failure = ""
    try:
        lib.kill_all()
        if lib.VIA != "launcher":
            lib.deploy()
        training_steps.launch([0], mash=False)
        shell_steps.open_home(0)
        wait_for("the backdrop's first picture", 90, lambda: backdrop(0)["drawn"])
        print("backdrop:", json.dumps(backdrop(0)))
        if a.report:
            report(a.out, a.sheets)
            wait_for("the backdrop after the report", 60, lambda: backdrop(0)["drawn"])
        if a.shots:
            pages(a.out)
        if problems:
            failure = "; ".join(problems)
    except lib.SfveError as e:
        failure = str(e)
    finally:
        try:
            print("copy 0: backdrop", json.dumps(backdrop(0)), "shell", json.dumps(shell(0)))
        except (lib.SfveError, KeyError):
            pass
        if not a.keep:
            lib.kill_all()
            training_steps.release_launch_lock()
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
