"""Ember's Training lab over SFV's Training battle (docs/design/beta10/training.md):
the steps of runtraining.py's "lab" part, and the check that the lab stays out
of every other battle.

lab: one copy, its window in front (the game reads its pad only then). For
  each dummy in DUMMIES, SFV's Training battle is launched through the pipe
  ("launch training"), P1 RYU. On the first:
  - every page of the lab's controls opens (traininglab controls/page), and
    each meter option changes what the HUD draws (shots);
  - slot 1 is recorded from standby with a special move and a throw played
    on P1's pad, which plays the dummy (its control in the match data goes
    to its own slot); played back with P2 on the right, then again after P1
    has jumped over it (P2 on the left: the slot turned for its facing; the
    meter's log names P2's moves both times); looped (the cursor goes round);
  - saved as a recording file, cleared, loaded again.
  On every dummy: a one-button reply recorded in slot 2 replies after block
  (Training's guard setting), after hit and on wake-up (P1's sweep), and the
  lab's reply timing says where its button met the dummy's free frame.
others: the lab's status is not available and the log has no lab lines in an
  offline Versus battle (a battle the pads play) after Training.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

import runshell as shell_steps
import sfvelib as lib

status = shell_steps.status
wait_for = shell_steps.wait_for
TRAINING_SCENE = "0x2DA8B70"  # SfvOffsets: kTrainingBattleVtable

# RYU, a charge fighter (Guile), a grappler, one with a stance (Zeku's two
# forms) and Chun-Li.
DUMMIES = ["RYU", "GUL", "ZGF", "Z25", "CNL"]
# SFV's own Training setting the guard is (sfv::dummy: entry 1, five
# choices; 0 none). Unconfirmed in game: the run logs what the dummy does.
GUARD_SETTING = 1
GUARD_ALL = 1
# A pad step a frame and a little over (the pipe's steps are in ms).
FRAME_MS = 17
RECORDINGS = lib.LOG_DIR.parent / "recordings"


def lab(index: int = 0) -> dict:
    return status(index)["traininglab"]


def host(index: int = 0) -> dict:
    return status(index)["traininghost"]


def ask(line: str, index: int = 0) -> None:
    lib.request_ok(index, f"traininglab {line}")
    time.sleep(0.4)


def pad(steps: str, index: int = 0) -> None:
    lib.request_ok(index, f"pad 0 {steps}")


def frames(n: int) -> int:
    return n * FRAME_MS


def shot(shots: Path | None, name: str, index: int = 0) -> None:
    if shots:
        time.sleep(0.5)
        lib.screenshot(lib.status_pid(index), shots / f"lab-{name}.png")


def log_since(mark: int, index: int = 0) -> list[str]:
    lines = lib.log_path(index).read_text(errors="replace").splitlines()
    return [line.split("] ", 1)[-1] for line in lines[mark:]]


def log_mark(index: int = 0) -> int:
    return len(lib.log_path(index).read_text(errors="replace").splitlines())


def launch_training(dummy: str, index: int = 0) -> None:
    lib.request_ok(index, f"launch training RYU {dummy} -")
    wait_for(f"Training against {dummy}", 90, lambda: status(index)["scene"] == TRAINING_SCENE and lab(index)["available"])
    wait_for("the fight", 30, lambda: lab(index)["ready"])
    time.sleep(1)


def leave_training(index: int = 0) -> None:
    """Back to the main menu as the pause menu's Go to Main Menu leaves."""
    ask("leave", index)
    wait_for("the main menu", 60, lambda: not status(index)["training"]["available"] and status(index)["scene"] != TRAINING_SCENE)
    time.sleep(3)


def pages(shots: Path | None) -> None:
    ask("controls open")
    if not host()["controls"]:
        raise lib.SfveError("the lab's controls did not open")
    for page in ("home", "recording", "history", "frame-data", "hud", "hitboxes", "meter-settings", "reply"):
        ask(f"page {page}")
        if host()["screen"] != page:
            raise lib.SfveError(f"the lab's controls did not open their {page} page ({host()['screen']})")
        shot(shots, f"page-{page}")
    ask("meter style \"classic\"")
    ask("page meter-classic")
    shot(shots, "page-meter-classic")
    ask("controls close")
    # Each option changes what is drawn: one shot each, the default first.
    shot(shots, "meter-angled")
    for key, value, name in (("style", '"flat"', "flat"), ("style", '"classic"', "classic"), ("frames_shown", "120", "120"),
                             ("hud_size", "2", "large"), ("position", "60", "position-60"), ("history_frames", "600", "history"),
                             ("after_pause", '"continue"', "after-pause"), ("scroll", '"smooth"', "smooth"),
                             ("bar_height", "16", "bar-height"), ("classic_colors", "false", "classic-colours-off"),
                             ("hud_numbers", "false", "numbers-off"), ("hud_state", "false", "state-off"),
                             ("hud_shortcuts", "false", "shortcuts-off"), ("hud_inputs", '"both"', "inputs-both"),
                             ("hitboxes", "true", "hitboxes")):
        ask(f"meter {key} {value}")
        shot(shots, f"meter-{name}")
    ask("hud off")
    shot(shots, "hud-hidden")
    ask("hud on")
    for key, value in (("style", '"angled"'), ("frames_shown", "60"), ("hud_size", "1"), ("position", "82"),
                       ("history_frames", "1200"), ("after_pause", '"start_over"'), ("scroll", '"pages"'),
                       ("classic_colors", "true"), ("hud_numbers", "true"), ("hud_state", "true"), ("hud_shortcuts", "true"),
                       ("hud_inputs", '"off"'), ("hitboxes", "false")):
        ask(f"meter {key} {value}")


def p2_moves(lines: list[str]) -> list[str]:
    return [line for line in lines if line.startswith("frame meter: P2's script")]


def record_slot(slot: int, steps: str, settle_s: float) -> int:
    ask(f"select {slot}")
    ask("record")
    if lab()["mode"] != "standby":
        raise lib.SfveError(f"Record did not put the dummy on standby ({lab()['mode']})")
    dummy = lab()["dummy"]
    if dummy["kind"] != 0 or dummy["pad"] != 1:
        raise lib.SfveError(f"on standby the dummy is not played from its slot: {dummy}")
    ask("record")
    if lab()["mode"] != "recording":
        raise lib.SfveError(f"the second Record did not record ({lab()['mode']})")
    pad(steps)
    time.sleep(settle_s)
    ask("record")
    length = lab()["lengths"][slot - 1]
    if lab()["mode"] != "idle" or length <= 0:
        raise lib.SfveError(f"the recording did not stop into slot {slot}: {lab()}")
    return length


def play_once(mark_label: str, length: int) -> list[str]:
    mark = log_mark()
    ask("play")
    if lab()["mode"] != "playback":
        raise lib.SfveError(f"Play did not play ({lab()['mode']})")
    time.sleep(length / 60 + 1)
    moves = p2_moves(log_since(mark))
    print(f"lab: {mark_label}: P2 played {moves}")
    return moves


def recording(shots: Path | None) -> None:
    # A Hadoken for the dummy facing left (down, down-back to the screen,
    # back... its forward is the screen's Left), then a throw (LP+LK).
    special = f"DOWN:{frames(4)} DOWN+LEFT:{frames(4)} LEFT+X:{frames(4)} -:{frames(60)}"
    throw = f"LEFT+X+A:{frames(4)} -:{frames(80)}"
    mark = log_mark()
    length = record_slot(1, f"-:{frames(20)} {special} {throw}", 4)
    recorded = p2_moves(log_since(mark))
    print(f"lab: slot 1 recorded, {length} frames; P2 played {recorded} on P1's pad")
    if not recorded:
        raise lib.SfveError("P1's pad on standby did not play the dummy (no P2 move in the meter's log)")
    shot(shots, "recorded")
    right = play_once("P2 on the right", length)
    if not right:
        raise lib.SfveError("the playback played no P2 move")
    shot(shots, "played-right")
    # P1 walks up to the dummy and jumps over it: P2 is then on the left.
    pad(f"RIGHT:1800 UP+RIGHT:{frames(4)} -:1500")
    time.sleep(4)
    x = lab()["x"]
    print(f"lab: after the jump P1 at {x[0]}, P2 at {x[1]}")
    if x[0] <= x[1]:
        raise lib.SfveError(f"P1 did not get past the dummy ({x})")
    left = play_once("P2 on the left", length)
    if [line.split(":")[1] for line in left] != [line.split(":")[1] for line in right]:
        raise lib.SfveError(f"the slot played other moves on the left: {left} against {right}")
    shot(shots, "played-left")
    # Loop: the cursor goes round.
    ask("loop on")
    ask("play")
    cursors = []
    for _ in range(int(length / 30) * 3 + 6):
        cursors.append(lab()["cursor"])
        time.sleep(0.5)
    ask("stop")
    if lab()["mode"] != "idle" or not any(b < a for a, b in zip(cursors, cursors[1:])):
        raise lib.SfveError(f"the loop did not go round: cursors {cursors}")
    shot(shots, "looped")
    ask("loop off")
    # A file: saved, the slot cleared, loaded again.
    name = f"labtest-{int(time.time())}"
    ask(f"save {name}")
    time.sleep(1)
    path = RECORDINGS / f"{name}.json"
    if not path.exists():
        raise lib.SfveError(f"Save did not write {path}")
    ask("clear")
    if lab()["lengths"][0] != 0:
        raise lib.SfveError("Clear left slot 1 recorded")
    ask(f"load {name}")
    if lab()["lengths"][0] != length:
        raise lib.SfveError(f"Load gave slot 1 {lab()['lengths'][0]} frames, not {length}")
    print(f"lab: {path} saved and loaded back ({length} frames)")
    path.unlink()


def reply_case(dummy: str, when: str, attack: str) -> dict:
    """P1's attack, then the reply's timing as the lab logs it."""
    before = lab()["reply"]["timed"]
    mark = log_mark()
    ask(f"reply {when} timing 0 chance 100")
    pad(f"RIGHT:1500 -:{frames(10)} {attack} -:{frames(150)}")
    wait_for(f"{dummy}'s reply {when}", 12, lambda: lab()["reply"]["timed"] > before)
    reply = lab()["reply"]
    lines = [line for line in log_since(mark) if line.startswith("training lab: reply")]
    print(f"lab: {dummy} {when}: {reply}; {lines[-1] if lines else 'no reply line'}")
    if reply["freed"] is None or reply["move"] is None or reply["move"] != reply["freed"]:
        raise lib.SfveError(f"{dummy}'s reply {when} did not start on its first free frame: {reply}")
    ask("reply off")
    return reply


def replies(dummy: str) -> None:
    # The reply: a light punch, recorded on P1's pad from standby.
    record_slot(2, f"-:{frames(10)} X:{frames(3)} -:{frames(30)}", 1.5)
    ask("slot 1 off")
    ask(f"setting {GUARD_SETTING} {GUARD_ALL}")
    reply_case(dummy, "block", f"Y:{frames(4)}")
    ask(f"setting {GUARD_SETTING} 0")
    reply_case(dummy, "hit", f"Y:{frames(4)}")
    reply_case(dummy, "rise", f"DOWN+RT:{frames(4)}")
    ask("slot 1 on")


def lab_part(shots: Path | None) -> None:
    for number, dummy in enumerate(DUMMIES):
        launch_training(dummy)
        lib.focus(lib.status_pid(0))
        if number == 0:
            pages(shots)
            recording(shots)
        replies(dummy)
        shot(shots, f"replies-{dummy}")
        leave_training()
    lib.request_ok(0, "launch versus RYU KEN -")
    others()


def others(index: int = 0) -> None:
    """In a battle that is not Training the lab is off and writes nothing."""
    mark = log_mark(index)
    wait_for("the versus battle", 90, lambda: status(index)["scene"] != TRAINING_SCENE and not lab(index)["available"])
    time.sleep(5)
    if lab(index)["available"]:
        raise lib.SfveError("the lab is on in a battle that is not Training")
    touched = [line for line in log_since(mark, index) if line.startswith("training lab: the dummy plays")]
    if touched:
        raise lib.SfveError(f"the lab drove a dummy outside Training: {touched}")
    print("lab: off in the versus battle")


def stays_off_in_room(index: int) -> None:
    """For runtraining's room parts: no lab in a room's battle."""
    if lab(index)["available"]:
        raise lib.SfveError(f"copy {index}: the Training lab is on in a room's battle")
    print(f"copy {index}: lab {json.dumps(lab(index))}")
