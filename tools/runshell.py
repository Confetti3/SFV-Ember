"""Two copies play a private room through Ember's shell, driven by their pads.

  python tools/runshell.py [--shots DIR] [--games N] [--keep]

Each copy's virtual pad 0 (-sfve_pads=01) presses the shell's buttons, the
same path a physical pad takes (EMBER_LIBRARY.md section 13): copy 0 opens
Ember with Start, creates a private room of 1 round of 30 s and copies its
invitation; copy 1 joins by pasting it from the clipboard; both take a seat,
pick on the pick screen and Ready on their own seat. The match plays with
seeded random input, and both copies must be back in the room after it.
Nothing goes through the room test commands: the pipe only reads the status
and presses the pad. Prints PASS or FAIL; closes the copies unless --keep.

The clipboard is the Windows one, shared by both copies: the run replaces
what it held with the room's invitation.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import sfvelib as lib  # noqa: E402

PRESS_S = 0.6  # a press (120 ms), its release, and a frame or two to draw


def status(index: int) -> dict:
    return json.loads(lib.request(index, "status")[3:])


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
        time.sleep(1)
    raise lib.SfveError(f"timed out waiting for {what}")


def expect(index: int, screen: str, focus: str | None = None) -> None:
    s = shell(index)
    if s["screen"] != screen or (focus is not None and s["focus"] != focus):
        raise lib.SfveError(f"copy {index}: expected {screen}/{focus}, shell is at {s['screen']}/{s['focus']}")


def seek(index: int, focus: str, moves: list[str], limit: int = 12) -> None:
    """Presses `moves` in turn, round and round, until the focus is `focus`."""
    for step in range(limit):
        if shell(index)["focus"] == focus:
            return
        press(index, moves[step % len(moves)])
    if shell(index)["focus"] != focus:
        raise lib.SfveError(f"copy {index}: could not reach {focus} (at {shell(index)['focus']})")


def open_home(index: int) -> None:
    """Start opens Ember; the first-run welcome is skipped."""
    press(index, "START")
    if shell(index)["screen"] == "welcome":
        seek(index, "welcome-skip", ["DOWN", "DOWN", "RIGHT"])
        press(index, "A")
    expect(index, "home")


def shot(shots: Path | None, name: str, index: int) -> None:
    if shots:
        lib.screenshot(lib.status_pid(index), shots / f"{name}.png")


def create_room(shots: Path | None) -> None:
    open_home(0)
    shot(shots, "1-menu", 0)
    seek(0, "online", ["UP"])
    press(0, "A")
    expect(0, "online", "create")
    press(0, "A")
    expect(0, "create")
    # Table rules: 1 round of 30 s, the shortest SFV plays.
    seek(0, "rounds", ["DOWN", "RIGHT", "UP"], 20)
    press(0, "A")  # every round count SFV plays
    shot(shots, "2-rules-rounds", 0)
    press(0, "B")
    for _ in range(3):
        press(0, "LEFT")
    press(0, "DOWN")
    expect(0, "create", "time")
    press(0, "LEFT")
    press(0, "LEFT")
    seek(0, "host", ["DOWN"])
    press(0, "A")
    wait_for("copy 0's room", 60, lambda: status(0)["room"]["room"] == "joined" and shell(0)["screen"] == "room")


def copy_invitation(shots: Path | None) -> None:
    # The room's bar (copy, copy-short, room-more, leave) is below its tables.
    seek(0, "room-more", ["DOWN"], 20)
    seek(0, "copy", ["LEFT"], 4)
    press(0, "A")
    shot(shots, "3-room-invitation", 0)


def join_room() -> None:
    open_home(1)
    seek(1, "online", ["UP"])
    press(1, "A")
    seek(1, "join", ["DOWN"])
    press(1, "A")
    expect(1, "join", "paste")
    press(1, "A")  # the invitation from the clipboard
    seek(1, "join-now", ["DOWN"])
    press(1, "A")
    wait_for("copy 1 in the room", 90, lambda: status(1)["room"]["room"] == "joined" and status(0)["room"]["members"] == 2)


def take_seat(index: int, seat: int) -> None:
    """Table 1's card opens its seat chooser, whose first choice is its first
    open seat: P1 for the first to sit, P2 for the second."""
    seek(index, "table-0", ["UP"], 20)
    press(index, "A")
    press(index, "A")
    wait_for(f"copy {index} seated", 60, lambda: status(index)["room"]["slot"] == seat)


def pick(index: int, fighter_steps: int, shots: Path | None, name: str | None) -> None:
    """X opens the pick screen: the fighter `fighter_steps` along, costume 0, colour 1."""
    press(index, "X")
    expect(index, "selection")
    for _ in range(fighter_steps):
        press(index, "RIGHT")
    press(index, "DOWN")  # costume
    press(index, "DOWN")  # colour
    press(index, "RIGHT")
    if name:
        shot(shots, name, index)
    press(index, "B")
    expect(index, "room")


def not_owned_shots(shots: Path | None) -> None:
    """Copy 0, after its pick, with --unowned CNL,KEN:1,KEN:0:1 (the pick is
    KEN's costume 0 in colour 03, the fitting having skipped CNL and colour
    02): each row's choices opened (Select) and the focus moved onto the
    choice its player does not own (CHUN-LI beside KEN, KEN's costume 1, his
    colour 02), where the pane says NOT OWNED's words."""
    if not shots:
        return
    press(0, "X")  # the screen opens on its first row, the fighter
    expect(0, "selection")
    for row, moves in (("fighter", ["LEFT"]), ("costume", ["RIGHT"]), ("color", ["UP", "RIGHT"])):
        if row != "fighter":
            press(0, "DOWN")
        press(0, "A")
        for move in moves:
            press(0, move)
        time.sleep(0.5)
        shot(shots, f"4b-not-owned-{row}", 0)
        press(0, "B")  # the choice closes; the pick is as it was
    press(0, "B")
    expect(0, "room")


def room_shots(shots: Path | None) -> None:
    """Copy 1, a guest seated as P2 with a pick, before Ready: its seat focused
    (the room, its members, the hint bar and what Ready will say), then its
    table page (Y: the table's rules as a guest reads them)."""
    if not shots:
        return
    seek(1, "table-0", ["UP"], 20)
    time.sleep(1)
    shot(shots, "5-room-seated", 1)
    press(1, "Y")
    expect(1, "room-table")
    shot(shots, "6-table-rules", 1)
    press(1, "B")
    expect(1, "room")


def ready_both(game: int) -> None:
    """A on the player's own seat readies. A Ready carries its pick, and a seat
    whose fighter changes takes back the other seat's Ready (the room's rule),
    so each copy readies whenever it can until both sessions start."""
    starts = [lib.count_log(i, "netplay: room match as P") for i in (0, 1)]

    def started() -> bool:
        for i in (0, 1):
            if status(i)["room"]["canReady"]:
                seek(i, "table-0", ["UP"], 20)
                press(i, "A")
        return all(lib.count_log(i, "netplay: room match as P") > starts[i] for i in (0, 1))
    wait_for(f"game {game}'s sessions", 180, started)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--shots", type=Path)
    p.add_argument("--games", type=int, default=1)
    p.add_argument("--keep", action="store_true")
    p.add_argument("--unowned", help="copy 0's -sfve_unowned (CODE[:costume[:colour]],...): content its player "
                   "may not pick, to show the pick screen's NOT OWNED")
    a = p.parse_args(argv)
    if a.shots:
        a.shots.mkdir(parents=True, exist_ok=True)
    lib.kill_all()
    for i, seed in ((0, 101), (1, 202)):
        unowned = [f"-sfve_unowned={a.unowned}"] if a.unowned and i == 0 else []
        lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1", f"-sfve_mash={seed}",
                                  "-sfve_mash_from=300"] + unowned)
    failure = ""
    try:
        wait_for("the runtime on both copies", 120,
                 lambda: all(status(i)["ember"]["host"] == "running" for i in (0, 1)))
        create_room(a.shots)
        copy_invitation(a.shots)
        join_room()
        take_seat(0, 0)
        take_seat(1, 1)
        pick(0, 2, a.shots, "4-pick")
        pick(1, 3, None, None)
        if a.unowned:
            not_owned_shots(a.shots)
        room_shots(a.shots)
        for game in range(1, a.games + 1):
            results = [lib.count_log(i, "netplay: match result") for i in (0, 1)]
            closes = [lib.count_log(i, "battle closed at its match") for i in (0, 1)]
            desyncs = [lib.count_log(i, "desync:") for i in (0, 1)]
            ready_both(game)

            def decided() -> bool:
                if any(lib.count_log(i, "desync:") > desyncs[i] for i in (0, 1)):
                    raise lib.SfveError(f"game {game} desynced (see the copies' logs)")
                return all(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1))
            wait_for(f"game {game}'s result on both copies", 300, decided)
            wait_for(f"both copies back in the room after game {game}", 120,
                     lambda: all(lib.count_log(i, "battle closed at its match") > closes[i] and
                                 status(i)["room"]["room"] == "joined" and shell(i)["shown"] and
                                 shell(i)["screen"] == "room" for i in (0, 1)))
            time.sleep(2)
            shot(a.shots, f"8-back-in-room-game{game}", 0)
    except lib.SfveError as e:
        failure = str(e)
    for i in (0, 1):
        try:
            print(f"copy {i}: shell {json.dumps(shell(i))} room {json.dumps(status(i)['room'])}")
        except lib.SfveError:
            print(f"copy {i} is not answering")
    if not a.keep:
        lib.kill_all()
    print("FAIL: " + failure if failure else "PASS")
    return 1 if failure else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
