"""Ember's frame meter in SFV's Training against RYU's frame data (EMBER_LIBRARY.md section 39).

  python tools/runframemeter.py [--shots DIR] [--keep]

One copy, RYU against RYU (Ember's Training select, as runtraining.py's solo
part). SFV's own Attack Data is turned on in its pause menu. P1 (copy 0's
virtual pad 0, its window in front: the game reads its pad only then) walks
up to the dummy and hits it with standing LP, standing MP, crouching MK and
standing HP; Guard All is set in the dummy's settings and the same four are
blocked; then, from mid screen, LP Hadoken and a whiffed LP Shoryuken. After
each, a shot of the meter over the game.

The mod logs each move as the meter counted it once it is over ("frame
meter: P1's script 600: startup 3, active 2, recovery 7") and each
exchange's advantage beside Training's own ("frame meter: P1's hit: P1 +4,
P2 -4 (Training's own: P1 +4, ...)"). Every number must be the frame data's
(FAT's SFV data, the final balance), and each hit's or block's advantage must
also be the one Training's own count gives. Prints PASS or FAIL; closes the
copy unless --keep. Takes D:\\sfv-re\\sfv-launch.lock while SFV runs.
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import runtraining as training  # noqa: E402
import sfvelib as lib  # noqa: E402

# RYU's frame data (FAT, raw.githubusercontent.com/D4RKONION/FAT/main/src/js/
# constants/framedata/SFVFrameData.json): the script each move plays, its
# startup, active and recovery, and its advantage on hit and on block.
# Shoryuken's recovery is its own 19 and its landing's 11; a Hadoken's active
# frame is its fireball coming out (FAT gives none).
MOVES = {
    "standing LP": (600, 3, 2, 7, +4, +2),
    "standing MP": (601, 5, 3, 10, +6, +2),
    "crouching MK": (616, 6, 3, 13, +2, -1),
    "standing HP": (602, 8, 3, 20, +3, -1),
    "LP Hadoken": (900, 14, 1, 31, None, None),
    "LP Shoryuken": (930, 3, 12, 30, None, None),
}
# P1 faces right at the start: forward is RIGHT. SFV's default layout: X LP,
# Y MP, RB HP, A LK, B MK, RT HK. Each normal walks in first: every hit and
# block pushes the two apart.
WALK_IN = "RIGHT:300 -:150 "
PRESSES = {
    "standing LP": WALK_IN + "X:50",
    "standing MP": WALK_IN + "Y:50",
    "crouching MK": WALK_IN + "DOWN+B:60",
    "standing HP": WALK_IN + "RB:50",
    "LP Hadoken": "DOWN:34 DOWN+RIGHT:34 RIGHT+X:50",
    "LP Shoryuken": "RIGHT:34 DOWN:34 DOWN+RIGHT+X:50",
}
MOVE_LINE = re.compile(r"frame meter: P1's script (\d+): startup (-?\d+), active (\d+), recovery (\d+)")
EXCHANGE_LINE = re.compile(r"frame meter: P1's (hit|block): P1 ([+-]\d+), P2 ([+-]\d+) \(Training's own: P1 ([+-]\d+)")


def pad(steps: str, settle_s: float) -> None:
    lib.request(0, f"pad 0 {steps}")
    time.sleep(settle_s)


def pause_menu(steps: str) -> None:
    """SFV's pause menu: opened with Start, walked, closed with Start."""
    pad("START:100 -:700", 1.0)
    pad(steps, 0.25 * steps.count(":") + 0.5)
    pad("START:100 -:700", 1.2)


def new_lines(seen: int) -> list[str]:
    lines = lib.log_path(0).read_text(errors="replace").splitlines()
    return lines[seen:]


def play(name: str, shots: Path | None, tag: str, failures: list[str], blocked: bool | None) -> None:
    seen = len(lib.log_path(0).read_text(errors="replace").splitlines())
    pad(f"-:100 {PRESSES[name]} -:100", 2.5)
    training.shot(shots, f"{tag}", 0)
    script, startup, active, recovery, on_hit, on_block = MOVES[name]
    lines = new_lines(seen)
    moves = [m.groups() for m in map(MOVE_LINE.search, lines) if m]
    measured = [tuple(int(v) for v in g) for g in moves if int(g[0]) == script]
    if not measured:
        failures.append(f"{name}: the log has no move of script {script} ({moves})")
        return
    got = measured[0][1:]
    print(f"{name}: startup {got[0]}, active {got[1]}, recovery {got[2]} (frame data {startup}, {active}, {recovery})")
    if got != (startup, active, recovery):
        failures.append(f"{name}: measured {got}, frame data {(startup, active, recovery)}")
    if blocked is None:
        return
    exchanges = [m.groups() for m in map(EXCHANGE_LINE.search, lines) if m]
    if not exchanges:
        failures.append(f"{name}: the log has no exchange")
        return
    kind, mine, theirs, game = exchanges[-1]
    expected = on_block if blocked else on_hit
    print(f"{name} on {kind}: P1 {mine}, P2 {theirs}; Training's own {game}; frame data {expected:+d}")
    if kind != ("block" if blocked else "hit"):
        failures.append(f"{name}: the exchange was a {kind}")
    if int(mine) != expected or int(game) != expected or int(theirs) != -expected:
        failures.append(f"{name}: advantage {mine} (Training's own {game}), frame data {expected:+d}")


def run(shots: Path | None, failures: list[str]) -> None:
    lib.launch(0, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1"])
    # The status names the runtime's host once the mod has started it.
    training.wait_for("the runtime", 120, lambda: training.status(0).get("ember", {}).get("host") == "running")
    # The main menu takes a moment to take the pad after the runtime starts.
    training.wait_for("Ember's Home", 30, lambda: training.shell(0)["shown"] or training.press(0, "START")["shown"])
    if training.shell(0)["screen"] == "welcome":
        training.seek(0, "welcome-skip", ["DOWN", "DOWN", "RIGHT"])
        training.press(0, "A")
    training.seek(0, "training", ["DOWN"], 12)
    training.press(0, "A")
    training.wait_for("Training select", 20, lambda: training.shell(0)["shown"] and training.shell(0)["screen"] == "training")
    training.seek(0, "training-start", ["DOWN"], 6)
    training.press(0, "A")
    training.wait_for("SFV's Training battle", 90, lambda: training.scene(0) == training.TRAINING_SCENE
                      and training.training(0)["available"] and not training.shell(0)["shown"])
    # The meter follows from the update after Training has seen the battle,
    # so its line can come a moment after the battle is available.
    deadline = time.monotonic() + 10
    while not lib.count_log(0, "frame meter: Training battle 1 followed"):
        if time.monotonic() >= deadline:
            failures.append("the meter does not follow the Training battle")
            return
        time.sleep(0.25)
    lib.focus(lib.status_pid(0))
    time.sleep(4)
    # Basic Settings: Attack Data is the sixth row.
    pause_menu("DOWN:80 -:150 DOWN:80 -:150 DOWN:80 -:150 DOWN:80 -:150 DOWN:80 -:150 RIGHT:80 -:300")
    pad("RIGHT:1500 -:300", 2.0)
    for name in ("standing LP", "standing MP", "crouching MK", "standing HP"):
        play(name, shots, f"hit-{name.replace(' ', '-')}", failures, blocked=False)
    # Dummy Settings (RB): Guard, the third row, from No Guard to Guard All.
    pause_menu("RB:80 -:600 DOWN:80 -:150 DOWN:80 -:150 RIGHT:80 -:200 RIGHT:80 -:200 RIGHT:80 -:300")
    for name in ("standing LP", "standing MP", "crouching MK", "standing HP"):
        play(name, shots, f"block-{name.replace(' ', '-')}", failures, blocked=True)
    pad("LEFT:1200 -:300", 1.8)
    play("LP Hadoken", shots, "hadoken", failures, blocked=None)
    play("LP Shoryuken", shots, "shoryuken", failures, blocked=None)
    names = [m.group(0) for m in map(re.compile(r"state kind \d+.*").search, lib.log_path(0).read_text(errors="replace").splitlines()) if m]
    for line in names:
        print("unnamed:", line)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shots", type=Path)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if args.shots:
        args.shots.mkdir(parents=True, exist_ok=True)
    lib.begin_run()
    failures: list[str] = []
    training.take_launch_lock()
    try:
        run(args.shots, failures)
    except lib.SfveError as e:
        failures.append(str(e))
    finally:
        if not args.keep:
            lib.kill_all()
            training.release_launch_lock()
    return lib.verdict(failures)


if __name__ == "__main__":
    sys.exit(main())
