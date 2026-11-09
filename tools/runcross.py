"""A room match between this PC's copy and another PC's (a desync between two PCs).

  python -I tools/runcross.py [--p1 Z40:3:0:1:2] [--p2 CNL:3:6:2:2] [--stage CHN]
                              [--games 2] [--rounds 3] [--seconds 99] [--out DIR]

This PC's copy 0 hosts a private room and plays P1; the other PC's copy 0
(through tools/sfve_relay.py, reached by an SSH tunnel to --host) joins and
plays P2. Both mash seeded random input, as tools/runroom.ps1's copies do.
Each game ends at its result or at a desync. Both logs are saved to --out;
at a desync, the regions whose hashes differ at that checkpoint are listed
(the log names each region and its hash at the failed checkpoint).
Prints PASS or FAIL. Kills both copies at the end.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

RESULT = "netplay: match result"
DESYNC = re.compile(r"desync: checkpoint (\d+) is ([0-9a-f]+) here and ([0-9a-f]+) at the other player")
REGION = re.compile(r"desync: checkpoint (\d+) region (.+), (\d+) bytes, hash ([0-9a-f]+)")
STARTED = "netplay: room match as P"


class Remote:
    def __init__(self, port: int):
        deadline = time.monotonic() + 120
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
                self.stream = self.sock.makefile("rwb")
                self.ask({"op": "count", "index": 0, "pattern": "^$"})
                return
            except (OSError, ValueError):
                if time.monotonic() > deadline:
                    raise lib.SfveError("the other PC's relay does not answer")
                time.sleep(2)

    def ask(self, request: dict) -> dict:
        self.stream.write((json.dumps(request) + "\n").encode())
        self.stream.flush()
        line = self.stream.readline()
        if not line:
            raise lib.SfveError("the relay closed the connection")
        reply = json.loads(line)
        if not reply.get("ok"):
            raise lib.SfveError(f"other PC: {reply.get('error')}")
        return reply


class Side:
    """One PC's copy 0: this one through sfvelib, the other through the relay."""

    def __init__(self, name: str, remote: Remote | None):
        self.name, self.remote = name, remote

    def ctl(self, line: str) -> str:
        if self.remote:
            return self.remote.ask({"op": "ctl", "index": 0, "line": line, "timeout": 10})["reply"]
        return lib.request(0, line, 10)

    def status(self) -> dict:
        reply = self.ctl("status")
        return json.loads(reply[3:]) if reply.startswith("ok ") else {}

    def count(self, pattern: str) -> int:
        if self.remote:
            return self.remote.ask({"op": "count", "index": 0, "pattern": re.escape(pattern) if pattern == RESULT or pattern == STARTED else pattern})["count"]
        path = lib.log_path(0)
        text = path.read_text(errors="replace").splitlines() if path.exists() else []
        rx = re.compile(re.escape(pattern) if pattern in (RESULT, STARTED) else pattern)
        return sum(1 for line in text if rx.search(line))

    def log(self) -> str:
        if self.remote:
            return self.remote.ask({"op": "log", "index": 0})["text"]
        path = lib.log_path(0)
        return path.read_text(errors="replace") if path.exists() else ""

    def launch(self, extra: list[str]) -> None:
        if self.remote:
            self.remote.ask({"op": "launch", "index": 0, "mode": "netplay", "extra": extra})
        else:
            lib.launch(0, "netplay", extra)


def wait_for(what: str, seconds: float, done) -> None:
    deadline = time.monotonic() + seconds
    while True:
        try:
            if done():
                return
        except (lib.SfveError, OSError, ValueError, KeyError):
            pass
        if time.monotonic() > deadline:
            raise lib.SfveError(f"timed out waiting for {what}")
        time.sleep(1)


def regions_at(log: str, checkpoint: str) -> dict[str, str]:
    """The last desync's region hashes at `checkpoint`, by region name."""
    found: dict[str, str] = {}
    for line in log.splitlines():
        m = REGION.search(line)
        if m and m.group(1) == checkpoint:
            found[m.group(2)] = m.group(4)
    return found


def compare(here_log: str, there_log: str, out: Path) -> list[str]:
    report = []
    here = [m.groups() for m in map(DESYNC.search, here_log.splitlines()) if m]
    there = [m.groups() for m in map(DESYNC.search, there_log.splitlines()) if m]
    for side, events in (("desktop", here), ("laptop", there)):
        for checkpoint, mine, theirs in events:
            report.append(f"{side}: desync at checkpoint {checkpoint} ({mine} here, {theirs} there)")
    if here and there:
        checkpoint = here[-1][0]
        a, b = regions_at(here_log, checkpoint), regions_at(there_log, checkpoint)
        differing = sorted(name for name in set(a) | set(b) if a.get(name) != b.get(name))
        report.append(f"checkpoint {checkpoint}: {len(a)} regions here, {len(b)} there, {len(differing)} differ")
        report += [f"  {name}: desktop {a.get(name, '-')} laptop {b.get(name, '-')}" for name in differing[:80]]
    (out / "compare.txt").write_text("\n".join(report) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="director")
    parser.add_argument("--port", type=int, default=47999)
    parser.add_argument("--p1", default="Z40:3:0:1:2")
    parser.add_argument("--p2", default="CNL:3:6:2:2")
    parser.add_argument("--stage", default="CHN")
    parser.add_argument("--games", type=int, default=2)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=99)
    parser.add_argument("--delay", type=int, default=2)
    parser.add_argument("--game-seconds", type=int, default=300)
    parser.add_argument("--out", type=Path, default=Path(r"D:\sfv-re\cross") / time.strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--extra", nargs="*", default=[], help="more game options for both copies")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    lib.begin_run()
    tunnel = subprocess.Popen(["ssh", "-N", "-o", "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=30",
                               "-L", f"{args.port}:127.0.0.1:{args.port}", args.host],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures: list[str] = []
    here = there = None
    try:
        remote = Remote(args.port)
        here, there = Side("desktop", None), Side("laptop", remote)
        remote.ask({"op": "kill"})
        lib.kill_all()
        print("laptop deploy:", remote.ask({"op": "deploy"})["copied"])
        print("desktop deploy:", lib.deploy(adopt=False))
        common = ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1", "-sfve_mash_from=300", "-sfve_lag=0"] + args.extra
        here.launch(common + ["-sfve_mash=101"])
        there.launch(common + ["-sfve_mash=202"])
        wait_for("the runtime on both PCs", 180, lambda: all(s.status().get("ember", {}).get("host") == "running" for s in (here, there)))
        wait_for("the room on the desktop", 30, lambda: here.ctl(f"room {args.rounds} {args.seconds} 0 0").startswith("ok"))
        invitation = ""

        def invited() -> bool:
            nonlocal invitation
            reply = here.ctl("invite")
            invitation = reply[3:] if reply.startswith("ok ") else ""
            return bool(invitation)
        wait_for("the invitation", 60, invited)
        wait_for("the laptop's join", 30, lambda: there.ctl(f"join {invitation}").startswith("ok"))
        wait_for("both PCs in the room", 120, lambda: all(s.status()["room"]["room"] == "joined" and s.status()["room"]["members"] == 2 for s in (here, there)))
        here.ctl("sit 0 0")
        there.ctl("sit 0 1")
        wait_for("both seated", 60, lambda: here.status()["room"]["slot"] == 0 and there.status()["room"]["slot"] == 1)
        for side in (here, there):
            side.ctl(f"delay {args.delay}")
        print("desktop pick:", here.ctl(f"pick {args.p1} {args.stage}"))
        print("laptop pick:", there.ctl(f"pick {args.p2}"))
        for game in range(1, args.games + 1):
            started = [s.count(STARTED) for s in (here, there)]
            results = [s.count(RESULT) for s in (here, there)]
            desyncs = [s.count(DESYNC.pattern) for s in (here, there)]

            def readied() -> bool:
                for side in (here, there):
                    if side.status().get("room", {}).get("canReady"):
                        side.ctl("ready")
                return all(s.count(STARTED) > n for s, n in zip((here, there), started))
            wait_for(f"game {game}'s sessions", 240, readied)
            print(f"game {game} started")

            def over() -> bool:
                return any(s.count(RESULT) > n for s, n in zip((here, there), results)) or \
                    any(s.count(DESYNC.pattern) > n for s, n in zip((here, there), desyncs))
            wait_for(f"game {game}'s end", args.game_seconds, over)
            time.sleep(5)
            if any(s.count(DESYNC.pattern) > n for s, n in zip((here, there), desyncs)):
                failures.append(f"game {game} desynced")
                print(f"game {game}: DESYNC")
                break
            print(f"game {game}: result on both" if all(s.count(RESULT) > n for s, n in zip((here, there), results)) else f"game {game}: result on one side only")
    except lib.SfveError as error:
        failures.append(str(error))
    finally:
        try:
            if here and there:
                here_log, there_log = here.log(), there.log()
                (args.out / "desktop.log").write_text(here_log)
                (args.out / "laptop.log").write_text(there_log)
                for line in compare(here_log, there_log, args.out):
                    print(line)
                there.remote.ask({"op": "kill"})
            lib.kill_all()
        except (lib.SfveError, OSError) as error:
            failures.append(f"cleanup: {error}")
        tunnel.terminate()
    print(f"logs: {args.out}")
    return lib.verdict(failures)


if __name__ == "__main__":
    sys.exit(main())
