"""Links from the browser reach a running SFV Ember.

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
   hand the link over and exit 0; the game's runtime must take it ("Room: a
   room link arrived from the browser" in the mod's log) while copy 0 stays
   in its room. Its own "Handed the room link to the running game" line is
   printed when it can be found (section 29: the second launcher's log).
4. The same with a match link, ember-sfv://tournament/open?bridge=..&match=..
   (section 31), for a made-up service and match: the runtime must take it
   ("Tournament: a link opened match ...") and, the copy being in a room,
   hold it for Play, so no bridge is asked.

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
# A service and a match of the right shape that no bridge has: the copy is in
# its room, so the link waits for Play and nothing is looked up.
BRIDGE = "brg_6f1c2d3e-4a5b-4c6d-8e7f-90a1b2c3d4e5"
MATCH = "emt_0a1b2c3d-4e5f-4a6b-9c7d-8e9f0a1b2c3d"
SCHEME = "ember-sfv"
# Each link the second launcher hands over: the link, what it is called in
# the launcher's "Handed the ... to the running game", and the runtime's line
# when it takes it.
LINKS = (
    (f"{SCHEME}://join/{CODE}", "room link", "Room: a room link arrived from the browser"),
    (f"{SCHEME}://tournament/open?bridge={BRIDGE}&match={MATCH}", "tournament match link",
     f"Tournament: a link opened match {MATCH}"),
)


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])


def wait_for(what: str, timeout_s: float, done) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if done():
            return
        time.sleep(0.5)
    raise lib.SfveError(f"timed out waiting for {what}")


def hand_over(launcher: Path, link: str, what: str, arrived: str, failures: list[str]) -> None:
    """A second Launcher.exe started with `link`, as the browser starts it:
    it must hand the link to the running game and exit 0, and the game's
    runtime must take it."""
    second = subprocess.run([str(launcher), "--join-link", link], capture_output=True, text=True, timeout=60)
    print(f"{what}: second launcher exit code {second.returncode}")
    # The second launcher's lines: in the launcher log (each start rotates
    # it), or on its own output when its log could not be opened.
    launcher_log = lib.LOG_DIR / "launcher.log"
    logged = launcher_log.read_text(errors="replace") if launcher_log.exists() else ""
    text = "\n".join((logged, second.stdout, second.stderr))
    handed = f"Handed the {what} to the running game"
    for entry in text.splitlines():
        if handed in entry or "Started with" in entry or "already running" in entry:
            print(f"launcher: {entry.strip()}")
    if second.returncode != 0:
        failures.append(f"the second launcher exited with {second.returncode} for the {what}")
    if handed not in text:
        print(f"note: no '{handed}' line in the launcher's log or the second launcher's output")
    try:
        wait_for(f"the runtime to take the {what}", 15, lambda: lib.count_log(0, arrived) > 0)
        print(f"mod: {arrived}")
    except lib.SfveError as error:
        failures.append(str(error))


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(prog="runlink")
    p.add_argument("--shots", type=Path)
    a = p.parse_args(argv)
    lib.begin_run()
    if lib.VIA != "launcher":
        return lib.verdict("set SFVE_VIA=launcher (and SFVE_PACKAGE_DIR): links go between launchers")
    launcher = lib.PACKAGE_DIR / "Launcher.exe"
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
        # As every run starts its copies: a virtual pad on slot 0 and background
        # play (an unfocused SFV draws nothing and never leaves its title).
        lib.launch(0, "netplay", ["-sfve_pads=0", "-sfve_background=1"])
        lib.boot_to_menu(0, shot_on_timeout=Path(r"D:\sfv-re\shots\runlink-boot.png"), bring_to_front=False)
        wait_for("the room", 30, lambda: lib.request(0, "room 1 30").startswith("ok"))
        wait_for("copy 0 in its room", 60, lambda: status(0)["room"]["room"] == "joined")
        for link, what, arrived in LINKS:
            hand_over(launcher, link, what, arrived, failures)
        room = status(0)["room"]
        print(f"copy 0 after the link: room {room.get('room')}, notice '{room.get('notice', '')}'")
        if room.get("room") != "joined":
            failures.append("copy 0 left its room when the links arrived")
        if a.shots:
            a.shots.mkdir(parents=True, exist_ok=True)
            time.sleep(1)
            print(lib.screenshot(lib.status_pid(0), a.shots / "link-arrived-0.png"))
    except (lib.SfveError, TimeoutError, subprocess.TimeoutExpired) as error:
        failures.append(str(error))
    finally:
        lib.kill_all()

    return lib.verdict(failures, f"PASS: {SCHEME}:// room and match links handed to the running game and taken")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
