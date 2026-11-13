"""Two copies play a private room through Ember's shell, driven by their pads.

  python tools/runshell.py [--shots DIR] [--games N] [--keep]

Each copy's virtual pad 0 (-sfve_pads=01) presses the shell's buttons, the
same path a physical pad takes (EMBER_LIBRARY.md section 13): copy 0 opens
Ember with Start, creates a private room of 1 round of 30 s and copies its
invitation; copy 1 joins by pasting it from the clipboard; both take a seat,
pick on the pick screen's board (X opens it on the fighters' tiles; Select on
one keeps it and closes the screen) and Ready on their own seat. The match
plays with seeded random input, and both copies must be back in the room
after it. With --shots, copy 0 also walks the pick screen's pages (the
board and its art pane, the costume and colour cards, the V-Skill and
V-Trigger cards, the stage cards, where Select must store the stage) and
copy 1 its table page's stage row as P2 (docs/design/beta8/select.md).
Nothing goes through the room test commands: the pipe only reads the status
and presses the pad. With --step-backdrop, each shot first fades the menu's
artwork backdrop to its next picture (the pipe's `backdrop step`,
docs/design/beta8/backdrop.md), so the shots show the pages on several
pictures. Prints PASS or FAIL; closes the copies unless --keep.

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
STEP_BACKDROP = False  # --step-backdrop: each shot on the backdrop's next picture


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


def pick_place(index: int) -> dict:
    """The pick screen's own page and focus (the shell status' "pick"), and
    the stored pick ("stored", PickText) and whether it is Random."""
    return shell(index)["pick"]


def expect_pick(index: int, page: str, focus: str | None = None) -> None:
    s = shell(index)
    p = s["pick"]
    if s["screen"] != "selection" or p["page"] != page or (focus is not None and p["focus"] != focus):
        raise lib.SfveError(f"copy {index}: expected the pick screen at {page}/{focus}, "
                            f"shell is at {s['screen']} and the pick screen at {p['page']}/{p['focus']}")


def seek_pick(index: int, focus: str | tuple[str, ...], moves: list[str], limit: int = 12) -> None:
    """seek, on the pick screen's own focus."""
    wanted = (focus,) if isinstance(focus, str) else focus
    for step in range(limit):
        if pick_place(index)["focus"] in wanted:
            return
        press(index, moves[step % len(moves)])
    if pick_place(index)["focus"] not in wanted:
        raise lib.SfveError(f"copy {index}: could not reach {focus} on the pick screen (at {pick_place(index)['focus']})")


# Runs of presses that walk the whole board from any tile: along the row
# both ways, then the same on the other row.
BOARD_RUNS = (["LEFT"] * 13, ["RIGHT"] * 26, ["DOWN"] + ["LEFT"] * 13, ["RIGHT"] * 26, ["UP"] + ["LEFT"] * 13)


def seek_board(index: int, what: str, found) -> None:
    """Walks the board (BOARD_RUNS) until `found(pick status)` holds."""
    for run in BOARD_RUNS:
        for move in run:
            if found(pick_place(index)):
                return
            press(index, move)
    if not found(pick_place(index)):
        raise lib.SfveError(f"copy {index}: could not reach {what} on the board (at {pick_place(index)['focus']})")


def seek_cards(index: int, card: str) -> None:
    """A card of a gallery page (costumes, colours, stages) wherever the grid
    put it: up and left to the first card, then line by line, right along
    one and left along the next. A run along a line ends where a press no
    longer moves the focus. A dimmed card (not this player's) is reached as
    any other: it is shown, only not chosen."""
    def at() -> str:
        return pick_place(index)["focus"]

    def run(move: str, limit: int) -> bool:
        for _ in range(limit):
            if at() == card:
                return True
            before = at()
            press(index, move)
            if at() == before:
                break
        return at() == card

    if run("UP", 12) or run("LEFT", 40):
        return
    for line in range(16):
        if run("RIGHT" if line % 2 == 0 else "LEFT", 40):
            return
        before = at()
        press(index, "DOWN")
        if at() == before:
            break
    if at() != card:
        raise lib.SfveError(f"copy {index}: could not reach {card} on the pick screen (at {at()})")


def seek_runs(index: int, focus: str, runs: list[tuple[str, int]]) -> None:
    """Presses each run's direction up to its count, a run ending early where a
    press no longer moves the focus, until the focus is `focus`."""
    for move, limit in runs:
        for _ in range(limit):
            if shell(index)["focus"] == focus:
                return
            before = shell(index)["focus"]
            press(index, move)
            if shell(index)["focus"] == before:
                break
    if shell(index)["focus"] != focus:
        raise lib.SfveError(f"copy {index}: could not reach {focus} (at {shell(index)['focus']})")


# The room board's first table from anywhere on it: up from the bar and the
# table's rows, left from the members and chat panels beside the tables, and
# up again from where those lead.
TABLE_RUNS = [("UP", 20), ("LEFT", 8), ("UP", 20), ("DOWN", 3), ("LEFT", 8), ("UP", 20)]


def seek_table(index: int) -> None:
    seek_runs(index, "table-0", TABLE_RUNS)


def seek(index: int, focus: str | tuple[str, ...], moves: list[str], limit: int = 12) -> None:
    """Presses `moves` in turn, round and round, until the focus is `focus`
    (or any of them)."""
    wanted = (focus,) if isinstance(focus, str) else focus
    for step in range(limit):
        if shell(index)["focus"] in wanted:
            return
        press(index, moves[step % len(moves)])
    if shell(index)["focus"] not in wanted:
        raise lib.SfveError(f"copy {index}: could not reach {focus} (at {shell(index)['focus']})")


def open_home(index: int) -> None:
    """Ember opens on entering the main menu; Start opens it if it was closed.
    The first-run welcome is skipped."""
    if not shell(index)["shown"]:
        press(index, "START")
    if shell(index)["screen"] == "welcome":
        seek(index, "welcome-skip", ["DOWN", "DOWN", "RIGHT"])
        press(index, "A")
    expect(index, "home")


def backdrop(index: int) -> dict:
    return status(index)["backdrop"]


def step_backdrop(index: int, timeout_s: float = 30) -> str:
    """Fades the menu's backdrop to its next picture (`backdrop step`) and
    waits until the fade is over; the picture now shown. A backdrop that is
    not drawn, or has no next picture, is left as it is."""
    before = backdrop(index)
    if not before["drawn"]:
        return before["current"]
    lib.request(index, "backdrop step")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        now = backdrop(index)
        if now["current"] != before["current"] and not now["fading"]:
            time.sleep(0.3)
            return now["current"]
        time.sleep(0.25)
    print(f"copy {index}: the backdrop did not change from {before['current']} (next {backdrop(index)['next']})")
    return backdrop(index)["current"]


def shot(shots: Path | None, name: str, index: int) -> None:
    if shots:
        if STEP_BACKDROP:
            print(f"copy {index}: {name} on {step_backdrop(index)}")
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


# The room's action bar under its tables, in its order: Table options and
# Training (both while the room is open), the two invitation copies, More...
# and Leave room. One line, or two where a language's labels are longer
# (Russian); which of them is under a table depends on the window's width.
ROOM_BAR = ("options", "room-training", "copy", "copy-short", "room-more", "leave")


def seek_bar(index: int, target: str) -> None:
    """Down from the tables onto the room's action bar, then along it (and
    onto its other line, if it has two) to `target`."""
    seek(index, ROOM_BAR, ["DOWN"], 30)
    for moves in (["RIGHT"] * 6, ["LEFT"] * 6, ["DOWN"] + ["RIGHT"] * 6, ["LEFT"] * 6,
                  ["UP"] + ["RIGHT"] * 6, ["LEFT"] * 6):
        for move in moves:
            if shell(index)["focus"] == target:
                return
            press(index, move)
        if shell(index)["focus"] == target:
            return
    raise lib.SfveError(f"copy {index}: could not reach {target} on the room's bar (at {shell(index)['focus']})")


def copy_invitation(shots: Path | None) -> None:
    seek_bar(0, "copy")
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
    seek_table(index)
    press(index, "A")
    press(index, "A")
    wait_for(f"copy {index} seated", 60, lambda: status(index)["room"]["slot"] == seat)


def pick(index: int, fighter_steps: int, shots: Path | None, name: str | None) -> None:
    """X opens the pick screen on its board, the stored fighter's tile
    focused; Right `fighter_steps` tiles along (and on past a fighter the
    player cannot play) and Select keeps that fighter in their remembered
    look and closes the screen (opened for the room's Fighter)."""
    press(index, "X")
    expect_pick(index, "home")
    before = pick_place(index)["stored"]
    # Right along the row, across the Random tile (Random is not this pick).
    for _ in range(fighter_steps):
        press(index, "RIGHT")
        if pick_place(index)["focus"] == "random-fighter":
            press(index, "RIGHT")
    if not pick_place(index)["focus"].startswith("fighter-"):
        raise lib.SfveError(f"copy {index}: the board's focus is {pick_place(index)['focus']}, not a fighter")
    if name:
        shot(shots, name, index)
    # A fighter its player cannot play keeps its tile but is not chosen:
    # the next one along is.
    for _ in range(6):
        press(index, "A")
        if shell(index)["screen"] != "selection":
            break
        press(index, "RIGHT")
        if pick_place(index)["focus"] == "random-fighter":
            press(index, "RIGHT")
    expect(index, "room")
    print(f"copy {index}: pick {before} -> {pick_place(index)['stored']}")


def table_row(index: int, row: str) -> None:
    """The table page (Y on the table's card) with `row` focused."""
    seek_table(index)
    press(index, "Y")
    expect(index, "room-table")
    seek(index, row, ["DOWN"], 20)


def not_owned_shots(shots: Path | None) -> None:
    """Copy 0, after its pick, with --unowned CNL,KEN:1,KEN:0:1 (the pick is
    KEN's costume 0 in colour 03, the fitting having skipped CNL and colour
    02): CHUN-LI's tile beside KEN's on the board, dimmed in its place, the
    art pane saying NOT OWNED's words; then the costume cards (the table
    page's Appearance) on KEN's costume 1 and the colour cards on his colour
    02, dimmed, the pane saying why."""
    if not shots:
        return
    press(0, "X")
    expect_pick(0, "home")
    seek_board(0, "CNL's tile", lambda p: p.get("code") == "CNL")
    time.sleep(0.5)
    shot(shots, "4b-not-owned-fighter", 0)
    press(0, "B")  # Back from the board: the room
    expect(0, "room")
    table_row(0, "appearance")
    press(0, "A")
    expect_pick(0, "costumes")
    seek_cards(0, "costume-1")
    time.sleep(0.5)
    shot(shots, "4b-not-owned-costume", 0)
    seek_cards(0, "costume-0")
    press(0, "A")  # costume 0's colours
    expect_pick(0, "colors")
    seek_cards(0, "color-1")
    time.sleep(0.5)
    shot(shots, "4b-not-owned-color", 0)
    press(0, "B")  # the costumes
    press(0, "B")  # opened on them: back to the table page
    expect(0, "room-table")
    press(0, "B")
    expect(0, "room")


def select_shots(shots: Path | None) -> None:
    """Copy 0 (P1) walks the pick screen's pages and takes each one's shot;
    a stage card chosen by pad stores the stage and closes the screen.
    Copy 1 (P2) shows its table page's stage row, which only P1 can open."""
    if not shots:
        return
    # The board: the tiles in the game's own cells, the art pane on the right.
    press(0, "X")
    expect_pick(0, "home")
    time.sleep(1)
    shot(shots, "4c-board", 0)
    seek_board(0, "the Random tile", lambda p: p["focus"] == "random-fighter")
    time.sleep(0.5)
    shot(shots, "4c-board-random", 0)
    press(0, "B")
    expect(0, "room")
    # The costume cards, then a costume's colour cards (the table page's Appearance).
    table_row(0, "appearance")
    press(0, "A")
    expect_pick(0, "costumes")
    time.sleep(1)
    shot(shots, "4d-costumes", 0)
    press(0, "A")
    expect_pick(0, "colors")
    time.sleep(1)
    shot(shots, "4e-colours", 0)
    press(0, "B")
    press(0, "B")
    expect(0, "room-table")
    # The V-Skill and V-Trigger cards (the table page's V-Skill and V-Trigger row).
    seek(0, "ultra", ["DOWN"], 30)
    press(0, "A")
    expect_pick(0, "loadout")
    time.sleep(1)
    shot(shots, "4f-loadout", 0)
    seek_pick(0, ("vtrigger-0", "vtrigger-1"), ["DOWN"], 3)
    time.sleep(0.5)
    shot(shots, "4f-loadout-vtrigger", 0)
    press(0, "B")
    expect(0, "room-table")
    # The stage cards (the table page's Stage): Right and Select store the
    # next stage and close the screen.
    seek(0, "stage", ["DOWN"], 30)
    press(0, "A")
    expect_pick(0, "stage")
    time.sleep(1)
    shot(shots, "4h-stage", 0)
    before = pick_place(0)["stored"]
    press(0, "RIGHT")
    focus = pick_place(0)["focus"]
    press(0, "A")
    expect(0, "room-table")
    after = pick_place(0)["stored"]
    print(f"copy 0: stage card {focus}: {before} -> {after}")
    if focus != "stage-255" and after == before:
        raise lib.SfveError(f"copy 0: the stage card {focus} did not change the stage ({after})")
    press(0, "B")
    expect(0, "room")
    # P2's stage row: shown, not opened; its words say only P1 chooses.
    table_row(1, "stage")
    time.sleep(0.5)
    shot(shots, "4i-stage-p2", 1)
    press(1, "A")
    expect(1, "room-table", "stage")
    press(1, "B")
    expect(1, "room")


def room_shots(shots: Path | None) -> None:
    """Copy 1, a guest seated as P2 with a pick, before Ready: its seat focused
    (the room, its members, the hint bar and what Ready will say), then its
    table page (Y: the table's rules as a guest reads them)."""
    if not shots:
        return
    seek_table(1)
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
                seek_table(i)
                press(i, "A")
        return all(lib.count_log(i, "netplay: room match as P") > starts[i] for i in (0, 1))
    wait_for(f"game {game}'s sessions", 180, started)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--shots", type=Path)
    p.add_argument("--games", type=int, default=1)
    p.add_argument("--keep", action="store_true")
    p.add_argument("--step-backdrop", action="store_true", help="each shot on the menu backdrop's next picture")
    p.add_argument("--unowned", help="copy 0's -sfve_unowned (CODE[:costume[:colour]],...): content its player "
                   "may not pick, to show the pick screen's NOT OWNED")
    a = p.parse_args(argv)
    global STEP_BACKDROP
    STEP_BACKDROP = a.step_backdrop
    lib.begin_run()
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
                 lambda: all(status(i).get("ember", {}).get("host") == "running" for i in (0, 1)))
        create_room(a.shots)
        copy_invitation(a.shots)
        join_room()
        take_seat(0, 0)
        take_seat(1, 1)
        pick(0, 2, a.shots, "4-pick")
        pick(1, 3, None, None)
        if a.unowned:
            not_owned_shots(a.shots)
        select_shots(a.shots)
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
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
