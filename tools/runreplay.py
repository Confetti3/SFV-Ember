"""Records a room battle on two copies, then plays each copy's replay back in SFV.

  python tools/runreplay.py [--rematches N] [--lag MS] [--shots DIR] [--keep] [--replays DIR]

EMBER_LIBRARY.md section 26. Two copies play a private room's battle with
seeded random input (the room test commands, as runroom.ps1 does), each saving
its replay of it into its own folder (SFVE_REPLAY_DIR, a scratch folder unless
--replays names one). With --rematches N the table has Instant rematch on and
the battle plays N + 1 games, the last WIN going back to the room.

Then both copies leave the room and watch their own replay:
- copy 0 through Ember's shell with its virtual pad, as a player does: Start,
  Replays, the newest replay's row, Select, Watch now. While it plays, the
  pipe's transport commands pause it (the battle must stand still), step it
  three frames (exactly three), slow it to 1/2 and back, as the strip's keys
  do;
- copy 1 through the pipe ("replay watch latest").

Each playback must play every recorded frame and end where the recording
ends, and its checkpoints must equal the battle's: those the mod compared with
the file ("playback ended: ... 0 differing"), and, independently here, the
playback's "checkpoint:" log lines against the same copy's lines of the
battle. The two copies' files must hold the same frames and checkpoints.
-shots takes the Replays list, the row's choices, the paused and slowed
playback with its strip, and the list again after it. Prints PASS or FAIL.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

PRESS_S = 0.6
CHECKPOINT = re.compile(r"checkpoint: frame (\d+) hash ([0-9a-f]+)")


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])


def ctl(index: int, line: str) -> str:
    return lib.request(index, line)


def shell(index: int) -> dict:
    return status(index)["shell"]


def press(index: int, button: str) -> dict:
    lib.request(index, f"pad 0 {button}:120 -:250")
    time.sleep(PRESS_S)
    return shell(index)


def wait_for(what: str, timeout_s: float, done) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        if done():
            return
        time.sleep(0.5)
    raise lib.SfveError(f"timed out waiting for {what}")


def seek(index: int, accept, moves: list[str], limit: int = 12) -> None:
    """Presses each move in turn, `limit` times, until the focus is accepted."""
    trail = []
    for move in moves:
        for _ in range(limit):
            focus = shell(index)["focus"]
            trail.append(focus)
            if accept(focus):
                return
            press(index, move)
    if not accept(shell(index)["focus"]):
        raise lib.SfveError(f"copy {index}: the focus went {' > '.join(dict.fromkeys(trail))}")


def shot(shots: Path | None, name: str, index: int) -> None:
    if shots:
        lib.screenshot(lib.status_pid(index), shots / f"{name}.png")


def replays(index: int) -> dict:
    return status(index)["replays"]


def log_lines(index: int) -> list[str]:
    return lib.log_path(index).read_text(encoding="utf-8", errors="replace").splitlines()


def play_battle(rematches: int) -> None:
    """The room, its seats and picks, Ready, and the battle to its end."""
    wait_for("the runtime on both copies", 120, lambda: all(status(i)["ember"]["host"] == "running" for i in (0, 1)))
    instant = 1 if rematches > 0 else 0
    wait_for("the room", 30, lambda: ctl(0, f"room 1 30 {instant} 0").startswith("ok"))
    invitation = ""

    def invited() -> bool:
        nonlocal invitation
        invitation = ctl(0, "invite")
        return invitation.startswith("ok ")
    wait_for("the invitation", 60, invited)
    wait_for("the join", 30, lambda: ctl(1, "join " + invitation[3:]).startswith("ok"))
    wait_for("both copies in the room", 90,
             lambda: all(status(i)["room"]["room"] == "joined" and status(i)["room"]["members"] == 2 for i in (0, 1)))
    for i in (0, 1):
        ctl(i, f"sit 0 {i}")
    wait_for("both copies seated", 60, lambda: status(0)["room"]["slot"] == 0 and status(1)["room"]["slot"] == 1)
    for i in (0, 1):
        ctl(i, "delay 2")
    ctl(0, "pick RYU:0:1 S25")
    ctl(1, "pick CNL:0:2")
    starts = [lib.count_log(i, "netplay: room match as P") for i in (0, 1)]

    def started() -> bool:
        for i in (0, 1):
            if status(i)["room"]["canReady"]:
                ctl(i, "ready")
        return all(lib.count_log(i, "netplay: room match as P") > starts[i] for i in (0, 1))
    wait_for("the battle's sessions", 180, started)
    for game in range(1, rematches + 2):
        last = game == rematches + 1
        results = [lib.count_log(i, "netplay: match result") for i in (0, 1)]
        answers = [lib.count_log(i, "the WIN's answers") for i in (0, 1)]
        wait_for(f"game {game}'s result", 240, lambda: all(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)))
        if rematches > 0:
            say = ["yes", "no"] if last else ["yes", "yes"]

            def decided() -> bool:
                for i in (0, 1):
                    r = status(i)["battle"]["mode"]["rematch"]
                    if r["prompt"] and r["offered"]:
                        ctl(i, f"rematch {say[i]}")
                return all(lib.count_log(i, "the WIN's answers") > answers[i] for i in (0, 1))
            wait_for(f"the WIN's answers after game {game}", 60, decided)
        print(f"game {game}: " + [l for l in log_lines(0) if "netplay: match result" in l][-1][24:])
    wait_for("both copies back in the room", 120,
             lambda: all(lib.count_log(i, "battle closed at its match") > 0 and status(i)["room"]["room"] == "joined" for i in (0, 1)))
    wait_for("both copies' replays saved", 30, lambda: all(lib.count_log(i, "replays: saved") > 0 for i in (0, 1)))
    for i in (0, 1):
        print(f"copy {i}: " + [l for l in log_lines(i) if "replays: saved" in l][-1][24:])


def watch_through_shell(shots: Path | None) -> None:
    """Copy 0: Ember (Start if it is closed), Replays, the newest row, Select, Watch now."""
    if not shell(0)["shown"]:
        press(0, "START")
    if shell(0)["screen"] == "welcome":
        seek(0, lambda f: f == "welcome-skip", ["DOWN", "DOWN", "RIGHT"])
        press(0, "A")
    for _ in range(6):  # back to Home from wherever the menu opened
        if shell(0)["screen"] == "home":
            break
        press(0, "B")
    wait_for("Ember's Home", 10, lambda: shell(0)["shown"] and shell(0)["screen"] == "home")
    seek(0, lambda f: f == "replays", ["DOWN", "RIGHT", "UP", "LEFT"])
    press(0, "A")
    wait_for("the Replays screen", 10, lambda: shell(0)["screen"] == "replays")
    seek(0, lambda f: f.startswith("replay:"), ["DOWN", "RIGHT"])
    time.sleep(1.5)  # the listing, the portraits
    shot(shots, "1-replays-list", 0)
    print("copy 0: the Replays screen's row " + shell(0)["focus"])
    press(0, "A")  # the row's choices, Watch now first
    time.sleep(0.8)
    shot(shots, "2-replay-choices", 0)
    print("copy 0: choices open: " + json.dumps(shell(0)))
    press(0, "A")
    wait_for("copy 0's playback", 90, lambda: replays(0)["playing"])
    if shell(0)["shown"]:
        raise lib.SfveError("Ember's menu stayed over the playback")


def exercise_transport(shots: Path | None) -> None:
    """Pause, step three frames, half speed and back, on copy 0's playback;
    first a pause before the fight (the intro: no fight frame played yet)."""
    wait_for("copy 0's playback 1 s in", 60, lambda: replays(0)["frames"] > 60)
    ctl(0, "replay pause")
    wait_for("the pause in the intro", 5, lambda: replays(0)["paused"])
    intro = replays(0)
    time.sleep(1.0)
    if replays(0)["frames"] != intro["frames"]:
        raise lib.SfveError("paused in the intro, the playback went on")
    if intro["cursor"] != 0:
        raise lib.SfveError(f"the pause meant for the intro came {intro['cursor']} frames into the fight")
    ctl(0, "replay pause")
    wait_for("the playback again after the intro's pause", 5, lambda: not replays(0)["paused"])
    print(f"copy 0: paused in the intro at frame {intro['frames']}, before any fight frame")
    wait_for("copy 0's playback 20 s in", 120, lambda: replays(0)["frames"] > 1200)
    session = replays(0)["session"]
    ctl(0, "replay pause")
    wait_for("the pause", 5, lambda: replays(0)["paused"])
    before = replays(0)["frames"]
    time.sleep(1.5)
    still = replays(0)["frames"]
    if still != before:
        raise lib.SfveError(f"paused, the playback went on ({before} to {still} frames)")
    shot(shots, "3-playback-paused", 0)
    for _ in range(3):
        ctl(0, "replay step")
        time.sleep(0.3)
    stepped = replays(0)["frames"]
    if stepped != still + 3:
        raise lib.SfveError(f"three steps played {stepped - still} frames")
    ctl(0, "replay pause")
    wait_for("the playback again", 5, lambda: not replays(0)["paused"])
    ctl(0, "replay slower")
    wait_for("half speed", 5, lambda: replays(0)["divisor"] == 2)
    a = replays(0)["frames"]
    time.sleep(2)
    b = replays(0)["frames"]
    shot(shots, "4-playback-half", 0)
    ctl(0, "replay faster")
    wait_for("full speed", 5, lambda: replays(0)["divisor"] == 1)
    c = replays(0)["frames"]
    time.sleep(2)
    d = replays(0)["frames"]
    print(f"copy 0: paused {still - before} frames in 1.5 s; 3 steps {stepped - still} frames; "
          f"1/2 speed {b - a} frames in 2 s; 1x {d - c} frames in 2 s (session {session})")
    if not (b - a) < (d - c) * 0.7:
        raise lib.SfveError("half speed did not play slower than 1x")


def checkpoints_compared(index: int) -> tuple[int, int, str]:
    """The playback's checkpoint lines against the battle's, in the same log:
    (compared, differing, the mod's own summary)."""
    lines = log_lines(index)
    watching = max(n for n, l in enumerate(lines) if "replays: watching" in l)
    battle_start = max(n for n, l in enumerate(lines[:watching]) if "netplay: room match as P" in l)
    battle = {int(m.group(1)): m.group(2) for l in lines[battle_start:watching] if (m := CHECKPOINT.search(l))}
    played = {int(m.group(1)): m.group(2) for l in lines[watching:] if (m := CHECKPOINT.search(l))}
    common = [f for f in played if f in battle]
    differing = [f for f in common if played[f] != battle[f]]
    summary = [l for l in lines[watching:] if "replays: playback ended" in l]
    return len(common), len(differing), summary[-1][24:] if summary else ""


def read_replay(path: Path) -> tuple[dict, list, list]:
    """The file's header, frames (expanded) and checkpoints (core/ReplayFile.hxx)."""
    data = path.read_bytes()
    if data[:8] != b"SFVEREPL":
        raise lib.SfveError(f"{path} is not a replay")
    at = 12
    (n,) = struct.unpack_from("<I", data, at)
    header = json.loads(data[at + 4:at + 4 + n])
    at += 4 + n
    (runs,) = struct.unpack_from("<I", data, at)
    at += 4
    frames = []
    for _ in range(runs):
        count, p1, p2, flags = struct.unpack_from("<IIIB", data, at)
        at += 13
        frames += [(p1, p2, flags)] * count
    (n,) = struct.unpack_from("<I", data, at)
    at += 4
    checkpoints = [struct.unpack_from("<iQ", data, at + 12 * k) for k in range(n)]
    return header, frames, checkpoints


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--rematches", type=int, default=0)
    p.add_argument("--lag", type=int, default=30)
    p.add_argument("--shots", type=Path)
    p.add_argument("--replays", type=Path, help="where the copies save their replays (a scratch folder by default)")
    p.add_argument("--keep", action="store_true", help="leave the copies running")
    a = p.parse_args(argv)
    lib.begin_run()
    if a.shots:
        a.shots.mkdir(parents=True, exist_ok=True)
    root = a.replays or Path(tempfile.mkdtemp(prefix="sfve-replays-"))
    folders = [root / f"copy{i}" for i in (0, 1)]
    lib.kill_all()
    for i, seed in ((0, 101), (1, 202)):
        os.environ["SFVE_REPLAY_DIR"] = str(folders[i])
        lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1", f"-sfve_mash={seed}",
                                  "-sfve_mash_from=300", f"-sfve_lag={a.lag}"])
    os.environ.pop("SFVE_REPLAY_DIR", None)
    failure = ""
    try:
        play_battle(a.rematches)
        for i in (0, 1):
            ctl(i, "leave")
        wait_for("both copies out of the room", 60, lambda: all(status(i)["room"]["room"] != "joined" for i in (0, 1)))
        time.sleep(2)
        reply = ctl(1, "replay watch latest")
        if not reply.startswith("ok"):
            raise lib.SfveError(f"copy 1's replay watch: {reply}")
        watch_through_shell(a.shots)
        wait_for("copy 1's playback", 90, lambda: replays(1)["playing"] or replays(1)["last"] != "none")
        exercise_transport(a.shots)
        wait_for("both playbacks to end", 400, lambda: all(replays(i)["last"] != "none" and not replays(i)["playing"] and
                                                           replays(i)["step"] == 0 for i in (0, 1)))
        wait_for("copy 0's shell back on the Replays screen", 30,
                 lambda: shell(0)["shown"] and shell(0)["screen"] == "replays")
        time.sleep(2)
        shot(a.shots, "5-replays-after-playback", 0)
        for i in (0, 1):
            compared, differing, summary = checkpoints_compared(i)
            r = replays(i)
            print(f"copy {i}: playback {summary}")
            print(f"copy {i}: the playback's checkpoint lines against the battle's: {compared} compared, {differing} differing")
            if not compared or differing:
                failure = failure or f"copy {i}'s playback checkpoints: {compared} compared, {differing} differing"
            m = re.search(r"(\d+) of (\d+) frame\(s\) played, (\d+) checkpoint\(s\) compared with the recording, (\d+) differing", summary)
            if not m or m.group(1) != m.group(2) or m.group(4) != "0" or m.group(3) == "0":
                failure = failure or f"copy {i}'s playback: {summary or 'no summary'}"
            if r["notice"]:
                failure = failure or f"copy {i}'s Replays screen says: {r['notice']}"
        files = [sorted(f.glob("*.sfvreplay")) for f in folders]
        if any(len(f) != 1 for f in files):
            raise lib.SfveError(f"each copy should hold one replay: {files}")
        (h0, f0, c0), (h1, f1, c1) = read_replay(files[0][0]), read_replay(files[1][0])
        print(f"files: copy 0 slot {h0['slot']} {len(f0)} frames {len(c0)} checkpoints {len(h0['games'])} game(s); "
              f"copy 1 slot {h1['slot']} {len(f1)} frames {len(c1)} checkpoints; same frames {f0 == f1}, same checkpoints {c0 == c1}")
        if f0 != f1 or c0 != c1:
            failure = failure or "the two copies' replays of the battle differ"
        if not h0["complete"] or len(h0["games"]) != a.rematches + 1:
            failure = failure or f"copy 0's replay: complete {h0['complete']}, {len(h0['games'])} game(s)"
    except lib.SfveError as e:
        failure = str(e)
    for i in (0, 1):
        try:
            print(f"copy {i}: replays {json.dumps(replays(i))}")
        except lib.SfveError:
            print(f"copy {i} is not answering")
        if lib.log_path(i).exists():
            for line in log_lines(i):
                if "desync" in line or "mismatch" in line:
                    print(f"copy {i}: {line[24:]}")
    if not a.keep:
        lib.kill_all()
        if not a.replays:
            shutil.rmtree(root, ignore_errors=True)
    return lib.verdict(failure, f"PASS: {a.rematches + 1} game(s) recorded and played back on both copies")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
