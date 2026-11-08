"""A room link from the browser reaches a running SFV Ember.

  python tools/runlink.py [--shots DIR]

EMBER_LIBRARY.md section 29. Needs SFVE_VIA=launcher: links go from one
Launcher.exe to the game another one started.
1. Launcher.exe --help names SFV Ember's own scheme (ember-sfv://), not
   SF4 Ember's.
2. Copy 0 starts through Launcher.exe (instance 0, the player's), reaches the
   main menu and hosts a private room, so a link that arrives is held for the
   Join screen instead of joined at once (no lookup of the made-up code).
3. A second Launcher.exe is started as the browser starts it, with
   --join-link ember-sfv://join/<code>. It must find the first one running,
   hand the link over ("Handed the room link to the running game" in the
   launcher's log) and exit; the game's runtime must take it ("Room: a room
   link arrived from the browser" in the mod's log) while copy 0 stays in its
   room.

Prints what it saw, and PASS or FAIL; copy 0 is closed at the end.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

# A code of the right shape (12 Crockford base32 symbols) that no room has.
CODE = "7Q4M-2KXD-9FNB"
SCHEME = "ember-sfv"
ARRIVED = "Room: a room link arrived from the browser"
HANDED = "Handed the room link to the running game"


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])


def wait_for(what: str, timeout_s: float, done) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if done():
            return
        time.sleep(0.5)
    raise lib.SfveError(f"timed out waiting for {what}")


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="runlink")
    p.add_argument("--shots", type=Path)
    a = p.parse_args(argv)
    if lib.VIA != "launcher":
        print("FAIL: set SFVE_VIA=launcher (and SFVE_PACKAGE_DIR): links go between launchers")
        return 1
    launcher = lib.PACKAGE_DIR / "Launcher.exe"
    launcher_log = lib.LOG_DIR / "launcher.log"
    failures: list[str] = []

    shown = subprocess.run([str(launcher), "--help"], capture_output=True, text=True, timeout=30)
    help_text = shown.stdout + shown.stderr
    line = next((l.strip() for l in help_text.splitlines() if "--join-link" in l), "")
    following = help_text[help_text.find("--join-link"):][:400] if "--join-link" in help_text else ""
    print(f"help: {line or '(no --join-link line)'}")
    if f"{SCHEME}://join/" not in following:
        failures.append(f"--help does not name {SCHEME}://join/")
    if "ember://" in following:
        failures.append("--help still names ember://")

    try:
        lib.kill_all()
        lib.launch(0, "netplay")
        lib.boot_to_menu(0)
        wait_for("the room", 30, lambda: lib.request(0, "room 1 30").startswith("ok"))
        wait_for("copy 0 in its room", 60, lambda: status(0)["room"]["room"] == "joined")
        log_from = launcher_log.stat().st_size if launcher_log.exists() else 0
        link = f"{SCHEME}://join/{CODE}"
        second = subprocess.run([str(launcher), "--join-link", link], capture_output=True, timeout=60)
        print(f"second launcher exit code: {second.returncode}")
        text = launcher_log.read_text(errors="replace")[log_from:] if launcher_log.exists() else ""
        for entry in text.splitlines():
            if "link" in entry or "already running" in entry:
                print(f"launcher: {entry}")
        if HANDED not in text:
            failures.append("the second launcher did not hand the link to the running game")
        try:
            wait_for("the runtime to take the link", 15, lambda: lib.count_log(0, ARRIVED) > 0)
        except lib.SfveError as error:
            failures.append(str(error))
        for entry in lib.tail(0, 400):
            if "link" in entry.lower():
                print(f"mod: {entry}")
        room = status(0)["room"]
        print(f"copy 0 after the link: room {room.get('room')}, notice '{room.get('notice', '')}'")
        if room.get("room") != "joined":
            failures.append("copy 0 left its room when the link arrived")
        if a.shots:
            a.shots.mkdir(parents=True, exist_ok=True)
            time.sleep(1)
            print(lib.screenshot(lib.status_pid(0), a.shots / "link-arrived-0.png"))
    except (lib.SfveError, TimeoutError, subprocess.TimeoutExpired) as error:
        failures.append(str(error))
    finally:
        lib.kill_all()

    print(f"FAIL: {'; '.join(failures)}" if failures else f"PASS: {SCHEME}:// link handed to the running game and taken")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
