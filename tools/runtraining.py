"""SFV Ember's Training through Ember's shell (EMBER_LIBRARY.md section 27).

  python tools/runtraining.py [--shots DIR] [--part solo|call|practice|control|all] [--keep]

Menus are driven by each copy's virtual pad 0 (-sfve_pads=01), the path a
physical pad takes; the pipe reads the status, presses the pad and, for the
Practice checks, reads and changes P2's vital (peek, perturb).

solo: one copy. Start opens Ember; Home's Training row leads to Ember's
  Training select (the game's main menu stays behind it); the Dummy row opens
  the dummy's own pick screen on its board (no Random card), where Select on
  a tile makes the dummy that fighter and opens its look page (with --shots,
  its costume and V-Skill cards too); the Stage row opens the stage cards,
  where Select stores the stage and closes them; Start Training loads SFV's
  own Training battle (TrainingBattleState) with the player's pick as P1 and
  the dummy as P2. Start there is the game's pause menu, and Ember's shell
  stays away.
call: two copies in a private room (runshell's steps). Copy 0 sits alone and
  waits in Training from the room's row. Copy 1 sits down opposite: copy 0's
  battle shows the call's banner and leaves for the main menu after the
  countdown, where the shell opens on the table with the seconds to ready;
  not readying gives the seat up. A second call is cut short with go now on
  the pad's View, a third with Enter in copy 0's window. Both then ready and
  play the room's game to its result.
practice: two copies in a Practice room. Copy 0's window in front, P1 walks
  up to P2; P2's vital is set to its last point (and its refill held off)
  on both copies at the same frame, and P1's hits take it to 0: no KO, and
  it fills again. Then both play seeded random input from frame 3000
  (-sfve_mash) for 90 s: no KO, no clock, every confirmed checkpoint agrees
  (as runroom.ps1 compares them). End practice over the fight (Start, A,
  Right, A) takes both back to the room with no result.
control: the same last point and hits in a normal room's match: P2 is
  knocked out (the result's finish is a KO's).

Prints PASS or FAIL; closes the copies it started unless --keep. Takes
D:\\sfv-re\\sfv-launch.lock while SFV runs (and waits for it while another
agent holds it and SFV is running).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import runshell as shell_steps  # noqa: E402
import sfvelib as lib  # noqa: E402

LAUNCH_LOCK = Path(r"D:\sfv-re\sfv-launch.lock")
LOCK_STALE_S = 45 * 60
TRAINING_SCENE = "0x2DA8B70"  # SfvOffsets: kTrainingBattleVtable
MAIN_MENU_SCENE = "0x2D98660"  # kMainMenuVtable

status = shell_steps.status
shell = shell_steps.shell
press = shell_steps.press
wait_for = shell_steps.wait_for
expect = shell_steps.expect
seek = shell_steps.seek
pick_place = shell_steps.pick_place
expect_pick = shell_steps.expect_pick
seek_pick = shell_steps.seek_pick


def take_launch_lock() -> None:
    while LAUNCH_LOCK.exists():
        age = time.time() - LAUNCH_LOCK.stat().st_mtime
        if age > LOCK_STALE_S or not lib.running_pids():
            break
        print(f"waiting for the launch lock: {LAUNCH_LOCK.read_text(errors='replace').strip()}")
        time.sleep(30)
    LAUNCH_LOCK.write_text(f"sfv-training agent (runtraining.py), {time.strftime('%Y-%m-%dT%H:%M:%S')}\n")


def release_launch_lock() -> None:
    try:
        if LAUNCH_LOCK.read_text(errors="replace").startswith("sfv-training agent"):
            LAUNCH_LOCK.unlink()
    except OSError:
        pass


def training(index: int) -> dict:
    return status(index)["training"]


def scene(index: int) -> str:
    return status(index)["scene"]


def shot(shots: Path | None, name: str, index: int) -> None:
    if shots:
        lib.screenshot(lib.status_pid(index), shots / f"{name}.png")


def launch(indexes: list[int], mash: bool = True, mash_from: int = 300, lag_ms: int = 0) -> None:
    """Copies with rooms on; with `mash`, each plays seeded random input from
    frame `mash_from` and its virtual pad 0's before (the pipe's presses),
    else only the pad's."""
    for i in indexes:
        seed = 101 * (i + 1)
        play = [f"-sfve_mash={seed}", f"-sfve_mash_from={mash_from}", "-sfve_mash_pad=1"] if mash else []
        lag = [f"-sfve_lag={lag_ms}"] if lag_ms else []
        lib.launch(i, "netplay", ["-sfve_pads=01", "-sfve_background=1", "-sfve_room=1"] + play + lag)
    wait_for("the runtime", 120, lambda: all(status(i).get("ember", {}).get("host") == "running" for i in indexes))


def solo(shots: Path | None) -> None:
    launch([0])
    shell_steps.open_home(0)
    seek(0, "training", ["DOWN"], 12)
    shot(shots, "solo-1-home", 0)
    press(0, "A")
    # The shell closes behind the request and opens again on Training select.
    wait_for("Training select", 20, lambda: shell(0)["shown"] and shell(0)["screen"] == "training")
    expect(0, "training", "training-fighter")
    shot(shots, "solo-2-training-select", 0)
    press(0, "DOWN")
    expect(0, "training", "training-dummy")
    press(0, "A")
    # The dummy's board: its tiles, no Random card.
    expect_pick(0, "home")
    if not pick_place(0)["dummy"]:
        raise lib.SfveError("Training select's Dummy row opened the player's pick screen")
    shot(shots, "solo-3-dummy-board", 0)
    # Two tiles along (and on past a fighter the player cannot play), Select
    # makes the dummy that fighter and opens its look page.
    press(0, "RIGHT")
    press(0, "RIGHT")
    for _ in range(6):
        press(0, "A")
        if pick_place(0)["page"] == "look":
            break
        press(0, "RIGHT")
    expect_pick(0, "look")
    shot(shots, "solo-3-dummy", 0)
    if shots:
        # The look page's Costume row opens the costume cards, its V-Skill
        # row the V-Skill and V-Trigger cards; Back returns to the look page.
        seek_pick(0, "costume", ["DOWN"], 12)
        press(0, "A")
        expect_pick(0, "costumes")
        time.sleep(1)
        shot(shots, "solo-3-dummy-costumes", 0)
        press(0, "B")
        expect_pick(0, "look")
        seek_pick(0, "vskill", ["DOWN"], 8)
        press(0, "A")
        expect_pick(0, "loadout")
        time.sleep(1)
        shot(shots, "solo-3-dummy-loadout", 0)
        press(0, "B")
        expect_pick(0, "look")
    press(0, "B")  # the board
    expect_pick(0, "home")
    press(0, "B")
    expect(0, "training")
    # The Stage row opens the stage cards; a card chosen by pad is stored and
    # the cards close.
    seek(0, "training-stage", ["DOWN"], 6)
    press(0, "A")
    expect_pick(0, "stage")
    time.sleep(1)
    shot(shots, "solo-3-stage", 0)
    before = pick_place(0)["stored"]
    press(0, "RIGHT")
    card = pick_place(0)["focus"]
    press(0, "A")
    expect(0, "training")
    after = pick_place(0)["stored"]
    print(f"solo: stage card {card}: {before} -> {after}")
    if card != "stage-255" and after == before:
        raise lib.SfveError(f"the stage card {card} did not change the stage ({after})")
    seek(0, "training-start", ["DOWN"], 6)
    shot(shots, "solo-4-start", 0)
    starts = lib.count_log(0, "training: battle")
    press(0, "A")
    wait_for("SFV's Training battle", 60,
             lambda: scene(0) == TRAINING_SCENE and training(0)["available"] and not shell(0)["shown"])
    if lib.count_log(0, "training: battle") <= starts:
        raise lib.SfveError("copy 0's log has no Training battle")
    # What the netplay mode made of the battle (it plays it unchanged).
    for line in lib.log_path(0).read_text(errors="replace").splitlines():
        if "adapter: bound" in line or "adapter: not bound yet" in line or "training: battle" in line:
            print("solo log:", line.split("] ", 1)[-1])
    time.sleep(4)
    shot(shots, "solo-5-training", 0)
    # The game's Start is its own Training menu; Ember's menu stays away.
    press(0, "START")
    time.sleep(1.5)
    if shell(0)["shown"]:
        raise lib.SfveError("Ember's shell opened over SFV's Training")
    if not training(0)["paused"]:
        raise lib.SfveError("the game's pause menu did not pause the Training battle")
    shot(shots, "solo-6-pause-menu", 0)
    press(0, "START")
    time.sleep(1)


def room_of_two(shots: Path | None) -> None:
    launch([0, 1])
    shell_steps.create_room(None)
    shell_steps.copy_invitation(None)
    shell_steps.join_room()


def wait_in_training(index: int) -> None:
    # The room's action bar (options, Training, the copy buttons, More...)
    # is walked with Left and Right (runshell's seek_bar).
    shell_steps.seek_bar(index, "room-training")
    press(index, "A")
    wait_for(f"copy {index} in Training", 60,
             lambda: scene(index) == TRAINING_SCENE and training(index)["available"])


def leave_seat(index: int) -> None:
    """The table page's Leave seat (Y on the table's card)."""
    seek(index, "table-0", ["UP"], 20)
    press(index, "Y")
    expect(index, "room-table")
    seek(index, "unqueue", ["DOWN"], 20)
    press(index, "A")
    wait_for(f"copy {index} out of its seat", 30, lambda: status(index)["room"]["slot"] == -1)
    press(index, "B")


def called_back(index: int, since: int, shots: Path | None, name: str | None) -> None:
    wait_for("the call", 30, lambda: lib.count_log(index, "training: leaving for the main menu in") > since)
    if name:
        time.sleep(0.5)
        shot(shots, name, index)


def call(shots: Path | None) -> None:
    room_of_two(shots)
    # Copy 0 sits alone and waits in Training; the room shows it there.
    shell_steps.take_seat(0, 0)
    wait_in_training(0)
    time.sleep(4)
    shot(shots, "call-1-waiting", 0)
    shot(shots, "call-1-room-of-copy1", 1)
    # Copy 1 sits down opposite: copy 0 is called back, its battle shows the
    # banner for the countdown and leaves for the main menu, where the shell
    # opens on its table with the seconds to ready.
    calls = lib.count_log(0, "training: leaving for the main menu in")
    shell_steps.take_seat(1, 1)
    called_back(0, calls, shots, "call-2-banner")
    shot(shots, "call-2-calling-on-copy1", 1)
    t0 = time.time()
    wait_for("copy 0 back at the main menu", 30, lambda: scene(0) == MAIN_MENU_SCENE)
    print(f"call: back at the main menu {time.time() - t0:.1f} s after the banner's shot")
    wait_for("the shell on copy 0's table", 20, lambda: shell(0)["shown"] and shell(0)["screen"] in ("room", "room-table"))
    time.sleep(1)
    shot(shots, "call-3-back-in-room", 0)
    press(0, "B")
    # Not readied in the window: the seat is given up.
    wait_for("the forfeit of copy 0's seat", 90, lambda: status(0)["room"]["slot"] == -1)
    if lib.count_log(0, "not readied in time after the call from Training; the seat is given up") == 0:
        raise lib.SfveError("copy 0's seat went, but not by the call's forfeit")
    shot(shots, "call-4-forfeit", 0)
    # Again, cut short with go now: copy 1 gets up, copy 0 sits and waits in
    # Training, copy 1 sits down again and copy 0 presses View.
    leave_seat(1)
    shell_steps.take_seat(0, 0)
    wait_in_training(0)
    time.sleep(3)
    calls = lib.count_log(0, "training: leaving for the main menu in")
    shell_steps.take_seat(1, 1)
    called_back(0, calls, shots, None)
    lib.request(0, "pad 0 BACK:100 -:200")
    t0 = time.time()
    wait_for("copy 0 back at the main menu after go now", 10, lambda: scene(0) == MAIN_MENU_SCENE)
    if lib.count_log(0, "shell: go now on the Training call") == 0:
        raise lib.SfveError("go now was not taken")
    print(f"go now: back at the main menu {time.time() - t0:.1f} s after View")
    wait_for("the shell on copy 0's table again", 20,
             lambda: shell(0)["shown"] and shell(0)["screen"] in ("room", "room-table"))
    press(0, "B")  # from the table page to the room's board, where Ready is
    # A third time, cut short with Enter in copy 0's window (in front).
    leave_seat(1)
    seek(0, "table-0", ["UP"], 20)
    wait_in_training(0)
    time.sleep(3)
    calls = lib.count_log(0, "training: leaving for the main menu in")
    shell_steps.take_seat(1, 1)
    lib.focus(lib.status_pid(0))
    called_back(0, calls, shots, None)
    VK_RETURN, KEYEVENTF_KEYUP = 0x0D, 0x0002
    lib._user32.keybd_event(VK_RETURN, 0, 0, 0)
    time.sleep(0.08)
    lib._user32.keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0)
    t0 = time.time()
    wait_for("copy 0 back at the main menu after Enter", 10, lambda: scene(0) == MAIN_MENU_SCENE)
    if lib.count_log(0, "shell: go now on the Training call (Enter)") == 0:
        raise lib.SfveError("go now's Enter was not taken")
    print(f"go now: back at the main menu {time.time() - t0:.1f} s after Enter")
    wait_for("the shell on copy 0's table a third time", 20,
             lambda: shell(0)["shown"] and shell(0)["screen"] in ("room", "room-table"))
    press(0, "B")
    # Both ready and play the room's game to its end, back in the room.
    results = [lib.count_log(i, "netplay: match result") for i in (0, 1)]
    closes = [lib.count_log(i, "battle closed at its match") for i in (0, 1)]
    shell_steps.ready_both(1)
    wait_for("the game's result on both copies", 300,
             lambda: all(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)))
    wait_for("both copies back in the room", 120,
             lambda: all(lib.count_log(i, "battle closed at its match") > closes[i] and
                         status(i)["room"]["room"] == "joined" for i in (0, 1)))


def checkpoints(index: int) -> dict[int, str]:
    """frame -> overall hash, as the copy logged them (SfvVerification), as
    runroom.ps1 reads them."""
    found: dict[int, str] = {}
    pattern = re.compile(r"checkpoint: frame (\d+) hash (\w+)")
    for line in lib.log_path(index).read_text(errors="replace").splitlines():
        m = pattern.search(line)
        if m:
            found[int(m.group(1))] = m.group(2)
    return found


def vitals(index: int) -> list[int] | None:
    """Both fighters' vital (CharaStatusPart's dword 48, the first word of its
    first snapshot run), from the bound battle's regions; None when not bound."""
    out = []
    for side in (0, 1):
        reply = lib.request(index, f"peek CharaUnit[{side}].CharaStatusPart+0xC0 0 8")
        if not reply.startswith("ok"):
            return None
        out.append(int(reply.split()[1], 16))
    return out


def create_practice_room() -> None:
    shell_steps.open_home(0)
    seek(0, "online", ["UP"])
    press(0, "A")
    expect(0, "online", "create")
    press(0, "A")
    expect(0, "create")
    # The rounds do not matter: a Practice match has no KO and no clock.
    seek(0, "practice-room", ["DOWN"], 20)
    press(0, "RIGHT")  # on
    seek(0, "host", ["DOWN"], 20)
    press(0, "A")
    wait_for("copy 0's Practice room", 60,
             lambda: status(0)["room"]["room"] == "joined" and shell(0)["screen"] == "room")


# P1's medium punch (SFV's default layout: Y), eight times.
HITS = " ".join(["Y:80 -:420"] * 8)
STATUS = "CharaUnit[1].CharaStatusPart+0xC0"  # P2's vital (+0), maximum (+4), recovery timer (+8)


def perturb_both(frame: int, offset: int, add: int, what: str) -> None:
    key = f"netplay: perturbed {STATUS}+0x{offset:X} "
    before = [lib.count_log(i, key) for i in (0, 1)]
    for i in (0, 1):
        reply = lib.request(i, f"perturb {frame} {STATUS} {offset} {add}")
        if not reply.startswith("ok"):
            raise lib.SfveError(f"copy {i}: {reply}")
    wait_for(what, 30, lambda: all(lib.count_log(i, key) > before[i] for i in (0, 1)))


def low_vital_and_hit() -> int:
    """P1 walks up to P2 (copy 0's window in front: the game reads its pad
    only then). With both players still, P2's recovery timer is held off and
    its vital goes to its last point, on both copies at the same frames,
    which no rollback crosses (no input is in flight); then P1 hits: the
    first hit takes P2 to 0. Returns P2's full vital."""
    # The intro and the round call take about 15 s from the session's start.
    wait_for("the fight", 60, lambda: vitals(0) is not None and status(0)["battle"]["mode"]["frame"] > 1000)
    lib.focus(lib.status_pid(0))
    time.sleep(0.5)
    lib.request(0, "pad 0 RIGHT:1400 -:100")
    time.sleep(3)
    full_vital = vitals(0)[1]
    # The timer counts up a frame at a time (1 << 16) to its cap (60 frames,
    # dword_143966360); refilling starts at 60% of it. Ten seconds below 0
    # holds the refill off until the hits, each of which sets it to 0 again.
    hold = -600 << 16
    timer = int(lib.request(0, f"peek {STATUS} 8 4").split()[1], 16)
    timer -= (1 << 32) if timer >= 1 << 31 else 0
    frame = status(0)["battle"]["mode"]["frame"] + 90
    perturb_both(frame, 8, hold - timer, "P2's recovery timer held off on both copies")
    frame = status(0)["battle"]["mode"]["frame"] + 90
    perturb_both(frame, 0, 1 - full_vital, "P2's vital at its last point on both copies")
    print(f"P2's recovery timer {timer} set to {hold} and its vital {full_vital} to 1 on both copies (frame {frame}); P1 hits")
    lib.request(0, f"pad 0 {HITS}")
    return full_vital


def result_frames() -> list[str]:
    found = [re.findall(r"netplay: match result (.*?) at frame (\d+)", lib.log_path(i).read_text(errors="replace"))
             for i in (0, 1)]
    return [f"{f[-1][1]} ({f[-1][0]})" if f else "?" for f in found]


def control(shots: Path | None) -> None:
    """The same last hit point and hits in a normal room's match (1 round of
    30 s): P2 is knocked out (the result's finish is a KO's, not the clock's)."""
    launch([0, 1], mash=False)
    shell_steps.create_room(None)
    shell_steps.copy_invitation(None)
    shell_steps.join_room()
    shell_steps.take_seat(0, 0)
    shell_steps.take_seat(1, 1)
    results = [lib.count_log(i, "netplay: match result") for i in (0, 1)]
    shell_steps.ready_both(1)
    low_vital_and_hit()
    t0 = time.time()
    wait_for("the match's result", 150, lambda: all(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)))
    frames = result_frames()
    print(f"control: the match ended {time.time() - t0:.0f} s after the hits began, at frame {frames[0]} / {frames[1]}")
    # MatchResult's finish: a KO's kinds (ko, perfect, ...) need the judge's
    # KO cause; a clock that ran out is "time over".
    if frames[0] == "?" or '"finish":"time over"' in frames[0] or '"finish":"other"' in frames[0]:
        raise lib.SfveError("the match was not ended by a KO")


def practice(shots: Path | None, seconds: int = 90) -> None:
    # 30 ms each way, as runroom.ps1 -lag 30: the input delay no longer
    # hides the link, so the random play rolls back.
    launch([0, 1], mash_from=3000, lag_ms=30)
    create_practice_room()
    shell_steps.copy_invitation(None)
    shell_steps.join_room()
    shell_steps.take_seat(0, 0)
    shell_steps.take_seat(1, 1)
    shot(shots, "practice-1-room", 1)
    results = [lib.count_log(i, "netplay: match result") for i in (0, 1)]
    shell_steps.ready_both(1)
    wait_for("the Practice gauges on both copies", 60,
             lambda: all(lib.count_log(i, "adapter: Practice gauges set for both players") > 0 for i in (0, 1)))
    full = low_vital_and_hit()
    # P2 is hit at 1 point of vital: it stays at 0 rather than going down,
    # then fills again. Sampled twice a second on copy 0.
    lows: list[int] = []
    t0 = time.time()
    while time.time() - t0 < 15:
        v = vitals(0)
        if v:
            lows.append(v[1])
        time.sleep(0.5)
    if any(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)):
        raise lib.SfveError(f"P2 was knocked out (result at frame {result_frames()})")
    print(f"practice: P2's vital after the hits: lowest {min(lows)}, last {lows[-1]} of {full} ({len(lows)} samples)")
    if min(lows) != 0 or lows[-1] != full:
        raise lib.SfveError("P2's vital did not stop at 0 and fill again")
    # Then both copies play seeded random input from frame 3000 on (-sfve_mash,
    # as runroom.ps1's games: rollbacks on both sides): nobody is knocked
    # out, no clock ends it, every checkpoint agrees.
    wait_for("the random play", 60, lambda: status(0)["battle"]["mode"]["frame"] > 3000)
    samples: list[list[int]] = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        if any(lib.count_log(i, "desync:") for i in (0, 1)):
            raise lib.SfveError("the Practice match desynced (see the copies' logs)")
        if any(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)):
            raise lib.SfveError("the Practice match ended by itself (a KO or the clock)")
        v = vitals(0)
        if v:
            samples.append(v)
        if len(samples) == 15:
            shot(shots, "practice-2-fight", 0)
        time.sleep(2)
    lowest = [min(x[i] for x in samples) for i in (0, 1)]
    rollbacks = [status(i)["battle"]["mode"]["rollbacks"] for i in (0, 1)]
    print(f"practice: under random play, lowest vital P1 {lowest[0]} P2 {lowest[1]} of {full} ({len(samples)} samples); "
          f"rollbacks P1 {rollbacks[0]} P2 {rollbacks[1]}")
    # End practice: Start opens Ember over the fight on the room's End practice.
    closes = [lib.count_log(i, "battle closed") for i in (0, 1)]
    time.sleep(2)
    lib.request(0, "pad 0 START:120 -:250")
    wait_for("Ember over the Practice match", 15, lambda: shell(0)["shown"] and shell(0)["focus"] == "end-practice")
    shot(shots, "practice-3-end-practice", 0)
    press(0, "A")  # End practice asks to confirm: Right and A confirm
    shot(shots, "practice-3-confirm", 0)
    press(0, "RIGHT")
    press(0, "A")
    wait_for("both copies back in the room", 90,
             lambda: all(lib.count_log(i, "battle closed") > closes[i] and status(i)["room"]["room"] == "joined"
                         for i in (0, 1)))
    if any(lib.count_log(i, "netplay: match result") > results[i] for i in (0, 1)):
        raise lib.SfveError("End practice reported a result")
    time.sleep(2)
    shot(shots, "practice-4-ended", 1)
    h0, h1 = checkpoints(0), checkpoints(1)
    common = sorted(set(h0) & set(h1))
    differing = [f for f in common if h0[f] != h1[f]]
    print(f"practice checkpoints: {len(common)} compared, {len(differing)} differing")
    if len(common) < seconds // 2 or differing:
        raise lib.SfveError(f"checkpoints: {len(common)} compared, {len(differing)} differing")


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--shots", type=Path)
    p.add_argument("--part", default="all", choices=["solo", "call", "practice", "control", "all"])
    p.add_argument("--keep", action="store_true")
    a = p.parse_args(argv)
    lib.begin_run()
    if a.shots:
        a.shots.mkdir(parents=True, exist_ok=True)
    take_launch_lock()
    failure = ""
    try:
        # Stopped before anything is installed. Launcher mode (SFVE_VIA=launcher) runs the
        # package folder's mod and writes nothing to the game folder.
        lib.kill_all()
        if lib.VIA != "launcher":
            lib.deploy()
        for part, run in (("solo", solo), ("call", call), ("practice", practice), ("control", control)):
            if a.part not in (part, "all"):
                continue
            lib.kill_all()
            print(f"--- {part}")
            run(a.shots)
            print(f"{part}: ok")
    except lib.SfveError as e:
        failure = str(e)
    finally:
        for i in (0, 1):
            try:
                print(f"copy {i}: shell {json.dumps(shell(i))} training {json.dumps(training(i))} "
                      f"room {json.dumps(status(i)['room'])}")
            except (lib.SfveError, KeyError):
                pass
        if not a.keep:
            lib.kill_all()
            release_launch_lock()
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
