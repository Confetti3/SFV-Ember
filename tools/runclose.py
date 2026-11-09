"""Two copies in a room: a battle of the player's own, then a normal close.

  python tools/runclose.py [--shots DIR]

EMBER_LIBRARY.md section 14 (items A1 and A2):
1. Copy 0 hosts a private room (the room test commands), copy 1 joins it.
2. Copy 1 starts a battle of its own with the pipe's "launch" (as a native
   menu would), while joined. It must play offline: the battle is not the
   room's (no "room's battle has loaded", the battle status says room false
   and no session), the battle runs, and copy 1 is still in the room.
3. Copy 0, the host, is closed as its window's close button closes it
   (WM_CLOSE, what Process.CloseMainWindow sends). It must leave the room
   from the game's exit request as Leave room does ("exit: ..." and
   "ember: left the room"), be gone within CLOSE_LIMIT_S, and its helper
   (sf4-net.exe) with it.
4. Copy 1 must see the host leave as Leave room shows it (the room goes on
   with one member, and no "Room control is recovering"), then it is closed
   the same way, from its battle.

Prints what it saw, and PASS or FAIL; any copy still running at the end is
killed.
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

CLOSE_LIMIT_S = 5.0


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])


def ctl(index: int, line: str) -> str:
    return lib.request(index, line)


def wait_for(what: str, timeout_s: float, done) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if done():
            return
        time.sleep(1)
    raise lib.SfveError(f"timed out waiting for {what}")


def helpers() -> dict[int, int]:
    """Each running helper's pid and its parent's (the copy that started it)."""
    out = subprocess.run(
        ["powershell", "-NoProfile", "-Command",
         "Get-CimInstance Win32_Process -Filter \"Name='sf4-net.exe'\" | "
         "ForEach-Object { \"$($_.ProcessId) $($_.ParentProcessId)\" }"],
        capture_output=True, text=True).stdout
    pairs = [line.split() for line in out.splitlines() if line.strip()]
    return {int(pid): int(parent) for pid, parent in pairs}


def close_and_time(index: int, pid: int) -> float:
    """WM_CLOSE to the copy's window; seconds until its process is gone."""
    started = time.monotonic()
    lib.close_window(pid)
    while pid in lib.running_pids():
        if time.monotonic() - started > 30:
            raise lib.SfveError(f"copy {index} was still running 30 s after its window was closed")
        time.sleep(0.1)
    return time.monotonic() - started


def log_lines(index: int, *texts: str, after: int = 0) -> list[str]:
    """The copy's log lines holding any of `texts`, past its first `after` lines."""
    path = lib.log_path(index)
    lines = path.read_text(errors="replace").splitlines()[after:] if path.exists() else []
    return [line for line in lines if any(text in line for text in texts)]


def log_length(index: int) -> int:
    path = lib.log_path(index)
    return len(path.read_text(errors="replace").splitlines()) if path.exists() else 0


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--shots", type=Path)
    a = p.parse_args(argv)
    lib.begin_run()
    lib.kill_all()
    for i in (0, 1):
        lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1"])
    failure = ""
    try:
        wait_for("the runtime on both copies", 120,
                 lambda: all(status(i)["ember"]["host"] == "running" for i in (0, 1)))
        pids = {i: lib.status_pid(i) for i in (0, 1)}
        wait_for("the room", 30, lambda: ctl(0, "room 1 30").startswith("ok"))
        invitation = ""

        def invited() -> bool:
            nonlocal invitation
            invitation = ctl(0, "invite")
            return invitation.startswith("ok ")
        wait_for("the invitation", 60, invited)
        wait_for("the join", 30, lambda: ctl(1, "join " + invitation[3:]).startswith("ok"))
        wait_for("both copies in the room", 90,
                 lambda: all(status(i)["room"]["room"] == "joined" and status(i)["room"]["members"] == 2 for i in (0, 1)))
        print("both copies in the room")

        # A1: a battle of copy 1's own while it is in the room.
        loaded = lib.count_log(1, "the room's battle has loaded")
        print("copy 1 launch:", ctl(1, "launch versus RYU KEN S25"))
        wait_for("copy 1's own battle", 60, lambda: status(1).get("battle") not in (None, "null")
                 and status(1)["battle"]["frames"] > 600)
        frames = status(1)["battle"]["frames"]
        time.sleep(5)
        s1 = status(1)
        mode = s1["battle"]["mode"]
        print(f"copy 1 battle: frames {frames} -> {s1['battle']['frames']}, mode {json.dumps(mode)[:160]}")
        print(f"copy 1 room: {json.dumps(s1['room'])[:200]}")
        if lib.count_log(1, "the room's battle has loaded") != loaded:
            raise lib.SfveError("copy 1's own battle was taken as the room's")
        if mode["room"] or mode["live"]:
            raise lib.SfveError("copy 1's own battle has a room session")
        if s1["battle"]["frames"] <= frames:
            raise lib.SfveError("copy 1's own battle is not running")
        if s1["room"]["room"] != "joined":
            raise lib.SfveError("copy 1 left the room for its own battle")
        if a.shots:
            lib.screenshot(pids[1], a.shots / "7-own-battle-in-room.png")
        print("A1: copy 1's own battle plays offline, still in the room")

        # A2: the host closes its window.
        guest_log = log_length(1)
        before = helpers()
        host_helpers = [pid for pid, parent in before.items() if parent == pids[0]]
        seconds = close_and_time(0, pids[0])
        time.sleep(1)
        left_over = [pid for pid in host_helpers if pid in helpers()]
        print(f"copy 0 (host) closed in {seconds:.1f} s; its helper(s) {host_helpers} left running: {left_over}")
        for line in log_lines(0, "exit:", "ember: left", "ember: leaving", "ember: the room was not left", "departure", "Leave"):
            print("  copy 0:", line[24:200])
        if seconds > CLOSE_LIMIT_S:
            raise lib.SfveError(f"the host took {seconds:.1f} s to close")
        if not host_helpers or left_over:
            raise lib.SfveError("the host's helper outlived it" if left_over else "the host had no helper")
        if not log_lines(0, "ember: left the room, stopped the helper"):
            raise lib.SfveError("the host did not leave the room from its exit request")
        wait_for("copy 1 to see the host leave", 30, lambda: status(1)["room"]["members"] == 1)
        print(f"copy 1 room after the host left: {json.dumps(status(1)['room'])[:200]}")
        for line in log_lines(1, "Room", "room", "timed out", "timeout", after=guest_log)[:12]:
            print("  copy 1:", line[24:200])
        if log_lines(1, "timed out", "timeout", "Room control is recovering", after=guest_log):
            raise lib.SfveError("copy 1 lost the host and recovered rather than saw it leave")

        seconds = close_and_time(1, pids[1])
        print(f"copy 1 closed from its battle in {seconds:.1f} s")
        if seconds > CLOSE_LIMIT_S:
            raise lib.SfveError(f"copy 1 took {seconds:.1f} s to close")
        time.sleep(1)
        if helpers():
            raise lib.SfveError(f"helpers still running: {helpers()}")
    except (lib.SfveError, KeyError, TypeError) as e:
        failure = str(e) or type(e).__name__
    lib.kill_all()
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
