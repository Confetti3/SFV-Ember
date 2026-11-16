"""P2's process dies as the match ends, while P1 has frames it guessed; a watcher watches.

  python tools/runkillp2.py [--lag MS] [--shots DIR]

Three copies (development build): copy 0 hosts a private room, copy 1 and the
watcher (copy 2) join, copy 0 is P1 and copy 1 is P2, the watcher watches the
table. The room's lag simulation holds the other player's inputs back (--lag,
80 ms by default), so when the fight ends P1 has predicted frames its peer never
confirmed. As copy 0 logs that the match is decided, copy 1's process is ended
(taskkill /F /T of that copy's pid only). Then (commit da53c91, the session kept
for the watchers):
- copy 0 logs "session: kept for N watcher stream(s) after the fight; the game
  is let go" (the session goes on for the watcher; the game's battle is let go);
- copy 0 is still running 30 s later, answers on its pipe, and wrote no crash
  record after the kill;
- the watcher reaches the match's result.
Prints what it saw, and PASS or FAIL; the copies it started are ended at the end.
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

KEPT = "session: kept for"
LET_GO = "the game is let go"
CRASH_LOG = "sfv_ember-crash.log"
ALIVE_S = 30.0


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status", idempotent=True)[3:])


def wait_for(what: str, timeout_s: float, done) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if done():
            return
        time.sleep(0.5)
    raise lib.SfveError(f"timed out waiting for {what}")


def crash_log_mtime() -> float:
    path = lib.LOG_DIR / CRASH_LOG
    return path.stat().st_mtime if path.exists() else 0.0


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--lag", type=int, default=80)
    p.add_argument("--shots", type=Path)
    a = p.parse_args(argv)
    lib.begin_run()
    lib.kill_all()
    failure = ""
    with lib.run_scope():
        try:
            crash_before = crash_log_mtime()
            seeds = (101, 202, 303)
            for i in (0, 1, 2):
                lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1", f"-sfve_mash={seeds[i]}",
                                          "-sfve_mash_from=300", f"-sfve_lag={a.lag}", "-sfve_hash_every=0"])
            wait_for("the runtime on every copy", 120, lambda: all(status(i)["ember"]["host"] == "running" for i in (0, 1, 2)))
            pids = {i: lib.status_pid(i) for i in (0, 1, 2)}
            wait_for("the room", 30, lambda: lib.request(0, "room 1 30 0 0 0 -").startswith("ok"))
            invitation = ""

            def invited() -> bool:
                nonlocal invitation
                invitation = lib.request(0, "invite")
                return invitation.startswith("ok ")
            wait_for("the invitation", 60, invited)
            for i in (1, 2):
                wait_for(f"copy {i}'s join", 30, lambda i=i: lib.request(i, "join " + invitation[3:]).startswith("ok"))
            wait_for("all three copies in the room", 90,
                     lambda: all(status(i)["room"]["room"] == "joined" and status(i)["room"]["members"] == 3 for i in (0, 1, 2)))
            wait_for("copy 2's Watch", 30, lambda: lib.request(2, "watchtable 0").startswith("ok"))
            wait_for("copy 2 watching table 0", 30, lambda: bool(status(2)["room"].get("watching")))
            for i in (0, 1):
                lib.request_ok(i, f"sit 0 {i}")
            wait_for("both copies seated", 60, lambda: status(0)["room"]["slot"] == 0 and status(1)["room"]["slot"] == 1)
            for i in (0, 1):
                lib.request_ok(i, "delay 2")
            print("copy 0 pick:", lib.request_ok(0, "pick RYU:0:1 S25"))
            print("copy 1 pick:", lib.request_ok(1, "pick CNL:0:2"))
            starts = [lib.count_log(i, "netplay: room match as P") for i in (0, 1)]
            watched = lib.count_log(2, "netplay: room match watched")
            decided = lib.count_log(0, "netplay: the match is decided at frame")
            kept = lib.count_log(0, KEPT)

            def started() -> bool:
                for i in (0, 1):
                    if status(i)["room"]["canReady"]:
                        lib.request(i, "ready")
                return all(lib.count_log(i, "netplay: room match as P") > starts[i] for i in (0, 1))
            wait_for("the battle's sessions", 180, started)
            wait_for("the watcher's stream", 120, lambda: lib.count_log(2, "netplay: room match watched") > watched)
            print("the battle and the watcher's stream have started; waiting for copy 0 to decide the match")
            wait_for("copy 0 to decide the match", 240, lambda: lib.count_log(0, "netplay: the match is decided at frame") > decided)
            killed = subprocess.run(["taskkill", "/F", "/T", "/PID", str(pids[1])], capture_output=True, text=True)
            print(f"copy 1 (pid {pids[1]}) ended as copy 0 decided the match: {killed.stdout.strip() or killed.stderr.strip()}")
            if killed.returncode != 0:
                raise lib.SfveError("copy 1's process could not be ended")
            if a.shots:
                try:
                    lib.screenshot(pids[0], a.shots / "killp2-copy0.png")
                except lib.SfveError as e:
                    print("shot:", e)
            wait_for("copy 0 to keep its session for the watcher and let the game go", 60, lambda: lib.count_log(0, KEPT) > kept)
            line = [l for l in lib.log_lines(0) if KEPT in l][-1]
            print("copy 0:", line[24:240])
            if LET_GO not in line:
                raise lib.SfveError(f"copy 0's line does not say the game is let go: {line[24:240]}")
            killed_at = time.time()
            while time.time() - killed_at < ALIVE_S:
                if pids[0] not in lib.running_pids():
                    raise lib.SfveError(f"copy 0 (pid {pids[0]}) was gone {time.time() - killed_at:.0f} s after P2 was killed")
                time.sleep(1)
            s0 = status(0)
            print(f"copy 0 alive {ALIVE_S:.0f} s later: room {json.dumps(s0['room'])[:200]}")
            if crash_log_mtime() > crash_before:
                raise lib.SfveError(f"{CRASH_LOG} was written during the run")
            wait_for("the watcher's result", 120, lambda: lib.count_log(2, "netplay: match result") > 0)
            print("copy 2:", [l for l in lib.log_lines(2) if "netplay: match result" in l][-1][24:240])
            for l in lib.log_lines(0)[-14:]:
                print("  copy 0 log:", l[24:230])
        except (lib.SfveError, KeyError, TypeError) as e:
            failure = str(e) or type(e).__name__
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
