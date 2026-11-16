"""Records a room battle on two copies, then plays each copy's replay back in SFV.

  python tools/runreplay.py [--rematches N] [--lag MS] [--seeks] [--shots DIR] [--keep] [--replays DIR]

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
With --seeks copy 0's playback is also sought (docs/design/beta10/replay-seek.md): paused, it
steps back three frames (each lands exactly one frame earlier), jumps back and forward five
seconds, restarts once (to the earliest landing, the status's "floor"), marks an A-B loop and
lets it loop twice, then plays to the end. The checkpoints it passes again after each seek
must equal the recording's, and the playback must still end whole.
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


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status", idempotent=True)[3:])


def ctl(index: int, line: str) -> str:
    return lib.request(index, line)


def ctl_ok(index: int, line: str) -> str:
    """A command that must be taken (a press, a sit, a pick, a transport toggle): a refusal raises."""
    return lib.request_ok(index, line)


def shell(index: int) -> dict:
    return status(index)["shell"]


def press(index: int, button: str) -> dict:
    ctl_ok(index, f"pad 0 {button}:120 -:250")
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
        ctl_ok(i, f"sit 0 {i}")
    wait_for("both copies seated", 60, lambda: status(0)["room"]["slot"] == 0 and status(1)["room"]["slot"] == 1)
    for i in (0, 1):
        ctl_ok(i, "delay 2")
    ctl_ok(0, "pick RYU:0:1 S25")
    ctl_ok(1, "pick CNL:0:2")
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
    ctl_ok(0, "replay pause")
    wait_for("the pause in the intro", 5, lambda: replays(0)["paused"])
    intro = replays(0)
    time.sleep(1.0)
    if replays(0)["frames"] != intro["frames"]:
        raise lib.SfveError("paused in the intro, the playback went on")
    if intro["cursor"] != 0:
        raise lib.SfveError(f"the pause meant for the intro came {intro['cursor']} frames into the fight")
    ctl_ok(0, "replay pause")
    wait_for("the playback again after the intro's pause", 5, lambda: not replays(0)["paused"])
    print(f"copy 0: paused in the intro at frame {intro['frames']}, before any fight frame")
    wait_for("copy 0's playback 20 s in", 120, lambda: replays(0)["frames"] > 1200)
    session = replays(0)["session"]
    ctl_ok(0, "replay pause")
    wait_for("the pause", 5, lambda: replays(0)["paused"])
    before = replays(0)["frames"]
    time.sleep(1.5)
    still = replays(0)["frames"]
    if still != before:
        raise lib.SfveError(f"paused, the playback went on ({before} to {still} frames)")
    shot(shots, "3-playback-paused", 0)
    for _ in range(3):
        ctl_ok(0, "replay step")
        time.sleep(0.3)
    stepped = replays(0)["frames"]
    if stepped != still + 3:
        raise lib.SfveError(f"three steps played {stepped - still} frames")
    ctl_ok(0, "replay pause")
    wait_for("the playback again", 5, lambda: not replays(0)["paused"])
    ctl_ok(0, "replay slower")
    wait_for("half speed", 5, lambda: replays(0)["divisor"] == 2)
    a = replays(0)["frames"]
    time.sleep(2)
    b = replays(0)["frames"]
    shot(shots, "4-playback-half", 0)
    ctl_ok(0, "replay faster")
    wait_for("full speed", 5, lambda: replays(0)["divisor"] == 1)
    c = replays(0)["frames"]
    time.sleep(2)
    d = replays(0)["frames"]
    print(f"copy 0: paused {still - before} frames in 1.5 s; 3 steps {stepped - still} frames; "
          f"1/2 speed {b - a} frames in 2 s; 1x {d - c} frames in 2 s (session {session})")
    if not (b - a) < (d - c) * 0.7:
        raise lib.SfveError("half speed did not play slower than 1x")


def landed(index: int, seeks_before: int, what: str) -> dict:
    """Waits for one more seek to land on `index`'s playback; its status."""
    wait_for(what, 20, lambda: replays(index)["seeks"] > seeks_before and not replays(index)["seeking"])
    time.sleep(0.3)  # the status is the engine frame's: let it settle
    return replays(index)


def exercise_seeks(shots: Path | None) -> int:
    """Steps back, jumps, restarts and loops copy 0's playback, paused so each seek lands where it
    was asked. Returns how many seeks it made (the playback's own count must say as many)."""
    wait_for("copy 0 deep in the fight with a state to go back to", 180,
             lambda: replays(0)["floor"] >= 0 and replays(0)["cursor"] > 900)
    if not replays(0)["paused"]:
        ctl(0, "replay pause")
        wait_for("the pause", 5, lambda: replays(0)["paused"])
    time.sleep(0.5)
    r = replays(0)
    made, f0, floor, last = r["seeks"], r["frame"], r["floor"], r["last"]
    shot(shots, "6-seek-start", 0)
    # Three steps back: exactly one frame each, the playback paused throughout.
    for k in range(1, 4):
        ctl_ok(0, "replay stepback")
        r = landed(0, made, f"step back {k}")
        made += 1
        if r["frame"] != f0 - k or not r["paused"]:
            raise lib.SfveError(f"step back {k} landed at frame {r['frame']} (paused {r['paused']}), not {f0 - k}")
    ctl(0, "replay step")
    time.sleep(0.5)
    if replays(0)["frame"] != f0 - 2:
        raise lib.SfveError(f"a step after the step backs is at frame {replays(0)['frame']}, not {f0 - 2}")
    # Five seconds back, then forward: to where the recording has a fight frame at or before (after) the target.
    here = replays(0)["frame"]
    ctl_ok(0, "replay jump -5")
    r = landed(0, made, "the jump back")
    made += 1
    want = max(floor, here - 300)
    if not (floor <= r["frame"] <= want and (r["frame"] == floor or want - r["frame"] < 400)):
        raise lib.SfveError(f"a jump back from frame {here} landed at {r['frame']} (wanted {want}, floor {floor})")
    back = r["frame"]
    shot(shots, "7-seek-jumped-back", 0)
    ctl_ok(0, "replay jump +5")
    r = landed(0, made, "the jump forward")
    made += 1
    if not (back + 300 <= r["frame"] < back + 300 + 400 or r["frame"] == last):
        raise lib.SfveError(f"a jump forward from frame {back} landed at {r['frame']} (last {last})")
    # Restart: the earliest landing, once.
    ctl_ok(0, "replay restart")
    r = landed(0, made, "the restart")
    made += 1
    if r["frame"] != floor:
        raise lib.SfveError(f"a restart landed at frame {r['frame']}, the floor is {floor}")
    shot(shots, "8-seek-restarted", 0)
    # An A-B loop: A here, B 240 frames on (a seek by number), then let it run twice and turn it off.
    ctl_ok(0, "replay loop")
    wait_for("A marked", 5, lambda: replays(0)["loop"]["phase"] == 1)
    ctl_ok(0, f"replay seek {floor + 240}")
    r = landed(0, made, "the seek to B")
    made += 1
    ctl_ok(0, "replay loop")
    wait_for("the loop on", 5, lambda: replays(0)["loop"]["phase"] == 2)
    a, b = replays(0)["loop"]["a"], replays(0)["loop"]["b"]
    if not (a == floor and a < b):
        raise lib.SfveError(f"the loop's marks are {a} to {b} (the floor is {floor})")
    shot(shots, "9-seek-loop", 0)
    wraps_from = replays(0)["seeks"]
    ctl(0, "replay pause")
    wait_for("the playback to loop twice", 60, lambda: replays(0)["seeks"] >= wraps_from + 2)
    seen = replays(0)["frame"]
    if not (a <= seen <= b + 60):
        raise lib.SfveError(f"looping {a} to {b}, the playback is at frame {seen}")
    made += replays(0)["seeks"] - wraps_from
    ctl_ok(0, "replay loop")
    wait_for("the loop off", 5, lambda: replays(0)["loop"]["phase"] == 0)
    print(f"copy 0: seeks: 3 step backs from frame {f0}, jump back to {back}, forward, restart to {floor}, "
          f"loop {a} to {b} twice; the status counts {replays(0)['seeks']} seek(s)")
    return made


def checkpoints_compared(index: int) -> tuple[int, int, str]:
    """The playback's checkpoint lines against the battle's, in the same log:
    (compared, differing, the mod's own summary)."""
    lines = log_lines(index)
    watching = max(n for n, l in enumerate(lines) if "replays: watching" in l)
    battle_start = max(n for n, l in enumerate(lines[:watching]) if "netplay: room match as P" in l)
    battle = lib.parse_checkpoints(lines[battle_start:watching])
    played = lib.parse_checkpoints(lines[watching:])
    compared, differing = lib.compare_checkpoints(battle, played)
    summary = [l for l in lines[watching:] if "replays: playback ended" in l]
    return compared, differing, summary[-1][24:] if summary else ""


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
    p.add_argument("--seeks", action="store_true", help="also step back, jump, restart and loop copy 0's playback")
    p.add_argument("--shots", type=Path)
    p.add_argument("--replays", type=Path, help="where the copies save their replays (a scratch folder by default)")
    p.add_argument("--keep", action="store_true", help="leave the copies running")
    a = p.parse_args(argv)
    lib.begin_run()
    with lib.run_scope(keep=a.keep):
        return run(a)


def run(a) -> int:
    if a.shots:
        a.shots.mkdir(parents=True, exist_ok=True)
    root = a.replays or Path(tempfile.mkdtemp(prefix="sfve-replays-"))
    folders = [root / f"copy{i}" for i in (0, 1)]
    lib.kill_all()
    failure = ""
    try:
        for i, seed in ((0, 101), (1, 202)):
            os.environ["SFVE_REPLAY_DIR"] = str(folders[i])
            try:
                lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1", f"-sfve_mash={seed}",
                                          "-sfve_mash_from=300", f"-sfve_lag={a.lag}"])
            finally:
                os.environ.pop("SFVE_REPLAY_DIR", None)
        play_battle(a.rematches)
        for i in (0, 1):
            ctl_ok(i, "leave")
        wait_for("both copies out of the room", 60, lambda: all(status(i)["room"]["room"] != "joined" for i in (0, 1)))
        time.sleep(2)
        reply = ctl(1, "replay watch latest")
        if not reply.startswith("ok"):
            raise lib.SfveError(f"copy 1's replay watch: {reply}")
        watch_through_shell(a.shots)
        wait_for("copy 1's playback", 90, lambda: replays(1)["playing"] or replays(1)["last"] != "none")
        exercise_transport(a.shots)
        seeks_made = exercise_seeks(a.shots) if a.seeks else 0
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
            if i == 0 and a.seeks:
                # Whole by having been to the last frame, the seeks counted, and every checkpoint equal.
                s = re.search(r"(\d+) frame\(s\) played in all, (\d+) seek\(s\)", summary)
                if not s or int(s.group(2)) < seeks_made or int(s.group(1)) <= int(m.group(2) if m else 0):
                    failure = failure or f"copy 0's playback did not report its seeks ({seeks_made} made): {summary or 'no summary'}"
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
        for line in lib.desync_lines(log_lines(i) if lib.log_path(i).exists() else []):
            print(f"copy {i}: {line[24:]}")
            failure = failure or f"copy {i}'s log has a desync line: {line[24:].strip()}"
    if not a.keep:
        lib.kill_all()
        if not a.replays:
            shutil.rmtree(root, ignore_errors=True)
    return lib.verdict(failure, f"PASS: {a.rematches + 1} game(s) recorded and played back on both copies")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
