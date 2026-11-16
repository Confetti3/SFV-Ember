"""Each fighter keeps its own look, and the player's pick is kept between runs (docs/design/beta10/picks.md).

  python tools/runpicks.py --out DIR [--keep-settings]

One copy (development build), its virtual pad 0 driving Ember's shell as a physical pad does. Training select's
Fighter row opens the player's own pick screen on the board, focused on the stored fighter's tile (as every Training
row but Stage does: SelectionStart::Board); Select on that tile opens the fighter's look page (rows Fighter, Costume,
Colour, V-Skill, V-Trigger and Stage; Back is the board). The pipe only reads the status and presses the pad. When a
step fails, the pick screen's page and focus after each press so far is printed (the trail), so an extra or missing
press shows.

1. Fighter A (the first fighter along with two V-Skills, two V-Triggers and two costumes of two colours) is on the look
   page, which Select on its tile of the board opens (the board is what Training select's Fighter row opens). The Costume, Colour, V-Skill and V-Trigger rows are each stepped with Right: the stored
   pick must change in each.
2. The Fighter row stepped to B: B must be in its own default look (V-Skill and V-Trigger as a fighter never chosen has
   them, a look that is not A's changed one). Stepped back to A: A's look is back, whole.
3. Random on (the board's Random tile, chosen, then stored: it opens the rolled fighter's page): stepping the page's
   Fighter row turns Random off.
4. A is chosen again from the board: its look is still the one set in 1.
5. The settings file holds `overlay.sfvPicks` (the fighter A and A's look) 1.5 s after the last change. The copy is closed
   as its window's close button closes it. Started again: the stored pick is A and its look, before any menu is opened; on
   its look page the same; stepped to B, B's default look, as before; stepped back, A's.

The test copy writes %APPDATA%\\SFVEmber\\settings.json (instance 0 does): the file is copied to DIR first and put
back at the end unless --keep-settings. Screenshots of the look page at each step go to DIR. Prints PASS or FAIL;
copies it started are ended at the end.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import runshell as sh  # noqa: E402
import runtraining as tr  # noqa: E402
import sfvelib as lib  # noqa: E402

A_TILE = [""]  # the board focus of fighter A's tile, once found
TRAIL: list[str] = []  # each press and the pick screen's page and focus after it, for a failure's report
PICK = re.compile(r"^(\S+) costume (\d+) colour (\d+) vskill (\S+) vtrigger (\S+)")
SETTINGS = lib.LOG_DIR.parent / "settings.json"
SAVE_WAIT_S = 15.0
CLOSE_LIMIT_S = 30.0


class Look:
    def __init__(self, text: str):
        m = PICK.match(text)
        if not m:
            raise lib.SfveError(f"cannot read the pick text {text!r}")
        self.code, self.costume, self.colour, self.vskill, self.vtrigger = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), m.group(5)
        self.text = text

    def look(self) -> tuple:
        return (self.costume, self.colour, self.vskill, self.vtrigger)

    def __repr__(self) -> str:
        return f"{self.code} {self.look()}"


_press = sh.press


def _pressed(index: int, button: str) -> dict:
    """runshell's press, noting where the screen is afterwards."""
    result = _press(index, button)
    place = result.get("pick") or {}
    TRAIL.append(f"{button}: screen {result.get('screen')}, pick screen {place.get('page')}/{place.get('focus')}")
    return result


sh.press = _pressed  # runshell's own seek helpers call press by its module name, so they are noted too


def print_trail(limit: int = 40) -> None:
    """The pick screen's page and focus after each press, the last `limit` of them."""
    if not TRAIL:
        return
    print(f"the presses so far ({len(TRAIL)}; the last {min(limit, len(TRAIL))}), each with the screen after it:")
    for line in TRAIL[-limit:]:
        print(f"  {line}")


def stored(index: int = 0) -> Look:
    return Look(sh.pick_place(index)["stored"])


def room_pick(index: int = 0) -> Look:
    return Look(sh.status(index)["room"]["pick"])


def shot(out: Path, name: str) -> None:
    lib.screenshot(lib.status_pid(0), out / f"{name}.png")


def space() -> dict:
    root = Path(__file__).resolve().parent.parent
    return {f["code"]: f for f in json.loads((root / "data" / "pick-space.json").read_text(encoding="utf-8"))["fighters"]}


def suitable(fighter: dict) -> bool:
    """Two V-Skills, two V-Triggers, and two costumes that each have two colours."""
    costumes = [c for c in fighter["costumes"] if len(c["colours"]) >= 2]
    return fighter.get("v_skills", 0) >= 2 and fighter.get("v_triggers", 0) >= 2 and len(costumes) >= 2


def to_look_page() -> None:
    """Home, Training, Training select's Fighter row: the player's pick screen, opened on the board with the
    stored fighter's tile focused (Training's rows open the board, Stage's the stage page); Select on that tile
    opens the stored fighter's look page (its Fighter, Costume, Colour, V-Skill, V-Trigger and Stage rows)."""
    sh.open_home(0)
    sh.seek(0, "training", ["DOWN"], 12)
    sh.press(0, "A")
    sh.wait_for("Training select", 20, lambda: sh.shell(0)["shown"] and sh.shell(0)["screen"] == "training")
    sh.expect(0, "training", "training-fighter")
    sh.press(0, "A")
    sh.expect_pick(0, "home")
    place = sh.pick_place(0)
    if place["dummy"]:
        raise lib.SfveError("Training select's Fighter row opened the dummy's pick screen")
    if not place["focus"].startswith("fighter-"):
        raise lib.SfveError(f"the board opened on {place['focus']}, not the stored fighter's tile")
    sh.press(0, "A")  # Select on the stored fighter's tile: its look page
    sh.expect_pick(0, "look")


def fighter_row(step: str) -> Look:
    """On the look page: the Fighter row, then Left or Right."""
    sh.seek_pick(0, "fighter", ["UP"], 8)
    sh.press(0, step)
    return stored()


def step_row(row: str, what: str, out: Path) -> Look:
    before = stored()
    sh.seek_pick(0, row, ["DOWN", "UP"], 10)
    sh.press(0, "RIGHT")
    after = stored()
    print(f"  {what}: {before.look()} -> {after.look()}")
    if after.text == before.text:
        raise lib.SfveError(f"stepping the {row} row did not change the pick ({after.text})")
    return after


def ensure_suitable_fighter(fighters: dict) -> Look:
    """On the look page: the Fighter row stepped along until the fighter has two V-Skills, two V-Triggers and two
    costumes of two colours. Then Back to the board (its focus is the fighter's tile, which A_TILE keeps) and
    Select on that tile: the fighter's look page again."""
    for _ in range(60):
        here = stored()
        if here.code in fighters and suitable(fighters[here.code]):
            break
        fighter_row("RIGHT")
    else:
        raise lib.SfveError("no fighter with two V-Skills, two V-Triggers and two costumes of two colours")
    sh.press(0, "B")
    sh.expect_pick(0, "home")
    A_TILE[0] = sh.pick_place(0)["focus"]
    if not A_TILE[0].startswith("fighter-"):
        raise lib.SfveError(f"the board's focus is {A_TILE[0]}, not the stored fighter's tile")
    sh.press(0, "A")
    sh.expect_pick(0, "look")
    return stored()


def settings_pick(wait_for_fighter: str | None = None) -> dict | None:
    """overlay.sfvPicks of the settings file (once it holds `wait_for_fighter`, when given)."""
    deadline = time.time() + SAVE_WAIT_S
    while True:
        try:
            data = json.loads(SETTINGS.read_bytes().decode("utf-8-sig"))
            picks = data.get("overlay", {}).get("sfvPicks")
        except (OSError, ValueError):
            picks = None
        if picks is not None and (wait_for_fighter is None or picks.get("fighter") == wait_for_fighter):
            return picks
        if time.time() > deadline:
            return picks
        time.sleep(0.5)


def launch_copy() -> None:
    tr.launch([0], mash=False)


def close_normally() -> float:
    pid = lib.status_pid(0)
    started = time.monotonic()
    lib.close_window(pid)
    while pid in lib.running_pids():
        if time.monotonic() - started > CLOSE_LIMIT_S:
            raise lib.SfveError(f"the copy was still running {CLOSE_LIMIT_S:.0f} s after its window was closed")
        time.sleep(0.2)
    return time.monotonic() - started


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--keep-settings", action="store_true")
    a = p.parse_args(argv)
    a.out.mkdir(parents=True, exist_ok=True)
    backup = a.out / "settings.before.json"
    had_settings = SETTINGS.exists()
    if had_settings:
        shutil.copy2(SETTINGS, backup)
    lib.begin_run()
    lib.kill_all()
    failure = ""
    fighters = space()
    with lib.run_scope():
        try:
            tr.take_launch_lock()
            launch_copy()
            to_look_page()
            first = ensure_suitable_fighter(fighters)
            a_tile = A_TILE[0]
            print(f"1. fighter {first.code} chosen, look page open; its first look {first.look()}")
            shot(a.out, "1-a-default")
            first_look = first.look()
            a1 = step_row("costume", "costume", a.out)
            a2 = step_row("color", "colour", a.out)
            a3 = step_row("vskill", "V-Skill", a.out)
            a4 = step_row("vtrigger", "V-Trigger", a.out)
            shot(a.out, "2-a-changed")
            A = a4
            if len({first.costume, a1.costume}) < 2 or A.vskill == first.vskill or A.vtrigger == first.vtrigger or A.colour == a1.colour:
                raise lib.SfveError(f"the four steps did not each change a part of the look ({first_look} -> {A.look()})")
            print(f"   A's look is {A.look()}")

            B = fighter_row("RIGHT")
            shot(a.out, "3-b-default")
            print(f"2. stepped to the next fighter: {B}")
            if B.code == A.code:
                raise lib.SfveError("the Fighter row did not move to another fighter")
            if B.vskill != first.vskill or B.vtrigger != first.vtrigger:
                raise lib.SfveError(f"{B.code} has V-Skill/V-Trigger {B.vskill}/{B.vtrigger}: A's changed look {A.vskill}/{A.vtrigger} was carried over")
            if (B.costume, B.colour) == (A.costume, A.colour) and fighters[B.code]["costumes"][0]["id"] != A.costume:
                raise lib.SfveError(f"{B.code} has A's costume {A.costume} and colour {A.colour}")
            b_default = B.look()
            back = fighter_row("LEFT")
            shot(a.out, "4-a-back")
            print(f"   stepped back: {back}")
            if back.text != A.text:
                raise lib.SfveError(f"A's look did not come back: {A.text!r} then {back.text!r}")

            # Random on: the board's Random tile, chosen, then stored (opens the rolled fighter's page).
            sh.press(0, "B")  # the board
            sh.expect_pick(0, "home")
            sh.seek_board(0, "the Random tile", lambda place: place["focus"] == "random-fighter")
            sh.press(0, "A")
            time.sleep(1)
            if not sh.pick_place(0)["random"]:
                raise lib.SfveError("Random was not turned on by Select on its tile")
            print(f"3. Random is on, rolled {stored()}")
            sh.press(0, "A")
            sh.expect_pick(0, "look")
            if not sh.pick_place(0)["random"]:
                raise lib.SfveError("the rolled fighter's page is not Random's")
            shot(a.out, "5-random-look-page")
            rolled = stored()
            after = fighter_row("RIGHT")
            print(f"   the Fighter row stepped from the rolled {rolled.code} to {after.code}; Random is now {sh.pick_place(0)['random']}")
            if sh.pick_place(0)["random"]:
                raise lib.SfveError("stepping the Fighter row did not turn Random off")
            shot(a.out, "6-random-off")

            # A again, from the board: its look is the one set before the other fighters were visited.
            sh.press(0, "B")
            sh.expect_pick(0, "home")
            sh.seek_board(0, f"{A.code}'s tile", lambda place: place["focus"] == a_tile)
            sh.press(0, "A")
            sh.expect_pick(0, "look")
            again = stored()
            print(f"4. {A.code} chosen again from the board: {again}")
            if again.text != A.text:
                raise lib.SfveError(f"A's look was lost: {A.text!r} then {again.text!r}")
            shot(a.out, "7-a-again")

            # 5. Saved, closed normally, started again.
            saved = settings_pick(A.code)
            print(f"5. settings.json overlay.sfvPicks: {json.dumps(saved)}")
            if not saved or saved.get("fighter") != A.code:
                raise lib.SfveError(f"settings.json has no overlay.sfvPicks for {A.code} within {SAVE_WAIT_S:.0f} s ({saved})")
            kept = saved.get("looks", {}).get(A.code)
            if not kept or [kept.get("costume"), kept.get("colour")] != [A.costume, A.colour]:
                raise lib.SfveError(f"settings.json keeps {kept} for {A.code}, not {A.look()}")
            seconds = close_normally()
            print(f"   the copy closed in {seconds:.1f} s")
            time.sleep(2)
            shutil.copy2(SETTINGS, a.out / "settings.after-close.json")
            final = settings_pick()
            if not final or final.get("fighter") != A.code:
                raise lib.SfveError(f"after the close settings.json says {final}")
            if (lib.LOG_DIR / "sfv_ember.old1.log").exists():
                print("   (the next start rotates the run's log to sfv_ember.old1.log)")
            tr.release_launch_lock()
            tr.take_launch_lock()
            launch_copy()
            sh.wait_for("the stored pick to be read", 60, lambda: sh.status(0)["room"]["pick"].startswith(A.code + " "))
            restored = room_pick()
            print(f"   started again: the stored pick is {restored}")
            if restored.text.split(" stage")[0] != A.text.split(" stage")[0]:
                raise lib.SfveError(f"the run did not start with A's look: {A.text!r} then {restored.text!r}")
            to_look_page()
            shot(a.out, "8-restart-a")
            second = stored()
            print(f"   the look page after the restart: {second}")
            if second.look() != A.look():
                raise lib.SfveError(f"after the restart A's look is {second.look()}, not {A.look()}")
            nb = fighter_row("RIGHT")
            shot(a.out, "9-restart-b")
            print(f"   stepped to {nb.code}: {nb.look()} (default before: {b_default})")
            if nb.code != B.code or nb.look() != b_default:
                raise lib.SfveError(f"after the restart the next fighter is {nb.code} {nb.look()}, not {B.code} {b_default}")
            back2 = fighter_row("LEFT")
            if back2.look() != A.look():
                raise lib.SfveError(f"stepped back after the restart: {back2.look()}, not {A.look()}")
            shot(a.out, "10-restart-a-back")
            print("   B's default stays, A's look is back")
        except (lib.SfveError, KeyError, TypeError) as e:
            failure = str(e) or type(e).__name__
            print_trail()
            try:
                shot(a.out, "failure")
            except (lib.SfveError, OSError):
                pass
        finally:
            tr.release_launch_lock()
    if not a.keep_settings:
        lib.kill_all()
        time.sleep(2)
        if had_settings:
            shutil.copy2(backup, SETTINGS)
        elif SETTINGS.exists():
            SETTINGS.unlink()
        print("settings.json put back as it was before the run")
    return lib.verdict(failure)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
