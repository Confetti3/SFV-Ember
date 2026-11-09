"""Drives this PC's copies for a test on another PC (tools/runcross.py).

  python -I tools/sfve_relay.py [--port 47999] [--minutes 45]

Listens on 127.0.0.1 only (the other PC reaches it through an SSH tunnel) and
answers one JSON request per line with one JSON reply per line:

  {"op": "launch", "index": 0, "mode": "netplay", "extra": [...]}
  {"op": "ctl", "index": 0, "line": "status"}        the copy's pipe
  {"op": "count", "index": 0, "pattern": "..."}       log lines matching
  {"op": "lines", "index": 0, "pattern": "...", "last": 20}
  {"op": "log", "index": 0}                           the whole log, as text
  {"op": "deploy"} / {"op": "kill"} / {"op": "quit"}

Run it where the games can show a window (the Ember Interactive task). It
stops after --minutes with nothing asked, or on quit, and kills the copies
it started.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402


def answer(request: dict) -> dict:
    op = request.get("op")
    index = int(request.get("index", 0))
    if op == "launch":
        lib.launch(index, request.get("mode", "netplay"), list(request.get("extra", [])))
        return {"ok": True}
    if op == "ctl":
        return {"ok": True, "reply": lib.request(index, str(request["line"]), float(request.get("timeout", 5.0)))}
    if op in ("count", "lines"):
        pattern = re.compile(str(request["pattern"]))
        path = lib.log_path(index)
        lines = path.read_text(errors="replace").splitlines() if path.exists() else []
        found = [line for line in lines if pattern.search(line)]
        if op == "count":
            return {"ok": True, "count": len(found)}
        return {"ok": True, "lines": found[-int(request.get("last", 20)):]}
    if op == "log":
        path = lib.log_path(index)
        return {"ok": True, "text": path.read_text(errors="replace") if path.exists() else ""}
    if op == "deploy":
        return {"ok": True, "copied": lib.deploy(adopt=True)}
    if op == "kill":
        return {"ok": True, "killed": lib.kill_all(all_runs=True)}
    raise lib.SfveError(f"unknown op {op!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=47999)
    parser.add_argument("--minutes", type=float, default=45.0)
    args = parser.parse_args()
    lib.begin_run()
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", args.port))
    server.listen(1)
    server.settimeout(1.0)
    print(f"relay listening on 127.0.0.1:{args.port}", flush=True)
    idle_until = time.monotonic() + args.minutes * 60
    running = True
    try:
        while running and time.monotonic() < idle_until:
            try:
                connection, _ = server.accept()
            except socket.timeout:
                continue
            with connection, connection.makefile("rwb") as stream:
                for raw in stream:
                    idle_until = time.monotonic() + args.minutes * 60
                    try:
                        request = json.loads(raw)
                        if request.get("op") == "quit":
                            running = False
                            reply = {"ok": True}
                        else:
                            reply = answer(request)
                    except Exception as error:  # noqa: BLE001 - every failure goes back to the caller
                        reply = {"ok": False, "error": f"{type(error).__name__}: {error}"}
                    stream.write((json.dumps(reply) + "\n").encode())
                    stream.flush()
                    if not running:
                        break
    finally:
        print(f"relay stopping; killed {lib.kill_all()}", flush=True)
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
